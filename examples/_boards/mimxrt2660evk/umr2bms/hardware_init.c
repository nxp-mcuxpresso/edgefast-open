/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*${header:start}*/
#include "clock_config.h"
#include "board.h"
#include "pin_mux.h"

#include "fsl_edma.h"
#include "fsl_xspi.h"
#include "fsl_lpi2c.h"
#include "fsl_iomuxc.h"
#include "fsl_debug_console.h"
#include "fsl_adapter_uart.h"
#include "fsl_tpm.h"
#include "fsl_clock.h"
#include "fsl_common.h"
#include <zephyr/drivers/bluetooth.h>
#include <psa/crypto.h>
#include <mbedtls/platform.h>
#include "fsl_pcal6524.h"
/*${header:end}*/

/*${macro:start}*/
/* QTPM - LE Audio sync signal.
 * umr2bms bridges 2 CIS streams onto 2 BIS channels. It does not locally play
 * audio, so it only needs to count 2EL SyncSignal edges (BOARD_SyncSignal_*),
 * not the SAI0_TX_SYNC/BCLK offset measured by the unicast_media_receiver.
 * PIO1_2 is WAKE__TPM_1 channel 0 (the 2EL SyncSignal). It is captured on
 * TPM1 input capture and each rising edge increments a running counter.
 */
#define SyncSignal_TPM       (WAKE__TPM_1)
#define SyncSignal_TPM_Irq   (WAKE_QTPM1_IRQn)
#define SyncSignal_TPM_Ch    (kTPM_Chnl_0)
/*${macro:end}*/

/*${variable:start}*/
static volatile uint32_t SyncSignal_Index = 0; /* Running count of 2EL SyncSignal edges. */
/*${variable:end}*/

/*${function:start}*/
static void BOARD_InitQTPMPins(void)
{
    IOMUXC_SetPin_Mux_Config(IOMUXC_PIO1_2_WAKE_QTPM0_CH2, 0x80U); /* PIO1_2: WAKE__TPM_1 ch0, input buffer enable */
}

static void BOARD_SyncSignal_Init(void)
{
    tpm_config_t config;

    BOARD_InitQTPMPins();

    /* WAKE_QTPM0/1 share one functional clock root (qtpm0_fclk). Point it at
     * the CGU AUDIO root so the timer counter has a functional clock; the exact
     * rate is not important here because only edge counts are used. */
    clock_root_config_t qtpmRootCfg = {
        .mux = kCLOCK_QTPM0_ClockRoot_AUDIO,
        .div = 1,
    };
    CLOCK_SetRootClock(kCLOCK_Root_WAKE_qtpm0_fclk, &qtpmRootCfg);

    TPM_GetDefaultConfig(&config);
    TPM_Init(SyncSignal_TPM, &config);
    TPM_SetupInputCapture(SyncSignal_TPM, SyncSignal_TPM_Ch, kTPM_RisingEdge);
    TPM_SetTimerPeriod(SyncSignal_TPM, TPM_MAX_COUNTER_VALUE(SyncSignal_TPM));

    EnableIRQWithPriority(SyncSignal_TPM_Irq, 3);
}

void BOARD_SyncSignal_Start(uint32_t init_offset)
{
    SyncSignal_Index = init_offset;

    TPM_ClearStatusFlags(SyncSignal_TPM, kTPM_Chnl0Flag);
    TPM_EnableInterrupts(SyncSignal_TPM, kTPM_Chnl0InterruptEnable);
    TPM_StartTimer(SyncSignal_TPM, kTPM_SystemClock);
}

void BOARD_SyncSignal_Stop(void)
{
    TPM_DisableInterrupts(SyncSignal_TPM, kTPM_Chnl0InterruptEnable);
    (void)TPM_StopTimer(SyncSignal_TPM);
}

uint32_t BOARD_SyncSignal_Count(void)
{
    return SyncSignal_Index;
}

void WAKE_QTPM1_IRQHandler(void)
{
    uint32_t flags = TPM_GetStatusFlags(SyncSignal_TPM);

    if ((flags & kTPM_Chnl0Flag) != 0U)
    {
        /* 2EL SyncSignal rising edge; count it. */
        SyncSignal_Index++;
    }

    TPM_ClearStatusFlags(SyncSignal_TPM, flags);
    SDK_ISR_EXIT_BARRIER;
}

/* -------------------------------------------------------------------------
 * mbedTLS/PSA hardware entropy source.
 *
 * The tf_psa_crypto entropy accumulator calls mbedtls_platform_get_entropy
 * as its platform entropy provider. RT2663 is not yet supported by any
 * psa_crypto_driver component, so no component provides this symbol and the
 * link fails with "undefined reference to mbedtls_platform_get_entropy". This
 * is a pragmatic bring-up entropy source; replace with a certified TRNG
 * driver for production security requirements.
 * ------------------------------------------------------------------------- */
int mbedtls_platform_get_entropy(psa_driver_get_entropy_flags_t flags, size_t *estimate_bits, unsigned char *output,
                                  size_t output_size)
{
    if (output == NULL || estimate_bits == NULL) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < output_size; i++) {
        output[i] = (unsigned char)rand();
    }

    *estimate_bits = 8u * output_size;

    return 0;
}

#if (defined(CONFIG_BT_SMP) && (CONFIG_BT_SMP > 0))
void bt_psa_crypto_init(void)
{
    psa_status_t status;

    status = psa_crypto_init();
    if (status != PSA_SUCCESS)
    {
        PRINTF("Failed to initialize PSA crypto");
    }
    assert(status == PSA_SUCCESS);
}
#endif /* CONFIG_BT_SMP */

/*
 * WiFi/BT reset lines are wired to the on-board PCAL6524 I2C I/O expander.
 * PCAL6524 pin numbering: P0_x = 0..7, P1_x = 8 + x, P2_x = 16 + x.
 *   WL_RST#     -> P1_4 -> expander pin 12
 *   WIFI_RST_B  -> P1_5 -> expander pin 13
 * Both signals are active-low, so driving low asserts reset and driving
 * high releases reset.
 */
#define BOARD_WL_RST_PIN        (12U)
#define BOARD_WIFI_RST_PIN      (13U)
#define BOARD_WL_RST_MASK       (1UL << BOARD_WL_RST_PIN)
#define BOARD_WIFI_RST_MASK     (1UL << BOARD_WIFI_RST_PIN)
#define BOARD_WIFI_RST_ALL_MASK (BOARD_WL_RST_MASK | BOARD_WIFI_RST_MASK)

void BOARD_ResetM2(void)
{
    /* Board-level singleton handle. The first call lazily performs the LPI2C0
     * init, BOARD_Init6524Pins() pin mux, and PCAL6524_Init(). */
    pcal6524_handle_t *exp = BOARD_GetPCAL6524Handle();

    /* Configure WL_RST# (P1_4) and WIFI_RST_B (P1_5) as outputs. */
    (void)PCAL6524_SetDirection(exp, BOARD_WIFI_RST_ALL_MASK, kPCAL6524_Output);

    /* Assert reset on both lines (drive low). */
    (void)PCAL6524_ClearPins(exp, BOARD_WIFI_RST_ALL_MASK);
    /* Hold reset asserted for 100 ms (argument unit is microseconds). */
    SDK_DelayAtLeastUs(100U * 1000U, SystemCoreClock);

    /* Release reset on both lines (drive high). */
    (void)PCAL6524_SetPins(exp, BOARD_WIFI_RST_ALL_MASK);
}

void BOARD_InitHardware(void)
{
    /* Board common setting: MPU, Power and Clock Tree, TRDC, and Debug Console init. */
    BOARD_CommonSetting();
    BOARD_InitUSDHC0Pins();
    BOARD_InitM2UARTPins();
    BOARD_Init6524Pins();

    BOARD_SyncSignal_Init();

#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    /*
     * RT2660 uses the unified eDMA. There is no separate DMAMUX peripheral;
     * the DMA request source is selected directly on the eDMA channel. So only
     * the eDMA controller needs to be initialized here.
     */
    edma_config_t config;
    EDMA_Type *dmaBases[] = EDMA_BASE_PTRS;
    EDMA_GetDefaultConfig(&config);
    EDMA_Init(dmaBases[0], &config);
#endif

    BOARD_ResetM2();
}

#if defined(WIFI_IW612_BOARD_MURATA_2EL_M2)
int controller_hci_uart_get_configuration(controller_hci_uart_config_t *config)
{
    if (NULL == config)
    {
        return -1;
    }
    config->clockSrc        = BOARD_BT_UART_CLK_FREQ;
    config->defaultBaudrate = 115200u;
    config->runningBaudrate = BOARD_BT_UART_BAUDRATE;
    config->instance        = BOARD_BT_UART_INSTANCE;
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    config->dma_instance = 0U;
    config->rx_channel   = 0U;
    config->tx_channel   = 1U;
    /* No standalone DMAMUX on RT2660; request source is set on the eDMA channel. */
    config->rx_request = kDmaRequestMux1HspLPUART2Rx;
    config->tx_request = kDmaRequestMux1HspLPUART2Tx;
#endif
    config->enableRxRTS = 1u;
    config->enableTxCTS = 1u;
    return 0;
}
#endif
/*${function:end}*/

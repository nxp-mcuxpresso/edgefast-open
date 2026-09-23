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
#include "ele_crypto.h"
#include <string.h>

/* The ELE runtime firmware container. Both the array name (ele_fw) and the
 * header file name (ele_fw_header) are provided as -D macros by the
 * component.edgelock.s100.rt2660a0_ele_container component, so the same two
 * lines work for any container variant selected in prj.conf. */
#define ELE_NAME_TO_STR(x)      #x
#define ELE_FW_HEADER_STRING(x) ELE_NAME_TO_STR(x)
#include ELE_FW_HEADER_STRING(ele_fw_header)
/* S3MU instance used to talk to the ELE (EdgeLock Enclave). Same alias the
 * ele_crypto examples define in their app.h. */
#define S3MU MAIN__SENTMU0_SENTMUA
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
 * mbedTLS/PSA hardware entropy source backed by the ELE (EdgeLock Enclave)
 * TRNG on RT2660.
 *
 * Split in two parts:
 *   BOARD_Init_TRNG()  - one-shot bring-up, called from BOARD_InitHardware().
 *                        Starts the ELE RNG service and waits for the
 *                        hardware TRNG to report valid entropy.
 *   mbedtls_platform_get_entropy() - the tf_psa_crypto (mbedTLS 4.x)
 *                        platform entropy hook; only reads random bytes.
 * ------------------------------------------------------------------------- */
void BOARD_Init_TRNG(void)
{
    status_t result;
    uint32_t trng_state = 0u;
    uint32_t fwstatus   = 0u;
    uint32_t retry;

    /* On RT2660 the boot ROM leaves the enclave running ROM code only, which
     * does not implement the crypto service command group. The ELE runtime
     * firmware container therefore has to be loaded and authenticated first;
     * without it ELE_InitServices() and every RNG command return
     * kStatus_Fail. */
    result = ELE_LoadFw(S3MU, ele_fw);
    if (result != kStatus_Success)
    {
        PRINTF("ELE_LoadFw failed (%d)\r\n", (int)result);
        return;
    }

    /* Bring up the crypto service interface of the freshly loaded firmware. */
    result = ELE_InitServices(S3MU);
    if (result != kStatus_Success)
    {
        PRINTF("ELE_InitServices failed (%d)\r\n", (int)result);
        return;
    }

    /* Confirm the firmware is really authenticated and running, the same check
     * the ele_crypto examples perform right after ELE_InitServices(). A zero
     * status means no ELE FW is in place, in which case none of the crypto
     * service commands below can work. */
    result = ELE_GetFwStatus(S3MU, &fwstatus);
    if (result != kStatus_Success)
    {
        PRINTF("ELE_GetFwStatus failed (%d)\r\n", (int)result);
        return;
    }
    if (fwstatus == 0u)
    {
        PRINTF("No ELE FW in place, FW status 0x%08x\r\n", (unsigned int)fwstatus);
        return;
    }

    /* Kick the ELE RNG service; on a cold boot this triggers the TRNG
     * start-up sequence inside the enclave. Treated as fatal, like in the
     * ele_crypto examples: without a started RNG there is no entropy source
     * for LE Secure Connections. */
    result = ELE_StartRng(S3MU);
    if (result != kStatus_Success)
    {
        PRINTF("ELE_StartRng failed (%d)\r\n", (int)result);
        return;
    }

    /* Wait until the hardware TRNG reports that its entropy is valid and ready
     * to be read. Only kELE_TRNG_ready means that; kELE_TRNG_CSAL_success only
     * says the Crypto Lib random context was initialized, which is not a
     * statement about entropy quality, so it is not accepted here. A bounded
     * loop is used so an unhappy state field cannot hang the boot. */
    for (retry = 0u; retry < 1000u; retry++)
    {
        result = ELE_GetTrngState(S3MU, &trng_state);
        if (result != kStatus_Success)
        {
            PRINTF("ELE_GetTrngState failed (%d)\r\n", (int)result);
            return;
        }
        if ((trng_state & 0xFFu) == (uint32_t)kELE_TRNG_ready)
        {
            break;
        }
    }

    if ((trng_state & 0xFFu) != (uint32_t)kELE_TRNG_ready)
    {
        PRINTF("ELE TRNG not ready, state 0x%08x (expected low byte 0x%02x)\r\n", (unsigned int)trng_state,
               (unsigned int)kELE_TRNG_ready);
        return;
    }
}

/* Staging buffer for the random bytes produced by the ELE.
 *
 * The enclave writes into this buffer through the system bus, not through the
 * CPU, so it must not be a cached, CPU-owned buffer:
 *   - It lives in the non-cacheable region, so the CPU always sees what the
 *     enclave actually wrote (no stale line, no need for a clean/invalidate
 *     pair around every call).
 *   - It is cache-line aligned and its size is a whole number of cache lines.
 *     ele_crypto is built with ELE_CACHE_HANDLING, which makes
 *     ELE_RngGetRandom() call DCACHE_InvalidateByRange() on this address.
 *     That operation works on whole cache lines, so a smaller or unaligned
 *     buffer would also invalidate the neighbouring variables that share its
 *     first/last line and silently drop their cached contents.
 *
 * The size is one cache line, which already covers the largest entropy request
 * PSA makes; larger requests are served by looping. */
#define BOARD_TRNG_BUF_SIZE FSL_FEATURE_L1DCACHE_LINESIZE_BYTE
AT_NONCACHEABLE_SECTION_ALIGN(static uint32_t s_trngBuf[BOARD_TRNG_BUF_SIZE / sizeof(uint32_t)],
                              FSL_FEATURE_L1DCACHE_LINESIZE_BYTE);

/* Read random bytes from the already-initialized ELE TRNG. ELE_RngGetRandom
 * takes a uint32_t pointer and works on whole words, so the request is rounded
 * up to a word and the result is then copied byte-for-byte into the caller's
 * buffer. */
static status_t BOARD_GetRandomData(uint8_t *output, size_t output_size)
{
    size_t offset;
    size_t chunk;
    size_t words;

    for (offset = 0u; offset < output_size; offset += chunk)
    {
        chunk = output_size - offset;
        if (chunk > sizeof(s_trngBuf))
        {
            chunk = sizeof(s_trngBuf);
        }
        words = (chunk + 3u) / 4u;
        if (ELE_RngGetRandom(S3MU, s_trngBuf, words * 4u, kNoReseed) != kStatus_Success)
        {
            return kStatus_Fail;
        }
        memcpy(output + offset, s_trngBuf, chunk);
    }

    return kStatus_Success;
}

int mbedtls_platform_get_entropy(psa_driver_get_entropy_flags_t flags, size_t *estimate_bits, unsigned char *output,
                                  size_t output_size)
{
    if (output == NULL || estimate_bits == NULL)
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    /* Ask the ELE directly. The TRNG state field polled in BOARD_Init_TRNG is
     * only informational; ELE_RngGetRandom succeeding is the real proof that
     * the hardware entropy source is usable. */
    if (BOARD_GetRandomData(output, output_size) != kStatus_Success)
    {
        PRINTF("ELE_RngGetRandom failed\r\n");
        return PSA_ERROR_HARDWARE_FAILURE;
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
    BOARD_InitUSDHC1Pins();
    BOARD_InitM2UARTPins();
    BOARD_Init6524Pins();

    /* Bring up the ELE hardware TRNG that backs mbedtls_platform_get_entropy;
     * BLE LE Secure Connections needs a real entropy source so every boot
     * generates a different P-256 key pair. */
    BOARD_Init_TRNG();

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

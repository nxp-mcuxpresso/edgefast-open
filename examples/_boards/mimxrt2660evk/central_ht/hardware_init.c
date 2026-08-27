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

#include "fsl_debug_console.h"
#include "fsl_adapter_uart.h"
#include "fsl_lpuart_edma.h"
#include "fsl_edma.h"
#include <zephyr/drivers/bluetooth.h>
#if defined(BOARD_USE_PCAL6524) && BOARD_USE_PCAL6524
#include "fsl_pcal6524.h"
#endif
/*${header:end}*/

/*${variable:start}*/

/*${variable:end}*/

/*${function:start}*/

#if defined(BOARD_USE_PCAL6524) && BOARD_USE_PCAL6524
/*
 * WiFi/BT reset lines are wired to the on-board PCAL6524 I2C I/O expander.
 * PCAL6524 pin numbering: P0_x = 0..7, P1_x = 8 + x, P2_x = 16 + x.
 *   WL_RST#     -> P1_4 -> expander pin 12
 *   WIFI_RST_B  -> P1_5 -> expander pin 13
 * Both signals are active-low, so driving low asserts reset and driving
 * high releases reset.
 */
#define BOARD_WL_RST_PIN    (12U)
#define BOARD_WIFI_RST_PIN  (13U)
#define BOARD_WL_RST_MASK   (1UL << BOARD_WL_RST_PIN)
#define BOARD_WIFI_RST_MASK (1UL << BOARD_WIFI_RST_PIN)
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
    SDK_DelayAtLeastUs(5000U, SystemCoreClock);

    /* Release reset on both lines (drive high). */
    (void)PCAL6524_SetPins(exp, BOARD_WIFI_RST_ALL_MASK);
    SDK_DelayAtLeastUs(10000U, SystemCoreClock);
}

#endif /* BOARD_USE_PCAL6524 */

/* -------------------------------------------------------------------------
 * mbedTLS hardware entropy source.
 *
 * mbedtls_entropy_init() references mbedtls_hardware_poll as the platform
 * entropy provider. RT2663 is not yet supported by any psa_crypto_driver
 * component, so no component provides this symbol and the link fails with
 * "undefined reference to mbedtls_hardware_poll". A running DWT cycle counter
 * is mixed to seed the entropy accumulator. This is a pragmatic bring-up
 * entropy source; replace with a certified TRNG driver for production
 * security requirements.
 * ------------------------------------------------------------------------- */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    (void)data;

    if ((NULL == output) || (NULL == olen))
    {
        return -1;
    }

    /* Enable the DWT cycle counter (best-effort; harmless if already on). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    for (size_t i = 0U; i < len; i++)
    {
        uint32_t sample = DWT->CYCCNT;
        sample ^= (sample >> 8) ^ (sample >> 16) ^ (sample >> 24);
        output[i] = (unsigned char)(sample & 0xFFU);
        /* Perturb the counter between samples. */
        for (volatile uint32_t d = 0U; d < 7U; d++)
        {
            __NOP();
        }
    }

    *olen = len;
    return 0;
}


void BOARD_InitHardware(void)
{
    BOARD_CommonSetting();
    BOARD_InitUSDHC0Pins();
    BOARD_InitM2UARTPins();
    BOARD_Init6524Pins();

#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    /*
     * RT2660 uses the unified eDMA (EDMA3/EDMA5). There is no separate DMAMUX
     * peripheral; the DMA request source is selected directly on the eDMA
     * channel (via kDmaRequestMux1... request IDs supplied when the transfer
     * handle is created). So only the eDMA controller needs to be initialized
     * here.
     */
    edma_config_t config;
    EDMA_Type *dmaBases[] = EDMA_BASE_PTRS;
    EDMA_GetDefaultConfig(&config);
    EDMA_Init(dmaBases[0], &config);
#endif

// #if defined(CONFIG_BT_HOST_CRYPTO) && !defined(CONFIG_BT_CRYPTO_PSA)
//     CRYPTO_InitHardware();
// #endif /* CONFIG_BT_HOST_CRYPTO && !CONFIG_BT_CRYPTO_PSA*/

#if defined(BOARD_USE_PCAL6524) && BOARD_USE_PCAL6524
    BOARD_ResetM2();
#endif
}


#if (defined(WIFI_IW416_BOARD_MURATA_1XK_M2) || defined(WIFI_88W8987_BOARD_MURATA_1ZM_M2) || \
     defined(WIFI_IW612_BOARD_MURATA_2EL_M2) || defined(WIFI_IW610_BOARD_MURATA_2LL_M2))
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
    config->dma_instance     = 0U;
    config->rx_channel       = 0U;
    config->tx_channel       = 1U;
    /* No standalone DMAMUX on RT2660; request source is set on the eDMA channel. */
    config->rx_request       = kDmaRequestMux1HspLPUART2Rx;
    config->tx_request       = kDmaRequestMux1HspLPUART2Tx;
#endif
    config->enableRxRTS = 1u;
    config->enableTxCTS = 1u;
    return 0;
}
#endif

/*${function:end}*/

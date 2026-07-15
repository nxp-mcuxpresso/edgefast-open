/*
 * Copyright 2025 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*${header:start}*/
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_clock.h"

#include <zephyr/drivers/bluetooth.h>

#include "fsl_lpflexcomm.h"
#include "fsl_edma_soc.h"
/*${header:end}*/

/*${macro:start}*/
#define LPUART_TX_DMA_CHANNEL       0U
#define LPUART_RX_DMA_CHANNEL       1U
#define DEMO_LPUART_TX_EDMA_CHANNEL kDma0RequestMuxLpFlexcomm2Tx
#define DEMO_LPUART_RX_EDMA_CHANNEL kDma0RequestMuxLpFlexcomm2Rx
#define EXAMPLE_LPUART_DMA_INDEX    0U

/* USB */
/*${macro:end}*/

/*${variable:start}*/

/*${variable:end}*/

/*${function:start}*/

void BOARD_InitHardware(void)
{
    /* attach FRO 12M to FLEXCOMM4 (debug console) */
    CLOCK_SetClkDiv(kCLOCK_DivFlexcom4Clk, 1u);
    CLOCK_AttachClk(BOARD_DEBUG_UART_CLK_ATTACH);

    /* attach FRO 12M to FLEXCOMM2 (M.2) */
    CLOCK_SetClkDiv(kCLOCK_DivFlexcom2Clk, 1u);
    CLOCK_AttachClk(BOARD_BT_UART_CLK_ATTACH);

    /* attach FRO HF to USDHC */
    CLOCK_SetClkDiv(kCLOCK_DivUSdhcClk, 1u);
    CLOCK_AttachClk(kFRO_HF_to_USDHC);

    /* Enables the clock for GPIO0 */
    CLOCK_EnableClock(kCLOCK_Gpio0);
    /* Enables the clock for GPIO2 */
    CLOCK_EnableClock(kCLOCK_Gpio2);

    BOARD_InitBootPins();
    BOARD_PowerMode_OD();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    /* Enables the clock for LPFlexComm2 */
#if defined(LPFLEXCOMM_INIT_NOT_USED_IN_DRIVER) && LPFLEXCOMM_INIT_NOT_USED_IN_DRIVER
    LP_FLEXCOMM_Init(BOARD_BT_UART_INSTANCE, LP_FLEXCOMM_PERIPH_LPI2CAndLPUART);
#endif /* LPFLEXCOMM_INIT_NOT_USED_IN_DRIVER */

    // XCACHE_DisableCache(XCACHE_PS);
}

#if (defined(WIFI_IW416_BOARD_AW_AM510_ARDUINO) || defined(WIFI_IW610_BOARD_MURATA_2LL_M2) || \
     defined(WIFI_IW416_BOARD_MURATA_1XK_M2))
int controller_hci_uart_get_configuration(controller_hci_uart_config_t *config)
{
    if (NULL == config)
    {
        return -1;
    }
    config->clockSrc        = BOARD_BT_UART_CLK_FREQ;
#if defined(WIFI_IW416_BOARD_AW_AM510_ARDUINO)
    config->defaultBaudrate = BOARD_BT_UART_BAUDRATE;
#else
    config->defaultBaudrate = 115200u;
#endif
    config->runningBaudrate = BOARD_BT_UART_BAUDRATE;
    config->instance        = BOARD_BT_UART_INSTANCE;
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    config->dma_instance = EXAMPLE_LPUART_DMA_INDEX;
    config->rx_channel   = LPUART_RX_DMA_CHANNEL;
    config->tx_channel   = LPUART_TX_DMA_CHANNEL;
#if (defined(FSL_FEATURE_EDMA_HAS_CHANNEL_MUX) && (FSL_FEATURE_EDMA_HAS_CHANNEL_MUX > 0U))
    config->rx_request = DEMO_LPUART_RX_EDMA_CHANNEL;
    config->tx_request = DEMO_LPUART_TX_EDMA_CHANNEL;
#endif
#endif
    config->enableRxRTS = 1u;
    config->enableTxCTS = 1u;
    return 0;
}
#endif
/*${function:end}*/

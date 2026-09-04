/*
 * Copyright 2022,2025 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*${header:start}*/
#include "clock_config.h"
#include "board.h"
#include "pin_mux.h"
#include "fsl_dmamux.h"
#include "fsl_edma.h"
#include "fsl_debug_console.h"
#include "fsl_adapter_uart.h"
#include "fsl_lpuart_edma.h"
#include <zephyr/drivers/bluetooth.h>
/*${header:end}*/

/*${macro:start}*/
#if defined(__GIC_PRIO_BITS)
#define USB_HOST_INTERRUPT_PRIORITY (25U)
#elif defined(__NVIC_PRIO_BITS) && (__NVIC_PRIO_BITS >= 3)
#define USB_HOST_INTERRUPT_PRIORITY (6U)
#else
#define USB_HOST_INTERRUPT_PRIORITY (3U)
#endif
#if (defined(WIFI_88W8987_BOARD_MURATA_1ZM_M2) || defined(WIFI_IW612_BOARD_MURATA_2EL_M2) || \
     defined(BT_NW61X_BOARD_NXP_RD_USD))
#define M2_WIFI_RESET_GPIO     GPIO9
#define M2_WIFI_RESET_GPIO_PIN 30U
#endif

/* DMA */
#define EXAMPLE_DMAMUX_INSTANCE (0U)
#define EXAMPLE_DMA_INSTANCE    (0U)
/*${macro:end}*/

/*${variable:start}*/
/*${variable:end}*/

/*${function:start}*/

void BOARD_InitHardware(void)
{
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    DMAMUX_Type *dmaMuxBases[] = DMAMUX_BASE_PTRS;
    edma_config_t config;
    DMA_Type *dmaBases[] = DMA_BASE_PTRS;
#endif

    BOARD_ConfigMPU();
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    DMAMUX_Init(dmaMuxBases[EXAMPLE_DMAMUX_INSTANCE]);
    EDMA_GetDefaultConfig(&config);
    EDMA_Init(dmaBases[EXAMPLE_DMA_INSTANCE], &config);
#endif
}

#if (defined(WIFI_88W8987_BOARD_MURATA_1ZM_M2) || defined(WIFI_IW612_BOARD_MURATA_2EL_M2) || \
     defined(BT_NW61X_BOARD_NXP_RD_USD))
int controller_hci_uart_get_configuration(controller_hci_uart_config_t *config)
{
    if (NULL == config)
    {
        return -1;
    }
    config->clockSrc        = CLOCK_GetRootClockFreq(kCLOCK_Root_Lpuart2);
    config->defaultBaudrate = 115200u;
    config->runningBaudrate = BOARD_BT_UART_BAUDRATE;
    config->instance        = 2;
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    config->dma_instance     = 0U;
    config->rx_channel       = 4U;
    config->tx_channel       = 5U;
    config->dma_mux_instance = 0U;
    config->rx_request       = kDmaRequestMuxLPUART2Rx;
    config->tx_request       = kDmaRequestMuxLPUART2Tx;
#endif
    config->enableRxRTS = 1u;
    config->enableTxCTS = 1u;
    return 0;
}
#endif

/*${function:end}*/

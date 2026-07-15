/*
 * Copyright 2020,2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*${header:start}*/
#include "pin_mux.h"
#include "board.h"
#include "clock_config.h"
#include "fsl_adapter_uart.h"

#include <zephyr/drivers/bluetooth.h>
#include "fsl_lpuart_edma.h"
#include "fsl_dmamux.h"
/*${header:end}*/

/*${macro:start}*/
#if defined(__GIC_PRIO_BITS)
#elif defined(__NVIC_PRIO_BITS) && (__NVIC_PRIO_BITS >= 3)
#else
#endif
/* MURATA wifi reset pin */
#if (defined(WIFI_IW416_BOARD_MURATA_1XK_USD) || defined(WIFI_88W8987_BOARD_MURATA_1ZM_USD) || \
     defined(WIFI_IW612_BOARD_MURATA_2EL_USD))
#define MURATA_WIFI_RESET_GPIO     GPIO1
#define MURATA_WIFI_RESET_GPIO_PIN 24U
#endif
/*${macro:end}*/

/*${variable:start}*/

/*${variable:end}*/

/*${function:start}*/

void BOARD_InitHardware(void)
{
    BOARD_ConfigMPU();
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    DMAMUX_Type *dmaMuxBases[] = DMAMUX_BASE_PTRS;
    edma_config_t config;
    DMA_Type *dmaBases[] = DMA_BASE_PTRS;
    DMAMUX_Init(dmaMuxBases[0]);
    EDMA_GetDefaultConfig(&config);
    EDMA_Init(dmaBases[0], &config);
#endif
#if (defined(WIFI_IW416_BOARD_MURATA_1XK_USD) || defined(WIFI_88W8987_BOARD_MURATA_1ZM_USD) || \
     defined(WIFI_IW612_BOARD_MURATA_2EL_USD))
    /* Turn on Bluetooth module */
    GPIO_PinWrite(MURATA_WIFI_RESET_GPIO, MURATA_WIFI_RESET_GPIO_PIN, 1U);
#endif
}

#if (defined(WIFI_88W8987_BOARD_AW_CM358_USD) || defined(WIFI_88W8987_BOARD_MURATA_1ZM_USD) || \
     defined(WIFI_IW416_BOARD_MURATA_1XK_USD) || defined(WIFI_IW612_BOARD_MURATA_2EL_USD))
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
    config->enableRxRTS     = 1u;
    config->enableTxCTS     = 1u;
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    config->dma_instance     = 0U;
    config->rx_channel       = 0U;
    config->tx_channel       = 1U;
    config->dma_mux_instance = 0U;
    config->rx_request       = kDmaRequestMuxLPUART3Rx;
    config->tx_request       = kDmaRequestMuxLPUART3Tx;
#endif
    return 0;
}
#elif (defined(WIFI_IW416_BOARD_AW_AM510_USD) || defined(WIFI_IW416_BOARD_AW_AM457_USD))
int controller_hci_uart_get_configuration(controller_hci_uart_config_t *config)
{
    if (NULL == config)
    {
        return -1;
    }
    config->clockSrc        = BOARD_BT_UART_CLK_FREQ;
    config->defaultBaudrate = BOARD_BT_UART_BAUDRATE;
    config->runningBaudrate = BOARD_BT_UART_BAUDRATE;
    config->instance        = BOARD_BT_UART_INSTANCE;
    config->enableRxRTS     = 1u;
    config->enableTxCTS     = 1u;
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    config->dma_instance     = 0U;
    config->rx_channel       = 0U;
    config->tx_channel       = 1U;
    config->dma_mux_instance = 0U;
    config->rx_request       = kDmaRequestMuxLPUART3Rx;
    config->tx_request       = kDmaRequestMuxLPUART3Tx;
#endif
    return 0;
}
#elif defined(K32W061_TRANSCEIVER)
int controller_hci_uart_get_configuration(controller_hci_uart_config_t *config)
{
    if (NULL == config)
    {
        return -1;
    }
    config->clockSrc        = BOARD_BT_UART_CLK_FREQ;
    config->defaultBaudrate = 115200u;
    config->runningBaudrate = 115200u;
    config->instance        = BOARD_BT_UART_INSTANCE;
    config->enableRxRTS     = 1u;
    config->enableTxCTS     = 1u;
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    config->dma_instance     = 0U;
    config->rx_channel       = 0U;
    config->tx_channel       = 1U;
    config->dma_mux_instance = 0U;
    config->rx_request       = kDmaRequestMuxLPUART3Rx;
    config->tx_request       = kDmaRequestMuxLPUART3Tx;
#endif
    return 0;
}
#else
#endif

/*${function:end}*/

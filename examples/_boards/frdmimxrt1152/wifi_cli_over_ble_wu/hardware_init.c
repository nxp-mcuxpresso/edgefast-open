/*
 * Copyright 2022 NXP
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
#include "fsl_dmamux.h"
#if (defined(BUTTON_COUNT) && (BUTTON_COUNT > 0U))
#include "fsl_component_button.h"
#endif
#include "fsl_component_timer_manager.h"
#if (((defined(CONFIG_BT_SMP)) && (CONFIG_BT_SMP)))
#include "psa/crypto.h"
#endif /* CONFIG_BT_SMP */
#include <zephyr/drivers/bluetooth.h>
/*${header:end}*/

/*${macro:start}*/

/* DMA */
#define EXAMPLE_DMAMUX_INSTANCE (0U)
#define EXAMPLE_DMA_INSTANCE    (0U)

/*${macro:end}*/

/*${variable:start}*/

/*${variable:end}*/

/*${function:start}*/
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

void BOARD_BT_Reset(void)
{
    pcal6524_handle_t *handle = BOARD_GetPCAL6524Handle();

    /* Configure WL_RST (P0_4), BT_RST (P0_5), PDn (P0_6) as outputs */
    status_t status = PCAL6524_SetDirection(handle,
                                            (1UL << BOARD_PCAL6524_WL_RST) | (1UL << BOARD_PCAL6524_BT_RST) |
                                                (1UL << BOARD_PCAL6524_PDn),
                                            kPCAL6524_Output);

    if (status != kStatus_Success)
    {
        (void)PRINTF("PCAL6524_SetDirection failed\r\n");
        return;
    }

    /* Assert WL_RST# (set LOW = hold WiFi in reset) */
    status = PCAL6524_ClearPins(handle, (1UL << BOARD_PCAL6524_WL_RST));
    if (status != kStatus_Success)
    {
        (void)PRINTF("PCAL6524_ClearPins failed for WL_RST\r\n");
        return;
    }

    /* Assert BT_RST# (set LOW = hold BT in reset) */
    status = PCAL6524_ClearPins(handle, (1UL << BOARD_PCAL6524_BT_RST));
    if (status != kStatus_Success)
    {
        (void)PRINTF("PCAL6524_ClearPins failed for BT_RST\r\n");
        return;
    }

    /* Assert PDn (set LOW = power down module) */
    status = PCAL6524_ClearPins(handle, (1UL << BOARD_PCAL6524_PDn));
    if (status != kStatus_Success)
    {
        (void)PRINTF("PCAL6524_ClearPins failed for PDn\r\n");
        return;
    }

    SDK_DelayAtLeastUs(100000, CLOCK_GetFreq(kCLOCK_CpuClk));

    /* Power up and release resets */
    status = PCAL6524_SetPins(handle, (1UL << BOARD_PCAL6524_PDn));
    if (status != kStatus_Success)
    {
        (void)PRINTF("PCAL6524_SetPins failed for Pdn\r\n");
        return;
    }

    SDK_DelayAtLeastUs(100000, CLOCK_GetFreq(kCLOCK_CpuClk));

    /* De-assert WL_RST# (set HIGH = release WiFi from reset) */
    status = PCAL6524_SetPins(handle, (1UL << BOARD_PCAL6524_WL_RST));
    if (status != kStatus_Success)
    {
        (void)PRINTF("PCAL6524_SetPins failed for WL_RST\r\n");
        return;
    }

    SDK_DelayAtLeastUs(100000, CLOCK_GetFreq(kCLOCK_CpuClk));

    /* De-assert BT_RST# (set HIGH = release BT from reset) */
    status = PCAL6524_SetPins(handle, (1UL << BOARD_PCAL6524_BT_RST));
    if (status != kStatus_Success)
    {
        (void)PRINTF("PCAL6524_SetPins failed for BT_RST\r\n");
        return;
    }
}

void BOARD_InitHardware(void)
{
    timer_config_t timerConfig;
    timer_status_t status;


#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    DMAMUX_Type *dmaMuxBases[] = DMAMUX_BASE_PTRS;
    edma_config_t config;
    DMA_Type *dmaBases[] = DMA_BASE_PTRS;
#endif

    BOARD_ConfigMPU();
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    DbgConsole_Init(0, 0, kSerialPort_BleWu, 0);

#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    DMAMUX_Init(dmaMuxBases[EXAMPLE_DMAMUX_INSTANCE]);
    EDMA_GetDefaultConfig(&config);
    EDMA_Init(dmaBases[EXAMPLE_DMA_INSTANCE], &config);
#endif

    BOARD_BT_Reset();

    (void)memset(&timerConfig, 0, sizeof(timer_config_t));
    timerConfig.instance    = 1;
    timerConfig.srcClock_Hz = CLOCK_GetRootClockFreq(kCLOCK_Root_Bus);
    status                  = TM_Init(&timerConfig);
    assert(status == kStatus_TimerSuccess);
    (void)status;
}

#if defined(WIFI_IW612_BOARD_MURATA_2EL_M2)
int controller_hci_uart_get_configuration(controller_hci_uart_config_t *config)
{
    if (NULL == config)
    {
        return -1;
    }
    config->clockSrc        = CLOCK_GetRootClockFreq(kCLOCK_Root_Lpuart8);
    config->defaultBaudrate = 115200u;
    config->runningBaudrate = BOARD_BT_UART_BAUDRATE;
    config->instance        = 8;
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    config->dma_instance     = 0U;
    config->rx_channel       = 4U;
    config->tx_channel       = 5U;
    config->dma_mux_instance = 0U;
    config->rx_request       = kDmaRequestMuxLPUART8Rx;
    config->tx_request       = kDmaRequestMuxLPUART8Tx;
#endif
    config->enableRxRTS = 1u;
    config->enableTxCTS = 1u;
    return 0;
}
#endif

#if (defined(BUTTON_COUNT) && (BUTTON_COUNT > 0U))
button_config_t g_buttonConfig[] = {{
    .gpio =
        {
            .direction       = kHAL_GpioDirectionIn,
            .port            = 13,
            .pin             = 0,
            .pinStateDefault = 1,
        },
}};

extern BUTTON_HANDLE_ARRAY_DEFINE(s_buttonHandle, BUTTON_COUNT);

#endif /* (defined(BUTTON_COUNT) && (BUTTON_COUNT > 0U)) */
/*${function:end}*/

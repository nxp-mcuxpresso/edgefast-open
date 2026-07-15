/*
 * Copyright 2020-2023 NXP
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
#include <zephyr/drivers/bluetooth.h>
#include "fsl_power.h"
// #include "fsl_adapter_flash.h"
#if (defined(BUTTON_COUNT) && (BUTTON_COUNT > 0U))
#include "fsl_component_button.h"
#endif
#include "fsl_component_timer_manager.h"
/*${header:end}*/

/*${macro:start}*/
/*${macro:end}*/

/*${variable:start}*/

/*${variable:end}*/

/*${function:start}*/

void BOARD_InitHardware(void)
{
    timer_config_t timerConfig;
    osa_status_t status;
    /* Define the init structure for the reset pin*/
    gpio_pin_config_t reset_config = {
        kGPIO_DigitalOutput,
        1,
    };

    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    /* Init output reset pin. */
    GPIO_PortInit(GPIO, 2);
    GPIO_PinInit(GPIO, 2, 12, &reset_config);
#if defined(CONFIG_BT_IND_DNLD)
    BOARD_USDHC_SDCARD_POWER_CONTROL_INIT();
#endif

    /* Attach AUX0_PLL clock to flexspi with divider 4*/
    BOARD_SetFlexspiClock(2, 8);
    /* attach FRG0 clock to FLEXCOMM5(debug console) */
    CLOCK_SetFRGClock(BOARD_BT_UART_FRG_CLK);
    CLOCK_AttachClk(BOARD_BT_UART_CLK_ATTACH);

    (void)memset(&timerConfig, 0, sizeof(timer_config_t));
    timerConfig.instance    = 0;
    timerConfig.srcClock_Hz = CLOCK_GetFreq(kCLOCK_BusClk);
    status                  = (osa_status_t)TM_Init(&timerConfig);
    assert(status == (osa_status_t)kStatus_TimerSuccess);
    (void)status;
}
#if defined(WIFI_IW416_BOARD_MURATA_1XK_M2) || defined(WIFI_88W8987_BOARD_MURATA_1ZM_M2) || \
    defined(WIFI_IW612_BOARD_MURATA_2EL_M2) || defined(WIFI_IW610_BOARD_MURATA_2LL_M2) ||   \
    defined(WIFI_88W8987_BOARD_AW_CM358_USD) || defined(WIFI_IW416_BOARD_AW_AM510MA) ||     \
    defined(WIFI_IW416_BOARD_AW_AM510_USD) || defined(WIFI_IW416_BOARD_AW_AM457MA) ||       \
    defined(WIFI_IW416_BOARD_AW_AM457_USD) || defined(WIFI_88W8987_BOARD_AW_CM358MA)
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
#else
#endif

#if (defined(BUTTON_COUNT) && (BUTTON_COUNT > 0U))
button_config_t g_buttonConfig[] = {{
    .gpio =
        {
            .direction       = kHAL_GpioDirectionIn,
            .port            = 0,
            .pin             = 10,
            .pinStateDefault = 1,
        },
}};

extern BUTTON_HANDLE_ARRAY_DEFINE(s_buttonHandle, BUTTON_COUNT);

#endif
/*${function:end}*/

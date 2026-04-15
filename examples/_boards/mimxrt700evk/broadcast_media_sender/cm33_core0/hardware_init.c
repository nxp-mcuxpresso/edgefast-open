/*
 * Copyright 2024 NXP
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
#include "fsl_xspi.h"
#include "psa/crypto.h"
#if (((defined(CONFIG_BT_SMP)) && (CONFIG_BT_SMP)))
#include "fsl_cache.h"
#endif /* CONFIG_BT_SMP */
#include "fsl_adapter_gpio.h"

/*${header:end}*/

/*${macro:start}*/

/*${macro:end}*/

/*${variable:start}*/
GPIO_HANDLE_DEFINE(sync_signal_pin_handle);
static volatile uint32_t SyncSignal_Index = 0;

/*${variable:end}*/

/*${function:start}*/

static void sync_signal_pin_callback(void *param)
{
    SyncSignal_Index += 1;
}

static void BOARD_SyncSignal_Init(void)
{
    BOARD_InitSyncSignalPins();

    CLOCK_EnableClock(kCLOCK_Gpio0);

    hal_gpio_pin_config_t config;
    config.direction = kHAL_GpioDirectionIn;
    config.port      = 0;
    config.pin       = 7;
    config.level     = 1;
    HAL_GpioInit((hal_gpio_handle_t)sync_signal_pin_handle, &config);

    HAL_GpioInstallCallback((hal_gpio_handle_t)sync_signal_pin_handle, sync_signal_pin_callback, NULL);
}

void BOARD_SyncSignal_Start(uint32_t init_offset)
{
    SyncSignal_Index = 0;

    GPIO_PinClearInterruptFlag(GPIO0,
                               7U); /* A walk-around for fsl_adapter_gpio will triger once after trigger enabled. */
    HAL_GpioSetTriggerMode((hal_gpio_handle_t)sync_signal_pin_handle, kHAL_GpioInterruptRisingEdge);
}

void BOARD_SyncSignal_Stop(void)
{
    HAL_GpioSetTriggerMode((hal_gpio_handle_t)sync_signal_pin_handle, kHAL_GpioInterruptDisable);
}

uint32_t BOARD_SyncSignal_Count(void)
{
    return SyncSignal_Index;
}

void BOARD_Init_M2(void);
void BOARD_Init_BT_UART(void);

#if (defined(CONFIG_BT_SMP) && (CONFIG_BT_SMP > 0))
void bt_psa_crypto_init(void)
{
    /* Nothing needs to be done */
}
#endif /* CONFIG_BT_SMP */

void BOARD_InitHardware(void)
{
    BOARD_Init_M2();

    BOARD_ConfigMPU();
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
    BOARD_InitAHBSC();

    BOARD_SyncSignal_Init();

    BOARD_Init_BT_UART();
}

void BOARD_Init_M2(void)
{
    CLOCK_EnableClock(kCLOCK_Gpio3);
    CLOCK_EnableClock(kCLOCK_Gpio7);
    RESET_PeripheralReset(kGPIO3_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kGPIO7_RST_SHIFT_RSTn);
}

void BOARD_Init_BT_UART(void)
{
    CLOCK_AttachClk(BOARD_BT_UART_FCCLK_ATTACH);
    CLOCK_SetClkDiv(BOARD_BT_UART_FCCLK_DIV, 1U);

    /* Attach FC1 clock to LP_FLEXCOMM3(HCI uart). */
    CLOCK_AttachClk(BOARD_BT_UART_CLK_ATTACH);
}

#if (defined(WIFI_88W8987_BOARD_MURATA_1ZM_M2) || defined(WIFI_IW416_BOARD_MURATA_1XK_M2) || \
     defined(WIFI_IW612_BOARD_MURATA_2EL_M2))
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
    return 0;
}
#endif

/*${function:end}*/

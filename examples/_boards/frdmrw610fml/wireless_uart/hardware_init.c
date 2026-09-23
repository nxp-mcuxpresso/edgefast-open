/*
 * Copyright 2026 NXP.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*${header:start}*/
#include "clock_config.h"
#include "board.h"
#include "pin_mux.h"
#include "fsl_debug_console.h"
#include "fsl_power.h"
#if (defined(BUTTON_COUNT) && (BUTTON_COUNT > 0U))
#include "fsl_component_button.h"
#endif
#include "fsl_component_timer_manager.h"
/*${header:end}*/

/*${function:start}*/

void BOARD_InitHardware(void)
{
    timer_config_t timerConfig;
    osa_status_t status;
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
    RESET_PeripheralReset(kINPUTMUX_RST_SHIFT_RSTn);
    timerConfig.instance       = 0U;
    timerConfig.srcClock_Hz    = SystemCoreClock;
    timerConfig.clockSrcSelect = 2U;
    status                     = (osa_status_t)TM_Init(&timerConfig);
    assert(status == (osa_status_t)kStatus_TimerSuccess);
    (void)status;
}

#if (defined(BUTTON_COUNT) && (BUTTON_COUNT > 0U))
button_config_t g_buttonConfig[] = {{
    .gpio =
        {
            .direction       = kHAL_GpioDirectionIn,
            .port            = BOARD_SW2_GPIO_PORT,
            .pin             = BOARD_SW2_GPIO_PIN,
            .pinStateDefault = 1,
        },
}};

extern BUTTON_HANDLE_ARRAY_DEFINE(s_buttonHandle, BUTTON_COUNT);

#endif
/*${function:end}*/

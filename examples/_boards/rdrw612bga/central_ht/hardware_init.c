/*
 * Copyright 2020 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*${header:start}*/
#include "clock_config.h"
#include "board.h"
#include "pin_mux.h"
#include "fsl_debug_console.h"
#include "fsl_power.h"
/*${header:end}*/

/* On RD-RW612-BGA boards, the Xtal32k is present and connected by default
 * so we use it by default. If an hardware rework done on the board disconnects
 * the Xtal32k, this flag should be set to 1 to fall back to the Fro32k */
#ifndef gBoardUseFro32k_d
#define gBoardUseFro32k_d 0
#endif

/*${function:start}*/

void BOARD_InitHardware(void)
{
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
    BOARD_InitSleepPinConfig();
}

/*${function:end}*/

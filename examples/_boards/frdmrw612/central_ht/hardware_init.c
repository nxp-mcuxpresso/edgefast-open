/*
 * Copyright 2024 NXP
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

/* On FRDM-RW612 boards, the Xtal32k is present but not connected by default
 * An hardware rework is needed to connect the Xtal32k, so disable this flag with caution */
#ifndef gBoardUseFro32k_d
#define gBoardUseFro32k_d 1
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

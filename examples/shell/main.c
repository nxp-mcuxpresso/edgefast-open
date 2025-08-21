/*
 * Copyright 2021 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_debug_console.h"

#include "FreeRTOS.h"
#include "task.h"

#include "shell.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
extern void BOARD_InitHardware(void);

/*******************************************************************************
 * Variables
 ******************************************************************************/

/*******************************************************************************
 * Code
 ******************************************************************************/

int main(void)
{
    BOARD_InitHardware();

    if (xTaskCreate(shell_task, "shell_task", configMINIMAL_STACK_SIZE * 8, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS)
    {
        PRINTF("Shell task creation failed!\r\n");
        while (1)
            ;
    }

    vTaskStartScheduler();
    for (;;)
        ;
}

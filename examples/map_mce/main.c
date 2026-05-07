/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_debug_console.h"

#include "FreeRTOS.h"
#include "task.h"
#include <zephyr/bluetooth/classic/map.h>

#include "app_map_mce.h"

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

	if (xTaskCreate(app_map_mce_task, "app_map_mce_task", configMINIMAL_STACK_SIZE * 8, NULL,
			tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
		PRINTF("map task creation failed!\r\n");
		while (1)
			;
	}

	vTaskStartScheduler();
	for (;;)
		;
}

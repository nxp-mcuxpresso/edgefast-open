/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <porting.h>
#include "fsl_wdog.h"

FUNC_NORETURN void sys_reboot(int type)
{
	wdog_config_t config;

	/*
	* wdogConfig->enableWdog = true;
	* wdogConfig->workMode.enableWait = true;
	* wdogConfig->workMode.enableStop = false;
	* wdogConfig->workMode.enableDebug = false;
	* wdogConfig->enableInterrupt = false;
	* wdogConfig->enablePowerdown = false;
	* wdogConfig->resetExtension = flase;
	* wdogConfig->timeoutValue = 0xFFU;
	* wdogConfig->interruptTimeValue = 0x04u;
	*/
	WDOG_GetDefaultConfig(&config);
	config.timeoutValue = 0x0U; /* Timeout value is (0xF + 1)/2 = 8 sec. */
	WDOG_Init(WDOG1, &config);
	while (true) {
		__NOP();
	}
}

/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <porting.h>

FUNC_NORETURN void sys_reboot(int type)
{
	__NVIC_SystemReset();
	while (true) {
		__NOP();
	}
}

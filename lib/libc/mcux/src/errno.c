/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

static volatile int g_errno;

__attribute__((__nothrow__)) volatile int *__aeabi_errno_addr(void)
{
    return &g_errno;
}

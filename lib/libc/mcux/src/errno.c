/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal errno storage for bare-metal / -nostdlib builds.
 *
 * Some lib/libc headers (e.g. <errno.h>) map `errno` to
 * `*__aeabi_errno_addr()`. When linking with `-nostdlib` the toolchain
 * runtime that normally provides this symbol is not pulled in, so we
 * provide a tiny implementation.
 */

#include <stdint.h>

/*
 * Per Arm EABI, __aeabi_errno_addr returns a pointer to a writable int.
 * Newlib and other libc implementations typically provide a TLS-backed
 * version; for our bare-metal use a single global is sufficient.
 */

static volatile int g_errno;

__attribute__((__nothrow__, __const__)) volatile int *__aeabi_errno_addr(void)
{
    return &g_errno;
}

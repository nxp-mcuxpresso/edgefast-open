/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EDGEFAST_OPEN_LIB_LIBC_MCUX_INCLUDE_SYS_TYPES_H_
#define EDGEFAST_OPEN_LIB_LIBC_MCUX_INCLUDE_SYS_TYPES_H_

#include <limits.h>

#ifndef _SSIZE_T_DECLARED
typedef signed long ssize_t;
#define _SSIZE_T_DECLARED

/* ssize_t is signed long here, so SSIZE_MAX should match LONG_MAX.
 * This macro is used by some libc/posix consumers and lwIP shims.
 */
#ifndef SSIZE_MAX
#define SSIZE_MAX LONG_MAX
#endif

#endif

#endif /* EDGEFAST_OPEN_LIB_LIBC_MCUX_INCLUDE_SYS_TYPES_H_ */

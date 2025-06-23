/*
 * Copyright (c) 2023 Meta
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SHELL_BACKEND_H_
#define ZEPHYR_INCLUDE_SHELL_BACKEND_H_

#include "stdarg.h"
#include "fsl_common.h"
#include "fsl_debug_console.h"

#include <zephyr/types.h>
#include <zephyr/shell/shell.h>

/**
 * @brief Get backend.
 *
 * @param[in] idx  Pointer to the backend instance.
 *
 * @return Pointer to the backend instance.
 */
static inline const struct shell *shell_backend_get(uint32_t idx)
{
	return ctx_shell;
}

/**
 * @brief Get number of backends.
 *
 * @return Number of backends.
 */
static inline int shell_backend_count_get(void)
{
	return 1;
}

#endif /* ZEPHYR_INCLUDE_SHELL_BACKEND_H_ */

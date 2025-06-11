/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef __ZEPHYR_LOGGING_LOG_H__
#define __ZEPHYR_LOGGING_LOG_H__

#ifdef CONFIG_BT_LOGGING
#define LOG_ENABLE 1
#endif /* CONFIG_BT_LOGGING */

#include "fsl_component_log.h"

#if defined(__cplusplus)
extern "C" {
#endif /* _cplusplus */

/* Helper macros to count arguments */
#define _LOG_ARG_N(_1, _2, N, ...) N
#define _LOG_COUNT_ARGS(...) _LOG_ARG_N(__VA_ARGS__, 2, 1, 0)

/* Check if a macro is defined and has a non-empty value */
#define _LOG_IS_DEFINED(x) _LOG_IS_DEFINED1(x)
#define _LOG_IS_DEFINED1(x) _LOG_IS_DEFINED2(_LOG_LEVEL##x)
#define _LOG_IS_DEFINED2(x) _LOG_IS_DEFINED3(x none, CONFIG_BT_DEFAULT_LOG_LEVEL)
#define _LOG_IS_DEFINED3(x, val, ...) ((log_level_t)(val))

#define _LOG_LEVEL0 __LOG_LEVEL, 0,
#define _LOG_LEVEL1 __LOG_LEVEL, 1,
#define _LOG_LEVEL2 __LOG_LEVEL, 2,
#define _LOG_LEVEL3 __LOG_LEVEL, 3,
#define _LOG_LEVEL4 __LOG_LEVEL, 4,
#define _LOG_LEVEL5 __LOG_LEVEL, 5,
#define _LOG_LEVEL6 __LOG_LEVEL, 6,
#define _LOG_LEVELnone __LOG_LEVEL,

/* Helper macros for LOG_MODULE_REGISTER with different argument counts */
#define _LOG_MODULE_REGISTER_1(module) LOG_MODULE_DEFINE(module, _LOG_IS_DEFINED(LOG_LEVEL))
#define _LOG_MODULE_REGISTER_2(module, level) LOG_MODULE_DEFINE(module, _LOG_IS_DEFINED(level))

/* Main macro that selects the appropriate implementation based on argument count */
#define _LOG_MODULE_REGISTER_N(N, ...) _LOG_MODULE_REGISTER_ ## N(__VA_ARGS__)
#define _LOG_MODULE_REGISTER_DISPATCH(N, ...) _LOG_MODULE_REGISTER_N(N, __VA_ARGS__)

/**
 * @brief Register a module for logging
 *
 * This macro registers a module for logging. It can be used in two ways:
 * 1. LOG_MODULE_REGISTER(test) - Registers module 'test' with default log level
 *    - If LOG_LEVEL is defined and valid, it uses LOG_LEVEL as the log level
 *    - If LOG_LEVEL is not defined or expands to something undefined, it uses kLOG_LevelNone
 * 2. LOG_MODULE_REGISTER(test, level) - Registers module 'test' with specified log level
 *
 * @param module The name of the module
 * @param level (optional) The log level for the module
 */
#define LOG_MODULE_REGISTER(...) _LOG_MODULE_REGISTER_DISPATCH(_LOG_COUNT_ARGS(__VA_ARGS__), __VA_ARGS__)

/**
 * @brief Register a module for logging
 *
 * This macro registers a module for logging. It can be used in two ways:
 * 1. LOG_MODULE_REGISTER(test) - Registers module 'test' with default log level
 *    - If LOG_LEVEL is defined and valid, it uses LOG_LEVEL as the log level
 *    - If LOG_LEVEL is not defined or expands to something undefined, it uses kLOG_LevelNone
 * 2. LOG_MODULE_REGISTER(test, level) - Registers module 'test' with specified log level
 *
 * @param module The name of the module
 * @param level (optional) The log level for the module
 */
#define LOG_MODULE_DECLARE(...) _LOG_MODULE_REGISTER_DISPATCH(_LOG_COUNT_ARGS(__VA_ARGS__), __VA_ARGS__)

#if defined(__cplusplus)
}
#endif

#endif /* __ZEPHYR_LOGGING_LOG_H__ */

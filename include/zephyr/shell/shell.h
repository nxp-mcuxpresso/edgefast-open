/*
 * Copyright (c) 2017 Intel Corporation
 * Copyright 2021-2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __BT_SHELL_H__
#define __BT_SHELL_H__

#include "stdarg.h"
#include <zephyr/kernel.h>
#include <zephyr/toolchain.h>

#include "fsl_common.h"
#include "fsl_debug_console.h"
#include "fsl_shell.h"

#include <zephyr/shell/shell_types.h>
#include <zephyr/shell/shell_string_conv.h>

#if defined(CONFIG_BT_SETTINGS) && (CONFIG_BT_SETTINGS > 0)
#define CONFIG_SETTINGS CONFIG_BT_SETTINGS
#endif /* CONFIG_BT_SETTINGS */

struct shell {
	shell_handle_t sh;
	struct {
		struct
		{
			uint8_t syntax;
		} active_cmd;
	} ctx[1];
};

#ifndef SHELL_CMD_HELP_PRINTED
#define SHELL_CMD_HELP_PRINTED 1
#endif /* SHELL_CMD_HELP_PRINTED */

typedef int (*shell_cmd_handler_t)(const struct shell *sh, size_t argc, char *argv[]);

#define SHELL_FUNC_DEF_SHELL_CMD(index, command, cmdList, descriptor, func) \
	static shell_status_t _CONCAT(_CONCAT(shell_cmd_handler_, __LINE__), index)(shell_handle_t sh, int32_t argc, char **argv) \
	{								\
		__ASSERT(ctx_shell->sh == sh, "Invalid Shell");		\
		int err;						\
		shell_cmd_handler_t handler = (shell_cmd_handler_t)func;\
									\
		if (handler == NULL) {					\
			return kStatus_SHELL_Error;			\
		}							\
									\
		err = handler(ctx_shell, argc, argv);			\
		if (err < 0) {						\
			return kStatus_SHELL_Error;			\
		} else if (err == SHELL_CMD_HELP_PRINTED) {		\
			return kStatus_SHELL_PrintCmdHelp;		\
		} else {						\
			return kStatus_SHELL_Success;			\
		}							\
	}

#define SHELL_FUNC_DEF_SHELL_CMD_ARG(index, command, cmdList, descriptor, func, minarg, additonarg) \
	static shell_status_t _CONCAT(_CONCAT(shell_cmd_handler_, __LINE__), index)(shell_handle_t sh, int32_t argc, char **argv) \
	{								\
		__ASSERT(ctx_shell->sh == sh, "Invalid Shell");		\
		int err;						\
		shell_cmd_handler_t handler = (shell_cmd_handler_t)func;\
									\
		if (handler == NULL) {					\
			return kStatus_SHELL_Error;			\
		}							\
									\
		err = handler(ctx_shell, argc, argv);			\
		if (err < 0) {						\
			return kStatus_SHELL_Error;			\
		} else if (err == SHELL_CMD_HELP_PRINTED) {		\
			return kStatus_SHELL_PrintCmdHelp;		\
		} else {						\
			return kStatus_SHELL_Success;			\
		}							\
	}

#define SHELL_FUNC_DEF_SHELL_COND_CMD_ARG(index, cond, command, cmdList, descriptor, func, minarg, additonarg) \
	static shell_status_t _CONCAT(_CONCAT(shell_cmd_handler_, __LINE__), index)(shell_handle_t sh, int32_t argc, char **argv) \
	{									\
		__ASSERT(ctx_shell->sh == sh, "Invalid Shell");			\
		int err;							\
		shell_cmd_handler_t handler = (shell_cmd_handler_t)func;	\
										\
		if (handler == NULL) {						\
			return kStatus_SHELL_Error;				\
		}								\
										\
		if ((cond) && (ctx_shell != NULL)) {				\
			err = handler(ctx_shell, argc, argv);			\
			if (err < 0) {						\
				return kStatus_SHELL_Error;			\
			} else if (err == SHELL_CMD_HELP_PRINTED) {		\
				return kStatus_SHELL_PrintCmdHelp;		\
			} else {						\
				return kStatus_SHELL_Success;			\
			}							\
		} else {							\
			return kStatus_SHELL_Error;				\
		}								\
	}

#define __SHELL_FUNC_DEF(arg) SHELL_FUNC_DEF_##arg
#define _SHELL_FUNC_DEF(arg) __SHELL_FUNC_DEF(arg)
#define SHELL_FUNC_DEF(index, arg) _SHELL_FUNC_DEF(_CONCAT(index, arg))
#define SHELL_FUNC_DEF_2(...)

#define SHELL_CMD_DESC(command, descriptor) descriptor

#define SHELL_CMD_DEF_SHELL_CMD(index, command, cmdList, descriptor, func)			\
	SHELL_COMMAND_EXDEFINE_ANONYMOUS(command,						\
					 (struct _shell_command **)cmdList,			\
					 SHELL_CMD_DESC(command, descriptor),			\
					 _CONCAT(_CONCAT(shell_cmd_handler_, __LINE__), index),	\
					 0,							\
					 0							\
					)

#define SHELL_CMD_DEF_SHELL_CMD_ARG(index, command, cmdList, descriptor, func, minarg, additonarg)	\
	SHELL_COMMAND_EXDEFINE_ANONYMOUS(command,							\
					 (struct _shell_command **)cmdList,				\
					 SHELL_CMD_DESC(command, descriptor),				\
					 _CONCAT(_CONCAT(shell_cmd_handler_, __LINE__), index),		\
					 ((minarg == 0) ? 0 : (minarg - 1)),				\
					 ((minarg == 0) ? additonarg : (additonarg + minarg - 1))	\
					)

#define SHELL_CMD_DEF_SHELL_COND_CMD_ARG(index, cond, command, cmdList, descriptor, func, minarg, additonarg)	\
	SHELL_COMMAND_EXDEFINE_ANONYMOUS(command,							\
					 (struct _shell_command **)cmdList,				\
					 IS_ENABLED(cond) ? SHELL_CMD_DESC(command, descriptor) : NULL, \
					 _CONCAT(_CONCAT(shell_cmd_handler_, __LINE__), index),		\
					 ((minarg == 0) ? 0 : (minarg - 1)),				\
					 ((minarg == 0) ? additonarg : (additonarg + minarg - 1))	\
					)

#define __SHELL_CMD_DEF(arg) SHELL_CMD_DEF_##arg
#define _SHELL_CMD_DEF(arg) __SHELL_CMD_DEF(arg)
#define SHELL_CMD_DEF(index, arg) _SHELL_CMD_DEF(_CONCAT(index, arg))
#define SHELL_CMD_DEF_2(...) ((struct _shell_command *)NULL)

#define SHELL_SUBCMD_SET_END NULL
#define SHELL_STATIC_SUBCMD_SET_CREATE(cmds, ...)			\
	SHELL_CMD_LOOP(SHELL_FUNC_DEF, (;), __VA_ARGS__)		\
	static struct _shell_command *cmds[] = {			\
		SHELL_CMD_LOOP(SHELL_CMD_DEF, (,), __VA_ARGS__)		\
		(struct _shell_command *)NULL,				\
	};

#define SHELL_COMMAND_SET_DEFINE(command, cmdList, descriptor, callback, miniArg, addtionArg) \
    STRUCT_SECTION_ITERABLE(_shell_command, command) = {                                      \
        .pcCommand           = (#command),                                                    \
        .pcHelpString        = (char *)(descriptor),                                          \
        .pFuncCallBack       = (callback),                                                    \
        .commandList         = (cmdList),                                                     \
        .parameter.range.min = (miniArg),                                                     \
        .parameter.range.max = (addtionArg + miniArg),                                        \
        .link                = {0},                                                           \
    }

#define _SHELL_CMD_ARG_REGISTER(line, command, cmdList, descriptor, func, minarg, additonarg)	\
	static shell_status_t _CONCAT(func, line)(shell_handle_t sh, int32_t argc, char **argv)	\
	{											\
		__ASSERT(ctx_shell->sh == sh, "Invalid Shell");					\
		int err = func(ctx_shell, argc, argv);						\
		if (err < 0) {									\
			return kStatus_SHELL_Error;						\
		} else if (err == SHELL_CMD_HELP_PRINTED) {					\
			return kStatus_SHELL_PrintCmdHelp;					\
		} else {									\
			return kStatus_SHELL_Success;						\
		}										\
	}											\
	SHELL_COMMAND_SET_DEFINE(command,							\
				(struct _shell_command **)cmdList,				\
				SHELL_CMD_DESC(command, descriptor),				\
				_CONCAT(func, line),						\
				((minarg == 0) ? 0 : (minarg - 1)),				\
				((minarg == 0) ? additonarg : (additonarg + minarg - 1))	\
				)

#define SHELL_CMD_ARG_REGISTER(...) _SHELL_CMD_ARG_REGISTER(__LINE__, ##__VA_ARGS__)

#define SHELL_CMD_REGISTER(syntax, subcmd, help, handler) \
	SHELL_CMD_ARG_REGISTER(syntax, subcmd, help, handler, 0, 0)

enum shell_vt100_color {
	SHELL_VT100_COLOR_BLACK,
	SHELL_VT100_COLOR_RED,
	SHELL_VT100_COLOR_GREEN,
	SHELL_VT100_COLOR_YELLOW,
	SHELL_VT100_COLOR_BLUE,
	SHELL_VT100_COLOR_MAGENTA,
	SHELL_VT100_COLOR_CYAN,
	SHELL_VT100_COLOR_WHITE,

	SHELL_VT100_COLOR_DEFAULT,

	VT100_COLOR_END
};

/**
 * @brief Terminal default text color for shell_fprintf function.
 */
#define SHELL_NORMAL	SHELL_VT100_COLOR_DEFAULT

/**
 * @brief Green text color for shell_fprintf function.
 */
#define SHELL_INFO	SHELL_VT100_COLOR_GREEN

/**
 * @brief Cyan text color for shell_fprintf function.
 */
#define SHELL_OPTION	SHELL_VT100_COLOR_CYAN

/**
 * @brief Yellow text color for shell_fprintf function.
 */
#define SHELL_WARNING	SHELL_VT100_COLOR_YELLOW

/**
 * @brief Red text color for shell_fprintf function.
 */
#define SHELL_ERROR	SHELL_VT100_COLOR_RED

#define shell_fprintf_eol(sh, mode, fmt, ...)					\
do {										\
	(void)sh;								\
	if (mode >= VT100_COLOR_END) {						\
		PRINTF( fmt "\r\n", ##__VA_ARGS__);				\
	} else if (mode == SHELL_NORMAL) {					\
		PRINTF( "\x1B[0m" fmt "\r\n", ##__VA_ARGS__);			\
	} else {								\
		PRINTF( "\x1B[1;3%dm" fmt "\x1B[0m\r\n", mode, ##__VA_ARGS__);	\
	}									\
} while(0)

#define shell_fprintf(sh, mode, fmt, ...)					\
do {										\
	(void)sh;								\
	if (mode >= VT100_COLOR_END) {						\
		PRINTF( fmt, ##__VA_ARGS__);					\
	} else if (mode == SHELL_NORMAL) {					\
		PRINTF( "\x1B[0m" fmt, ##__VA_ARGS__);				\
	} else {								\
		PRINTF( "\x1B[1;3%dm" fmt "\x1B[0m", mode, ##__VA_ARGS__);	\
	}									\
} while(0)

#define shell_dump(sh, fmt, ...) shell_fprintf(sh, SHELL_OPTION, fmt, ##__VA_ARGS__)
#define shell_print(sh, fmt, ...) shell_fprintf_eol(sh, SHELL_NORMAL, fmt, ##__VA_ARGS__)
#define shell_info(sh, fmt, ...) shell_fprintf_eol(sh, SHELL_INFO, fmt, ##__VA_ARGS__)
#define shell_warn(sh, fmt, ...) shell_fprintf_eol(sh, SHELL_WARNING, fmt, ##__VA_ARGS__)
#define shell_error(sh, fmt, ...) shell_fprintf_eol(sh, SHELL_ERROR, fmt, ##__VA_ARGS__)
#define shell_help(...)

#define bt_shell_dump(fmt, ...) shell_fprintf(ctx_shell, SHELL_OPTION, fmt, ##__VA_ARGS__)
#define bt_shell_print(fmt, ...) shell_fprintf_eol(ctx_shell, SHELL_NORMAL, fmt, ##__VA_ARGS__)
#define bt_shell_info(fmt, ...) shell_fprintf_eol(ctx_shell, SHELL_INFO, fmt, ##__VA_ARGS__)
#define bt_shell_warn(fmt, ...) shell_fprintf_eol(ctx_shell, SHELL_WARNING, fmt, ##__VA_ARGS__)
#define bt_shell_error(fmt, ...) shell_fprintf_eol(ctx_shell, SHELL_ERROR, fmt, ##__VA_ARGS__)
#define bt_shell_help(...)

#define bt_shell_fprintf(color, fmt, ...) shell_fprintf_eol(ctx_shell, color, fmt, ##__VA_ARGS__)
#define bt_shell_fprintf_print(fmt, ...) shell_fprintf_eol(ctx_shell, SHELL_NORMAL, fmt, ##__VA_ARGS__)
#define bt_shell_fprintf_info(fmt, ...) shell_fprintf_eol(ctx_shell, SHELL_INFO, fmt, ##__VA_ARGS__)
#define bt_shell_fprintf_warn(fmt, ...) shell_fprintf_eol(ctx_shell, SHELL_WARNING, fmt, ##__VA_ARGS__)
#define bt_shell_fprintf_error(fmt, ...) shell_fprintf_eol(ctx_shell, SHELL_ERROR, fmt, ##__VA_ARGS__)

extern struct shell *ctx_shell;

static inline void shell_hexdump(const struct shell *sh, const uint8_t *data, size_t len)
{
	const uint8_t rowCount = 8;
	size_t index = 0;

	shell_dump(sh, "Index:");
	for (uint8_t i = 0; i < rowCount; i++) {
		shell_dump(sh, " %02X", i);
	}
	shell_print(sh, "");
	shell_dump(sh, "======");
	for (uint8_t i = 0; i < rowCount; i++) {
		shell_dump(sh, "===");
	}
	shell_print(sh, "");

	while (len > 0)
	{
		shell_dump(sh, "%04d :", index / rowCount);
		for (uint8_t i = 0; i < MIN(rowCount, len); i++) {
			shell_dump(sh, " %02X", ((uint8_t *)(data))[index + i]);
		}
		shell_print(sh, "");
		len -= MIN(rowCount, len);
		index += rowCount;
	}
}

static inline void bt_shell_hexdump(const uint8_t *data, size_t len)
{
    	shell_hexdump(ctx_shell, data, len);
}

#endif /* __BT_SHELL_H__ */
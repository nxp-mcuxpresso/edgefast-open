/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __EDGEFAST_LIBC_ARM_INCLUDE_STRINGS_H__
#define __EDGEFAST_LIBC_ARM_INCLUDE_STRINGS_H__

#include <string.h>

#if defined(__STRICT_ANSI__)
/**
 * @brief Separate `str` by any char in `sep` and return NULL terminated
 * sections. Consecutive `sep` chars in `str` are treated as a single
 * separator.
 *
 * @return pointer to NULL terminated string or NULL on errors.
 */
static inline char *strtok_r(char *str, const char *sep, char **state)
{
	char *start, *end;

	start = str ? str : *state;

	/* skip leading delimiters */
	while (*start && strchr(sep, *start)) {
		start++;
	}

	if (*start == '\0') {
		*state = start;
		return NULL;
	}

	/* look for token chars */
	end = start;
	while (*end && !strchr(sep, *end)) {
		end++;
	}

	if (*end != '\0') {
		*end = '\0';
		*state = end + 1;
	} else {
		*state = end;
	}

	return start;
}
#endif

#endif /* __EDGEFAST_LIBC_ARM_INCLUDE_STRINGS_H__ */

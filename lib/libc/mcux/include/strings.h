/*
 * Copyright 2025 - 2026 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef EDGEFAST_OPEN_LIB_LIBC_MCUX_INCLUDE_STRINGS_H_
#define EDGEFAST_OPEN_LIB_LIBC_MCUX_INCLUDE_STRINGS_H_

#include <string.h>

#ifndef HAVE_STRNCASECMP
static inline int strncasecmp(const char *s1, const char *s2, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		unsigned char c1 = (unsigned char)s1[i];
		unsigned char c2 = (unsigned char)s2[i];

		if (c1 >= 'A' && c1 <= 'Z') {
			c1 = (unsigned char)(c1 - 'A' + 'a');
		}
		if (c2 >= 'A' && c2 <= 'Z') {
			c2 = (unsigned char)(c2 - 'A' + 'a');
		}

		if (c1 != c2) {
			return (int)c1 - (int)c2;
		}
		if (c1 == '\0') {
			return 0;
		}
	}

	return 0;
}
#endif /* HAVE_STRNCASECMP */

#ifndef HAVE_STRCASECMP
static inline int strcasecmp(const char *s1, const char *s2)
{
	return strncasecmp(s1, s2, (size_t)-1);
}
#endif /* HAVE_STRCASECMP */

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

#endif /* EDGEFAST_OPEN_LIB_LIBC_MCUX_INCLUDE_STRINGS_H_ */

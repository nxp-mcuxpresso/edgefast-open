/*
 * Copyright 2025 NXP
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <zephyr/toolchain.h>
#include <zephyr/device.h>
#include <zephyr/sys/iterable_sections.h>
#include <stdlib.h>


long shell_strtol(const char *str, int base, int *err)
{
	long val;
	char *endptr;

	errno = 0;
	val = strtol(str, &endptr, base);
	if (errno == ERANGE) {
		*err = -ERANGE;
		return 0;
	} else if (errno || endptr == str || *endptr) {
		*err = -EINVAL;
		return 0;
	}

	return val;
}

unsigned long shell_strtoul(const char *str, int base, int *err)
{
	unsigned long val;
	char *endptr;

	if (*str == '-') {
		*err = -EINVAL;
		return 0;
	}

	errno = 0;
	val = strtoul(str, &endptr, base);
	if (errno == ERANGE) {
		*err = -ERANGE;
		return 0;
	} else if (errno || endptr == str || *endptr) {
		*err = -EINVAL;
		return 0;
	}

	return val;
}

unsigned long long shell_strtoull(const char *str, int base, int *err)
{
	unsigned long long val;
	char *endptr;

	if (*str == '-') {
		*err = -EINVAL;
		return 0;
	}

	errno = 0;
	val = strtoull(str, &endptr, base);
	if (errno == ERANGE) {
		*err = -ERANGE;
		return 0;
	} else if (errno || endptr == str || *endptr) {
		*err = -EINVAL;
		return 0;
	}

	return val;
}

bool shell_strtobool(const char *str, int base, int *err)
{
	if (!strcmp(str, "on") || !strcmp(str, "enable") || !strcmp(str, "true")) {
		return true;
	}

	if (!strcmp(str, "off") || !strcmp(str, "disable") || !strcmp(str, "false")) {
		return false;
	}

	return shell_strtoul(str, base, err);
}

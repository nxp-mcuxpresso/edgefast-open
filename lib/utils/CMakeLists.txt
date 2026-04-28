# SPDX-License-Identifier: Apache-2.0

zephyr_sources(
  dec.c
  hex.c
  )

zephyr_library_sources_ifdef(CONFIG_GETOPT getopt/getopt.c getopt/getopt_common.c getopt/getopt_common.h)
zephyr_library_sources_ifdef(CONFIG_GETOPT_LONG getopt/getopt_long.c)

zephyr_sources_ifdef(CONFIG_UTF8 utf8.c)

# SPDX-License-Identifier: Apache-2.0

zephyr_sources_ifdef(CONFIG_CRC
  crc16_sw.c
  crc8_sw.c
  )

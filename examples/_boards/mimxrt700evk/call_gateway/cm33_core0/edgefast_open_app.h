/*
 *  Copyright 2024 NXP
 *  All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "edgefast_open_config.h"

#define GENERIC_LIST_LIGHT 0

#define PRINTF_FLOAT_ENABLE    1
#define PRINTF_ADVANCED_ENABLE 1

#if defined(WIFI_IW612_BOARD_MURATA_2EL_M2)
#include "wifi_bt_module_config.h"
#include "wifi_config.h"
#else
#error The transceiver module is unsupported
#endif

#define OSA_USED 1
#if defined(WIFI_IW612_BOARD_MURATA_2EL_M2)
#undef SD_TIMING_MAX
#define SD_TIMING_MAX kSD_TimingDDR50Mode
#endif /*#define WIFI_IW612_BOARD_MURATA_2EL_M2*/

/* CIS */

/* BIS */

/* VCP */

/* MCS */

/* MPL */

/* TBS */

/* CSIP set coordinator */

/* The noise issue will occur if the BT SNOOP feature is enabled */

/* Debug Settings */

#define CONFIG_LITTLE_ENDIAN 1

#define CONFIG_WORK_QUEUE_MSG_QUEUE_COUNT 128

#define LOG_MAX_BUFF_LOG_COUNT 128

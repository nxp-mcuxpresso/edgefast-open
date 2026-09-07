/*
 *  Copyright 2026 NXP
 *  All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "edgefast_open_config.h"

#if defined(WIFI_IW612_BOARD_MURATA_2EL_M2)
#include "wifi_bt_module_config.h"
#include "wifi_config.h"
#else
#error The transceiver module is unsupported - mimxrt2660evk only supports the Embedded Artists 2EL M.2 module
#endif

#define OSA_USED 1

#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#undef SD_TIMING_MAX
#define SD_TIMING_MAX kSD_TimingDDR50Mode

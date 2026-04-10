/*
 *  Copyright 2023 - 2025 NXP
 *  All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "edgefast_open_config.h"

#if (defined(WIFI_IW416_BOARD_MURATA_1XK_M2) || defined(WIFI_88W8987_BOARD_MURATA_1ZM_M2) || \
     defined(WIFI_IW612_BOARD_MURATA_2EL_M2) || defined(WIFI_IW610_BOARD_MURATA_2LL_M2))
#include "wifi_bt_module_config.h"
#include "wifi_config.h"
#else
#error The transceiver module is unsupported
#endif

#define OSA_USED 1
/* To enable the BT IND Reset. */
/* If ID Reset is enabled, use BT UART to download controller firmware. */
#if (defined(CONFIG_BT_IND_RESET) && (CONFIG_BT_IND_RESET > 0U))
#define BOARD_BT_IND_RESET_OOB_PORT (1U)
#define BOARD_BT_IND_RESET_OOB_PIN  (18U)
#endif /*#define CONFIG_BT_IND_RESET*/

#if CONFIG_BT_HAS
#undef CONFIG_UTF8
#define CONFIG_UTF8 1
#endif

#if CONFIG_BT_MCS
#undef CONFIG_MCTL_LOCAL_PLAYER_REMOTE_CONTROL
#define CONFIG_MCTL_LOCAL_PLAYER_REMOTE_CONTROL 1
#endif

#if CONFIG_MCTL
#undef CONFIG_MCTL_LOCAL_PLAYER_CONTROL
#define CONFIG_MCTL_LOCAL_PLAYER_CONTROL 1
#undef CONFIG_MCTL_LOCAL_PLAYER_LOCAL_CONTROL
#define CONFIG_MCTL_LOCAL_PLAYER_LOCAL_CONTROL 1
#undef CONFIG_MCTL_LOCAL_PLAYER_REMOTE_CONTROL
#define CONFIG_MCTL_LOCAL_PLAYER_REMOTE_CONTROL 1
#undef CONFIG_MCTL_REMOTE_PLAYER_CONTROL
#define CONFIG_MCTL_REMOTE_PLAYER_CONTROL 1
#undef CONFIG_MCTL_REMOTE_PLAYER_CONTROL_OBJECTS
#define CONFIG_MCTL_REMOTE_PLAYER_CONTROL_OBJECTS 1
#endif

#if ((defined(CONFIG_BT_MAP_MCE) && (CONFIG_BT_MAP_MCE > 0U)) || \
     (defined(CONFIG_BT_PBAP_PCE) && (CONFIG_BT_PBAP_PCE > 0U)))
#else
#endif

/*
 *  Copyright 2020-2021, 2024  NXP
 *  All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "edgefast_open_config.h"

#if defined(WIFI_IW416_BOARD_AW_AM510_USD) || defined(WIFI_IW416_BOARD_AW_AM457_USD) ||       \
    defined(WIFI_88W8987_BOARD_AW_CM358_USD) || defined(K32W061_TRANSCEIVER) ||               \
    defined(WIFI_IW416_BOARD_MURATA_1XK_USD) || defined(WIFI_88W8987_BOARD_MURATA_1ZM_USD) || \
    defined(WIFI_IW612_BOARD_MURATA_2EL_USD)
#include "wifi_bt_module_config.h"
#include "wifi_config.h"
#else
#error The transceiver module is unsupported
#endif

#define OSA_USED 1
#if defined(WIFI_IW612_BOARD_MURATA_2EL_USD)
#undef SD_TIMING_MAX
#define SD_TIMING_MAX kSD_TimingDDR50Mode
#endif

/* Select witch beacon application to start */
#define BEACON_APP  1
#define IBEACON_APP 0
#define EDDYSTONE   0

#undef CONFIG_BT_NXP_IW610
#undef CONFIG_BT_NXP_IW416
#undef CONFIG_BT_NXP_88W8987
#undef CONFIG_BT_NXP_NW612

#if !(defined(WIFI_IW416_BOARD_AW_AM510_USD) || defined(WIFI_IW416_BOARD_AW_AM457_USD) || \
      defined(WIFI_88W8987_BOARD_AW_CM358_USD) || defined(WIFI_IW416_BOARD_MURATA_1XK_USD) || \
      defined(WIFI_88W8987_BOARD_MURATA_1ZM_USD) || defined(WIFI_IW612_BOARD_MURATA_2EL_USD))
#undef CONFIG_BT_H4_NXP_CTLR
#endif

#if defined(WIFI_IW416_BOARD_AW_AM510_USD) || defined(WIFI_IW416_BOARD_AW_AM457_USD) || \
    defined(WIFI_IW416_BOARD_MURATA_1XK_USD)
#define CONFIG_BT_NXP_IW416
#endif /* WIFI_IW416_BOARD_AW_AM510_USD || WIFI_IW416_BOARD_AW_AM457_USD ||
          WIFI_IW416_BOARD_MURATA_1XK_USD */

#if defined(WIFI_88W8987_BOARD_AW_CM358_USD) || defined(WIFI_88W8987_BOARD_MURATA_1ZM_USD)
#define CONFIG_BT_NXP_88W8987
#endif /* WIFI_88W8987_BOARD_AW_CM358_USD || WIFI_88W8987_BOARD_MURATA_1ZM_USD */

#if defined(WIFI_IW612_BOARD_MURATA_2EL_USD)
#define CONFIG_BT_NXP_NW612

#undef SD_TIMING_MAX
#define SD_TIMING_MAX kSD_TimingDDR50Mode
#endif /* WIFI_IW612_BOARD_MURATA_2EL_USD */

#if !(defined(CONFIG_BT_NXP_NW612) || defined(CONFIG_BT_NXP_IW416))
#undef CONFIG_HCI_NXP_SET_CAL_DATA
#undef CONFIG_HCI_NXP_SET_CAL_DATA_ANNEX100
#endif /* !(CONFIG_BT_NXP_NW612 || CONFIG_BT_NXP_IW416) */

#if defined(CONFIG_BT_NXP_IW610) && defined(CONFIG_BT_CLASSIC)
#error "Bluetooth Classic is unsupported by Murata 2LL"
#endif /* CONFIG_BT_NXP_IW610 && CONFIG_BT_CLASSIC */

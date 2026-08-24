/*
 * Copyright 2020-2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "edgefast_open_config.h"

#if defined(WIFI_IW416_BOARD_AW_AM510MA) || defined(WIFI_88W8987_BOARD_AW_CM358MA) || \
    defined(WIFI_IW416_BOARD_MURATA_1XK_M2) || defined(WIFI_88W8987_BOARD_MURATA_1ZM_M2) ||                           \
    defined(WIFI_IW612_BOARD_MURATA_2EL_M2) || defined(WIFI_IW610_BOARD_MURATA_2LL_M2)
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

/*******************************************************************************
 * Low power feature config
 ******************************************************************************/

/* Controller_selection
 * List of controllers supporting Low power feature,
 * WIFI_IW416_BOARD_MURATA_1XK_M2
 * WIFI_IW612_BOARD_MURATA_2EL_M2
 */
#if defined(WIFI_IW416_BOARD_MURATA_1XK_M2) || defined(WIFI_IW612_BOARD_MURATA_2EL_M2)
/* Enable/Disable low power entry on tickless idle. */
#define APP_LOWPOWER_ENABLED 1
#if defined(APP_LOWPOWER_ENABLED) && (APP_LOWPOWER_ENABLED > 0)
/* Defines the low power mode of BLE host when scanning and connecting. */
#define APP_LOW_POWER_MODE PWR_DeepSleep
/* To achieve a better low power performance, optimize MEM_1V8 power domain. */
#define APP_MEM_POWER_OPT 1
/* If low power is enabled, force tickless idle enable in FreeRTOS. */
#define configUSE_TICKLESS_IDLE 1
#endif /* APP_LOWPOWER_ENABLED */
#endif /* Controller_selection */

#undef CONFIG_BT_NXP_IW610
#undef CONFIG_BT_NXP_IW416
#undef CONFIG_BT_NXP_88W8987
#undef CONFIG_BT_NXP_NW612

#if !(defined(WIFI_IW416_BOARD_MURATA_1XK_M2) || defined(WIFI_88W8987_BOARD_MURATA_1ZM_M2) || \
      defined(WIFI_IW612_BOARD_MURATA_2EL_M2) || defined(BT_NW61X_BOARD_NXP_RD_USD) || \
      defined(WIFI_IW610_BOARD_MURATA_2LL_M2) || defined(WIFI_AW611_BOARD_UBX_JODY_W5_M2))
#undef CONFIG_BT_H4_NXP_CTLR
#endif

#if defined(WIFI_IW416_BOARD_MURATA_1XK_M2)
#define CONFIG_BT_NXP_IW416
#endif /* WIFI_IW416_BOARD_MURATA_1XK_M2 */

#if defined(WIFI_88W8987_BOARD_MURATA_1ZM_M2)
#define CONFIG_BT_NXP_88W8987
#endif /* WIFI_88W8987_BOARD_MURATA_1ZM_M2 */

#if defined(WIFI_IW612_BOARD_MURATA_2EL_M2) || defined(BT_NW61X_BOARD_NXP_RD_USD) || \
    defined(WIFI_AW611_BOARD_UBX_JODY_W5_M2)
#define CONFIG_BT_NXP_NW612

#undef SD_TIMING_MAX
#define SD_TIMING_MAX kSD_TimingDDR50Mode
#endif /* WIFI_IW612_BOARD_MURATA_2EL_M2 || BT_NW61X_BOARD_NXP_RD_USD ||
          WIFI_AW611_BOARD_UBX_JODY_W5_M2 */

#if defined(WIFI_IW610_BOARD_MURATA_2LL_M2)
#define CONFIG_BT_NXP_IW610
#endif /* WIFI_IW610_BOARD_MURATA_2LL_M2 */

#if !(defined(CONFIG_BT_NXP_NW612) || defined(CONFIG_BT_NXP_IW416))
#undef CONFIG_HCI_NXP_SET_CAL_DATA
#undef CONFIG_HCI_NXP_SET_CAL_DATA_ANNEX100
#endif /* !(CONFIG_BT_NXP_NW612 || CONFIG_BT_NXP_IW416) */

#if defined(WIFI_IW610_BOARD_MURATA_2LL_M2) && defined(CONFIG_BT_CLASSIC)
#error "Bluetooth Classic is unsupported by Murata 2LL"
#endif /* WIFI_IW610_BOARD_MURATA_2LL_M2 && CONFIG_BT_CLASSIC */

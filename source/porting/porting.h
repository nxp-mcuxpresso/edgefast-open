/*
 * Copyright 2021, 2024 NXP
 * Copyright (c) 2016, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __EDGEFAST_BT_BLE_PORTING_H__
#define __EDGEFAST_BT_BLE_PORTING_H__

#include "fsl_common.h"
#include "fsl_os_abstraction.h"

#include "fsl_debug_console.h"

#include "system_c.h"

#include "common/bt_str.h"

#include <zephyr/linker/section_tags.h>

#include <zephyr/settings/settings.h>

#if ((defined(FSL_FEATURE_L2CACHE_LINESIZE_BYTE)) && (defined(FSL_FEATURE_L1DCACHE_LINESIZE_BYTE)))
#define EDGEFAST_BT_CACHE_LINESIZE MAX(FSL_FEATURE_L2CACHE_LINESIZE_BYTE, FSL_FEATURE_L1DCACHE_LINESIZE_BYTE)
#elif (defined(FSL_FEATURE_L2CACHE_LINESIZE_BYTE))
#define EDGEFAST_BT_CACHE_LINESIZE MAX(FSL_FEATURE_L2CACHE_LINESIZE_BYTE, 0)
#elif (defined(FSL_FEATURE_L1DCACHE_LINESIZE_BYTE))
#define EDGEFAST_BT_CACHE_LINESIZE MAX(0, FSL_FEATURE_L1DCACHE_LINESIZE_BYTE)
#else
#define EDGEFAST_BT_CACHE_LINESIZE 0
#endif

#define LOG_HEXDUMP_DBG(data, length, msg)                           \
do                                                                  \
{                                                                   \
    const uint8_t rowCount = 4;                                     \
    uint32_t dataLen = (length);                                    \
    uint32_t index = 0;                                             \
                                                                    \
    LOG_DBG("%s", msg);                                              \
    LOG_DBG("Index: 0  1  2  3");                                    \
    LOG_DBG("=================");                                    \
                                                                    \
    while ((dataLen) >= rowCount)                                   \
    {                                                               \
        LOG_DBG("%04d :%02X %02X %02X %02X", index / rowCount,       \
              ((uint8_t *)(data))[index + 0],                       \
              ((uint8_t *)(data))[index + 1],                       \
              ((uint8_t *)(data))[index + 2],                       \
              ((uint8_t *)(data))[index + 3]);                      \
        (dataLen) -= rowCount;                                      \
        index += rowCount;                                          \
    }                                                               \
                                                                    \
    switch ((dataLen))                                              \
    {                                                               \
    case 3:                                                         \
      LOG_DBG("%04d :%02X %02X %02X", index / rowCount,              \
            ((uint8_t *)(data))[index + 0],                         \
            ((uint8_t *)(data))[index + 1],                         \
            ((uint8_t *)(data))[index + 2]);                        \
      break;                                                        \
    case 2:                                                         \
      LOG_DBG("%04d :%02X %02X", index / rowCount,                   \
            ((uint8_t *)(data))[index + 0],                         \
            ((uint8_t *)(data))[index + 1]);                        \
      break;                                                        \
    case 1:                                                         \
      LOG_DBG("%04d :%02X", index / rowCount,                        \
        ((uint8_t *)(data))[index + 0]);                            \
      break;                                                        \
    default:                                                        \
      /* Fix MISRA C-2012 Rule 16.4 */                              \
      break;                                                        \
    }                                                               \
} while (0);

void bt_logging_init(void);
void bt_logging_deinit(void);

size_t strnlen(const char *s, size_t maxlen);

uint32_t crc32_ieee(const uint8_t *data, size_t len);

#define k_oops() configASSERT(false)
#define k_panic() configASSERT(false)

void bt_sys_init(void);

struct arch_esf;

#if defined(__CC_ARM) || (defined(__ARMCC_VERSION))
#define _bt_conn_cb_list_start  Load$$ER_bt_conn_cb$$Base
#define _bt_conn_cb_list_end  Load$$ER_bt_conn_cb$$Limit

#define _bt_sco_conn_cb_list_start  Load$$ER_bt_sco_conn_cb$$Base
#define _bt_sco_conn_cb_list_end  Load$$ER_bt_sco_conn_cb$$Limit

#define _bt_sco_hci_cb_list_start Load$$ER_bt_sco_hci_cb$$Base
#define _bt_sco_hci_cb_list_end Load$$ER_bt_sco_hci_cb$$Limit

#define _bt_ias_cb_list_start  Load$$ER_bt_ias_cb$$Base
#define _bt_ias_cb_list_end  Load$$ER_bt_ias_cb$$Limit

#define _bt_gatt_service_static_list_start  Load$$ER_bt_gatt_service_static$$Base
#define _bt_gatt_service_static_list_end  Load$$ER_bt_gatt_service_static$$Limit

#define _bt_settings_static_list_start  Load$$ER_bt_settings_static$$Base
#define _bt_settings_static_list_end  Load$$ER_bt_settings_static$$Limit

#define _settings_handler_static_list_start  Load$$ER_settings_handler_static$$Base
#define _settings_handler_static_list_end  Load$$ER_settings_handler_static$$Limit

#define _bt_l2cap_fixed_chan_list_start  Load$$ER_bt_l2cap_fixed_chan$$Base
#define _bt_l2cap_fixed_chan_list_end  Load$$ER_bt_l2cap_fixed_chan$$Limit

#define _bt_l2cap_br_fixed_chan_list_start  Load$$ER_bt_l2cap_br_fixed_chan$$Base
#define _bt_l2cap_br_fixed_chan_list_end  Load$$ER_bt_l2cap_br_fixed_chan$$Limit

#define _net_buf_pool_list_start  Image$$RW_net_buf_pool$$Base
#define _net_buf_pool_list_end  Image$$RW_net_buf_pool$$Limit

#define _k_mem_slab_list_start  Image$$RW_k_mem_slab$$Base
#define _k_mem_slab_list_end  Image$$RW_k_mem_slab$$Limit

#define __shell_command_list_start  Image$$RW__shell_command$$Base
#define __shell_command_list_end  Image$$RW__shell_command$$Limit

#define __init_PRE_KERNEL_1_start  Load$$ER_z_init_PRE_KERNEL_1$$Base
#define __init_PRE_KERNEL_2_start  Load$$ER_z_init_PRE_KERNEL_2$$Base
#define __init_POST_KERNEL_start  Load$$ER_z_init_POST_KERNEL$$Base
#define __init_APPLICATION_start  Load$$ER_z_init_APPLICATION$$Base
#define __init_start  Load$$ER_z_init_PRE_KERNEL_1$$Base
#define __init_end  Load$$ER_z_init_APPLICATION$$Limit
#endif /* defined(__CC_ARM) || (defined(__ARMCC_VERSION)) */

#endif /* __EDGEFAST_BT_BLE_PORTING_H__ */
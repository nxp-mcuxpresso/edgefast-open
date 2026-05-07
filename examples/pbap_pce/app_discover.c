/*
 * Copyright 2020 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <porting.h>
#include <string.h>
#include <stdbool.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/slist.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include "clock_config.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "app_connect.h"

#define APP_INQUIRY_LENGTH        (10) /* 10 * 1.28 Sec */
#define APP_INQUIRY_NUM_RESPONSES (20)

static uint32_t br_discover_result_count;
static struct bt_br_discovery_result br_discovery_results[APP_INQUIRY_NUM_RESPONSES];

static void br_device_found(
    size_t index, const bt_addr_t *addr, int8_t rssi, const uint8_t cod[3], const uint8_t eir[240])
{
    char br_addr[BT_ADDR_STR_LEN];
    char name[239];
    int len = 240;

    (void)memset(name, 0, sizeof(name));

    while (len)
    {
        if (len < 2)
        {
            break;
        }

        /* Look for early termination */
        if (!eir[0])
        {
            break;
        }

        /* check if field length is correct */
        if (eir[0] > len - 1)
        {
            break;
        }

        switch (eir[1])
        {
            case BT_DATA_NAME_SHORTENED:
            case BT_DATA_NAME_COMPLETE:
                memcpy(name, &eir[2], (eir[0] - 1) > (sizeof(name) - 1) ? (sizeof(name) - 1) : (eir[0] - 1));
                break;
            default:
                break;
        }

        /* Parse next AD Structure */
        len -= (eir[0] + 1);
        eir += (eir[0] + 1);
    }

    bt_addr_to_str(addr, br_addr, sizeof(br_addr));
    PRINTF("[%d]: %s, RSSI %i %s\r\n", index + 1, br_addr, rssi, name);
}

static void br_discovery_complete(const struct bt_br_discovery_result *results, size_t count)
{
    size_t index;
    int8_t max_rssi        = -128;
    uint8_t max_rssi_index = 0;

    br_discover_result_count = count;
    PRINTF("BR/EDR discovery complete\r\n");
    for (index = 0; index < count; ++index)
    {
        br_device_found(index, &results[index].addr, results[index].rssi, results[index].cod, results[index].eir);

        if (BT_COD_MAJOR_DEVICE_CLASS(results[index].cod) & BT_COD_MAJOR_DEVICE_CLASS_PHONE)
        {
            if (br_discovery_results[index].rssi > max_rssi)
            {
                max_rssi       = br_discovery_results[index].rssi;
                max_rssi_index = index;
            }
        }
    }

    PRINTF("Connect %d\r\n", max_rssi_index + 1);
    app_connect(&results[max_rssi_index].addr.val[0]);
}

static struct bt_br_discovery_cb discovery_cb = {
    .timeout = br_discovery_complete,
};
void app_discover(void)
{
    int err;
    struct bt_br_discovery_param param;

    param.length  = APP_INQUIRY_LENGTH;
    param.limited = 0U;
    err           = bt_br_discovery_start(&param, br_discovery_results, APP_INQUIRY_NUM_RESPONSES);
    if (err != 0)
    {
        PRINTF("Failed to start discovery\r\n");
    }
    else
    {
        PRINTF("Discovery started. Please wait ...\r\n");
    }
}

void app_discover_init(void)
{
    bt_br_discovery_cb_register(&discovery_cb);
}

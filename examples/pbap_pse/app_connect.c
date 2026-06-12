/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <porting.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/classic.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/pbap.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include "clock_config.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "app_connect.h"
#include "app_pbap_pse.h"
#include "FreeRTOS.h"

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        PRINTF("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
    }
    else
    {
        struct bt_conn_info info;

        bt_conn_get_info(conn, &info);
        if (info.type != BT_CONN_TYPE_BR)
        {
            return;
        }

        if (default_conn == NULL)
        {
            default_conn = bt_conn_ref(conn);
        }

        PRINTF("Connected\n");
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    PRINTF("Disconnected, reason 0x%02x %s\n", reason, bt_hci_err_to_str(reason));

    if (default_conn != conn)
    {
        return;
    }

    if (default_conn)
    {
        bt_conn_unref(default_conn);
        default_conn = NULL;
    }
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    struct bt_conn_info info;

    bt_conn_get_info(conn, &info);

    bt_addr_to_str(info.br.dst, addr, sizeof(addr));

    PRINTF("Security changed: %s level %u, err %s(%d)\n", addr, level, bt_security_err_to_str(err), err);
}

static struct bt_conn_cb conn_callbacks = {
    .connected        = connected,
    .disconnected     = disconnected,
    .security_changed = security_changed,
};

void app_connect_init(void)
{
    bt_conn_cb_register(&conn_callbacks);
}
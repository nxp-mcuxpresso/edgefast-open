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
#include <zephyr/bluetooth/classic/map.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include "clock_config.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "app_connect.h"
#include "FreeRTOS.h"

static void connected(struct bt_conn *conn, uint8_t err)
{
	struct bt_conn_info info;

	if (err != 0) {
		PRINTF("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
	}

	bt_conn_get_info(conn, &info);
	if (info.type != BT_CONN_TYPE_BR) {
		return;
	}

	PRINTF("Connected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	PRINTF("Disconnected, reason 0x%02x %s\n", reason, bt_hci_err_to_str(reason));
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	const bt_addr_t *dst_addr = bt_conn_get_dst_br(conn);

	if (dst_addr == NULL) {
		return;
	}

	bt_addr_to_str(dst_addr, addr, sizeof(addr));

	if (err == BT_SECURITY_ERR_SUCCESS) {
		PRINTF("Security changed: %s level %u\n", addr, level);
	} else {
		PRINTF("Security failed: %s level %u err %d\n", addr, level, err);
		if (err == BT_SECURITY_ERR_PIN_OR_KEY_MISSING) {
			int ret;

			PRINTF("The peer device seems to have lost the bonding information.\n");
			PRINTF("Delete the bonding information of the peer, please try again.\n");
			ret = bt_br_unpair(dst_addr);
			if (ret) {
				PRINTF("fail to delete.\n");
			} else {
				PRINTF("success to delete.\n");
			}
		}
	}
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

void app_connect_init(void)
{
	bt_conn_cb_register(&conn_callbacks);
}

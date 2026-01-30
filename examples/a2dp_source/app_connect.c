/*
 * Copyright 2020 - 2021, 2024 - 2025 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <porting.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/slist.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/a2dp.h>
#include <zephyr/bluetooth/classic/a2dp_codec_sbc.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include "clock_config.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "app_a2dp_source.h"

static void connected(struct bt_conn *conn, uint8_t err);
static void disconnected(struct bt_conn *conn, uint8_t reason);
static void security_changed(struct bt_conn *conn, bt_security_t level,
				 enum bt_security_err err);

struct bt_conn *default_conns[CONFIG_BT_MAX_CONN];
bt_addr_t connecting_peer_addr[CONFIG_BT_MAX_CONN];

//todo
#define APP_LOCK()
#define APP_UNLOCK()

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void connected(struct bt_conn *conn, uint8_t err)
{
#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	uint8_t index = bt_conn_index(conn);
#else
	uint8_t index = 0;
#endif

	APP_LOCK();

	if (err) {
		default_conns[index] = NULL;
		APP_UNLOCK();
		PRINTF("Connection failed (err 0x%02x)\n", err);
	} else {
		struct bt_conn_info info;
		bool connect_initated = false;
		const bt_addr_t *dst_addr = bt_conn_get_dst_br(conn);

		if (dst_addr == NULL) {
			return;
		}

		default_conns[index] = conn;

#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
		if (bt_addr_eq(&connecting_peer_addr[index], dst_addr)) {
			connect_initated = true;
		}
#else
		if (bt_addr_eq(&connecting_peer_addr[0], dst_addr)) {
			connect_initated = true;
		}
#endif
		memset(&connecting_peer_addr[index], 0, sizeof(bt_addr_t));
		APP_UNLOCK();

		/* connecting is initiated from local */
		if (connect_initated) {
			/*
			 * Do an SDP Query on Successful ACL connection complete with the
			 * required device
			 */
			app_sdp_discover_a2dp_sink(conn);
		}

		bt_conn_get_info(conn, &info);
		if (info.type == BT_CONN_TYPE_LE) {
			return;
		}

		PRINTF("Connected %s\n", info.role == BT_HCI_ROLE_CENTRAL ? "central" : "peripheral");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	uint8_t index = bt_conn_index(conn);
#else
	uint8_t index = 0;
#endif

	printk("Disconnected (reason 0x%02x)\n", reason);
	APP_LOCK();
	memset(&connecting_peer_addr[index], 0, sizeof(bt_addr_t));
	default_conns[index] = NULL;
	APP_UNLOCK();
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
				 enum bt_security_err err)
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

void app_connect(uint8_t *addr)
{
	struct bt_conn *conn;
	uint8_t addr_idx = 0xFFu;

	APP_LOCK();
#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	for (uint8_t idx = 0; idx < CONFIG_BT_MAX_CONN; ++idx) {
		if (default_conns[idx] == NULL) {
			memcpy(&connecting_peer_addr[idx], addr, 6U);
			addr_idx = idx;
			break;
		}
	}
	
	if (addr_idx == 0xFFu) {
		APP_UNLOCK();
		PRINTF("no connection idle\r\n");
		return;
	}
#else
	memcpy(&connecting_peer_addr[0], addr, 6U);
	addr_idx = 0;
#endif

	default_conns[addr_idx] = NULL;
	conn = bt_conn_create_br(&connecting_peer_addr[addr_idx], BT_BR_CONN_PARAM_DEFAULT);
	if (conn == NULL) {
		memset(&connecting_peer_addr[addr_idx], 0, sizeof(bt_addr_t));
		APP_UNLOCK();
		PRINTF("Connection failed\r\n");
	} else {
		default_conns[addr_idx] = conn;
		APP_UNLOCK();
		/* unref connection obj in advance as app user */
		bt_conn_unref(conn);
		PRINTF("Connection pending\r\n");
	}
}

void app_disconnect(void)
{
#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	struct bt_conn *conn = NULL;

	for (uint8_t i = 0; i < CONFIG_BT_MAX_CONN; i++) {
		if (default_conns[i] != NULL) {
			conn = default_conns[i];
			break;
		}
	}
#else
	struct bt_conn *conn = default_conns[0];
#endif

	if (conn != NULL) {
		if (bt_conn_disconnect(conn, 0x13U)) {
			printk("Disconnection failed\r\n");
		}
	}
}

void app_connect_init(void)
{
	bt_conn_cb_register(&conn_callbacks);
}

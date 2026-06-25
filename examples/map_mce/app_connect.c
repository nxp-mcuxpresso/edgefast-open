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
#include "app_map_mce.h"
#include "FreeRTOS.h"

extern struct mce_mas_instance mas_inst;
extern struct k_work sm_work;

NET_BUF_POOL_DEFINE(sdp_discover_pool, CONFIG_BT_MAX_CONN,
		    BT_L2CAP_BUF_SIZE(CONFIG_BT_L2CAP_TX_MTU), CONFIG_BT_CONN_TX_USER_DATA_SIZE,
		    NULL);

void app_connect(const uint8_t *addr)
{
	bt_addr_t default_peer_addr;

	memcpy(&default_peer_addr, addr, 6U);
	mas_inst.conn = bt_conn_create_br(&default_peer_addr, BT_BR_CONN_PARAM_DEFAULT);
	if (mas_inst.conn == NULL) {
		PRINTF("Connection failed\r\n");
	} else {
		/* unref connection obj in advance as app user */
		bt_conn_unref(mas_inst.conn);
		PRINTF("Connection pending\r\n");
	}
}

static int map_sdp_get_goep_l2cap_psm(const struct net_buf *buf, uint16_t *psm)
{
	int err;
	struct bt_sdp_attribute attr;
	struct bt_sdp_attr_value value;

	err = bt_sdp_get_attr(buf, BT_SDP_ATTR_GOEP_L2CAP_PSM, &attr);
	if (err != 0) {
		return err;
	}

	err = bt_sdp_attr_read(&attr, NULL, &value);
	if (err != 0) {
		return err;
	}

	if ((value.type != BT_SDP_ATTR_VALUE_TYPE_UINT) || (value.uint.size != sizeof(*psm))) {
		return -EINVAL;
	}

	*psm = value.uint.u16;
	return 0;
}

static int map_sdp_get_features(const struct net_buf *buf, uint32_t *feature)
{
	int err;
	struct bt_sdp_attribute attr;
	struct bt_sdp_attr_value value;

	err = bt_sdp_get_attr(buf, BT_SDP_ATTR_MAP_SUPPORTED_FEATURES, &attr);
	if (err != 0) {
		return err;
	}

	err = bt_sdp_attr_read(&attr, NULL, &value);
	if (err != 0) {
		return err;
	}

	if ((value.type != BT_SDP_ATTR_VALUE_TYPE_UINT) || (value.uint.size != sizeof(*feature))) {
		return -EINVAL;
	}

	*feature = value.uint.u32;
	return 0;
}

static int map_sdp_get_instance_id(const struct net_buf *buf, uint8_t *id)
{
	int err;
	struct bt_sdp_attribute attr;
	struct bt_sdp_attr_value value;

	err = bt_sdp_get_attr(buf, BT_SDP_ATTR_MAS_INSTANCE_ID, &attr);
	if (err != 0) {
		return err;
	}

	err = bt_sdp_attr_read(&attr, NULL, &value);
	if (err != 0) {
		return err;
	}

	if ((value.type != BT_SDP_ATTR_VALUE_TYPE_UINT) || (value.uint.size != sizeof(*id))) {
		return -EINVAL;
	}

	*id = value.uint.u8;
	return 0;
}

static int map_sdp_get_msg_type(const struct net_buf *buf, uint8_t *msg_type)
{
	int err;
	struct bt_sdp_attribute attr;
	struct bt_sdp_attr_value value;

	err = bt_sdp_get_attr(buf, BT_SDP_ATTR_SUPPORTED_MESSAGE_TYPES, &attr);
	if (err != 0) {
		return err;
	}

	err = bt_sdp_attr_read(&attr, NULL, &value);
	if (err != 0) {
		return err;
	}

	if ((value.type != BT_SDP_ATTR_VALUE_TYPE_UINT) || (value.uint.size != sizeof(*msg_type))) {
		return -EINVAL;
	}

	*msg_type = value.uint.u8;
	return 0;
}

static int map_sdp_get_service_name(const struct net_buf *buf, char *name, size_t name_len)
{
	int err;
	struct bt_sdp_attribute attr;
	struct bt_sdp_attr_value value;
	size_t copy_len;

	err = bt_sdp_get_attr(buf, BT_SDP_ATTR_SVCNAME_PRIMARY, &attr);
	if (err != 0) {
		return err;
	}

	err = bt_sdp_attr_read(&attr, NULL, &value);
	if (err != 0) {
		return err;
	}

	if (value.type != BT_SDP_ATTR_VALUE_TYPE_TEXT) {
		return -EINVAL;
	}

	copy_len = MIN(value.text.len, name_len - 1);
	memcpy(name, value.text.text, copy_len);
	name[copy_len] = '\0';

	return 0;
}

static uint8_t sdp_discover_cb(struct bt_conn *conn, struct bt_sdp_client_result *result,
			       const struct bt_sdp_discover_params *params)
{
	int err;
	uint32_t features = 0;
	uint16_t version = 0;
	uint16_t rfcomm_channel = 0;
	uint16_t l2cap_psm = 0;
	uint8_t instance_id = 0;
	uint8_t msg_type = 0;
	char service_name[32] = {0};

	PRINTF("Discover done\n");

	if (result == NULL || result->resp_buf == NULL) {
		return BT_SDP_DISCOVER_UUID_CONTINUE;
	}

	err = bt_sdp_get_proto_param(result->resp_buf, BT_SDP_PROTO_RFCOMM, &rfcomm_channel);
	if (err != 0) {
		PRINTF("Failed to get RFCOMM channel: %d\n", err);
	} else {
		PRINTF("Found RFCOMM channel %u\n", rfcomm_channel);
	}

	err = map_sdp_get_goep_l2cap_psm(result->resp_buf, &l2cap_psm);
	if (err != 0) {
		PRINTF("Failed to get GOEP L2CAP PSM: %d\n", err);
	} else {
		PRINTF("Found GOEP L2CAP PSM 0x%04x\n", l2cap_psm);
	}

	err = bt_sdp_get_profile_version(result->resp_buf, BT_SDP_MAP_SVCLASS, &version);
	if (err != 0) {
		PRINTF("Failed to get MAP profile version: %d\n", err);
	} else {
		PRINTF("Found MAP profile version 0x%04x\n", version);
	}

	err = map_sdp_get_features(result->resp_buf, &features);
	if (err != 0) {
		features = BT_MAP_MANDATORY_SUPPORTED_FEATURES;
		PRINTF("Failed to get MAP features: %d\n", err);
	} else {
		PRINTF("Found MAP features 0x%08x\n", features);
	}

	err = map_sdp_get_service_name(result->resp_buf, service_name, sizeof(service_name));
	if (err != 0) {
		PRINTF("Failed to get service name: %d\n", err);
	} else {
		PRINTF("Found service name: %s\n", service_name);
	}

	err = map_sdp_get_instance_id(result->resp_buf, &instance_id);
	if (err != 0) {
		PRINTF("Failed to get MAP instance ID: %d\n", err);
	} else {
		PRINTF("Found MAP instance ID %u\n", instance_id);
	}

	err = map_sdp_get_msg_type(result->resp_buf, &msg_type);
	if (err != 0) {
		PRINTF("Failed to get MAP MSG type: %d\n", err);
	} else {
		PRINTF("Found MAP MSG type 0x%02x\n", msg_type);
	}

	mas_inst.instance_id = instance_id;
	mas_inst.psm = l2cap_psm;
	mas_inst.channel = rfcomm_channel;
	mas_inst.supported_features = features;
	mas_inst.state = APP_STATE_IDLE;
	(void)k_work_submit(&sm_work);

	return BT_SDP_DISCOVER_UUID_STOP;
}

static struct bt_sdp_discover_params sdp_discover = {
	.type = BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
	.func = sdp_discover_cb,
	.pool = &sdp_discover_pool,
	.uuid = BT_UUID_DECLARE_16(BT_SDP_MAP_MSE_SVCLASS),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	struct bt_conn_info info;

	if (err != 0) {
		PRINTF("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
	}

	if (mas_inst.conn != conn) {
		return;
	}

	bt_conn_get_info(conn, &info);
	if (info.type != BT_CONN_TYPE_BR) {
		return;
	}

	PRINTF("Connected\n");

	err = bt_sdp_discover(mas_inst.conn, &sdp_discover);
	if (err != 0) {
		PRINTF("SDP discovery failed (err %d)\r\n", err);
	} else {
		PRINTF("SDP discovery started\r\n");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	PRINTF("Disconnected, reason 0x%02x %s\n", reason, bt_hci_err_to_str(reason));

	if (mas_inst.conn == conn) {
		mas_inst.conn = NULL;
	}
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

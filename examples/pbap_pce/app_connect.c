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
#include "app_pbap_pce.h"
#include "FreeRTOS.h"

extern struct bt_pbap_pce_cb pce_cb;
extern struct app_pbap_pce app_pce;
struct bt_conn *default_conn;
static bt_addr_t default_peer_addr;

NET_BUF_POOL_DEFINE(
    sdp_discover_pool, 10, BT_L2CAP_BUF_SIZE(CONFIG_BT_L2CAP_TX_MTU), CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

void app_connect(const uint8_t *addr)
{
    memcpy(&default_peer_addr, addr, 6U);
    default_conn = bt_conn_create_br(&default_peer_addr, BT_BR_CONN_PARAM_DEFAULT);
    if (!default_conn)
    {
        PRINTF("Connection failed\r\n");
    }
    else
    {
        /* unref connection obj in advance as app user */
        bt_conn_unref(default_conn);
        PRINTF("Connection pending\r\n");
    }
}

static int pbap_sdp_get_goep_l2cap_psm(const struct net_buf *buf, uint16_t *psm)
{
    int err;
    struct bt_sdp_attribute attr;
    struct bt_sdp_attr_value value;

    err = bt_sdp_get_attr(buf, BT_SDP_ATTR_GOEP_L2CAP_PSM, &attr);
    if (err != 0)
    {
        return err;
    }

    err = bt_sdp_attr_read(&attr, NULL, &value);
    if (err != 0)
    {
        return err;
    }

    if ((value.type != BT_SDP_ATTR_VALUE_TYPE_UINT) || (value.uint.size != sizeof(*psm)))
    {
        return -EINVAL;
    }

    *psm = value.uint.u16;
    return 0;
}

static int pbap_sdp_get_features(const struct net_buf *buf, uint32_t *feature)
{
	int err;
	struct bt_sdp_attribute attr;
	struct bt_sdp_attr_value value;

	err = bt_sdp_get_attr(buf, BT_SDP_ATTR_PBAP_SUPPORTED_FEATURES, &attr);
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

static uint8_t sdp_discover_cb(struct bt_conn *conn,
                               struct bt_sdp_client_result *result,
                               const struct bt_sdp_discover_params *params)
{
    int err;
    uint16_t l2cap_psm;
    uint16_t rfcomm_channel;
    uint32_t feature;

    PRINTF("Discover done\n");

    if (result->resp_buf != NULL)
    {
        l2cap_psm      = 0;
        rfcomm_channel = 0;

        err = pbap_sdp_get_goep_l2cap_psm(result->resp_buf, &l2cap_psm);
        if (err != 0)
        {
            PRINTF("PSE l2cap PSM not found, err %d\n", err);
        }
        PRINTF("Found L2CAP PSM: %d\n", l2cap_psm);

        err = bt_sdp_get_proto_param(result->resp_buf, BT_SDP_PROTO_RFCOMM, &rfcomm_channel);
        if (err != 0)
        {
            PRINTF("PSE Rfcomm channel not found, err %d\n", err);
            return BT_SDP_DISCOVER_UUID_CONTINUE;
        }
        PRINTF("Found Rfcomm channel: %d\n", rfcomm_channel);

        err = pbap_sdp_get_features(result->resp_buf, &feature);
        if (err != 0) {
            PRINTF("PSE feature not found, err %d\n", err);
        } else {
            PRINTF("PSE feature param 0x%08x\n", feature);
        }

        if (l2cap_psm != 0)
        {
            PRINTF("Creating transport connection via L2CAP PSM: %d\n", l2cap_psm);
            err = bt_pbap_pce_l2cap_connect(default_conn, &app_pce.pbap_pce, &pce_cb, l2cap_psm);
            if (err != 0)
            {
                PRINTF("Failed to connect PBAP PCE over L2CAP, err %d\n", err);
            }
        }
        else
        {
            PRINTF("Creating transport connection via RFCOMM channel: %d\n", rfcomm_channel);
            err = bt_pbap_pce_rfcomm_connect(default_conn, &app_pce.pbap_pce, &pce_cb, rfcomm_channel);
            if (err != 0)
            {
                PRINTF("Failed to connect PBAP PCE over RFCOMM, err %d\n", err);
            }
        }
    }

    return BT_SDP_DISCOVER_UUID_CONTINUE;
}

static struct bt_sdp_discover_params sdp_discover = {
    .type = BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
    .func = sdp_discover_cb,
    .pool = &sdp_discover_pool,
    .uuid = BT_UUID_DECLARE_16(BT_SDP_PBAP_PSE_SVCLASS),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
    struct bt_conn_info info;

    if (err != 0)
    {
        PRINTF("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
    }

    bt_conn_get_info(conn, &info);
    if (info.type != BT_CONN_TYPE_BR)
    {
        return;
    }

    PRINTF("ACL Connected\n");
    default_conn = conn;

    err = bt_sdp_discover(default_conn, &sdp_discover);
    if (err != 0)
    {
        PRINTF("SDP discovery failed (err %d)\r\n", err);
    }
    else
    {
        PRINTF("SDP discovery started\r\n");
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
        default_conn = NULL;
    }
    else
    {
        return;
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

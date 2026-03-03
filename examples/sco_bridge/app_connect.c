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
#include <zephyr/bluetooth/classic/hfp_ag.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/logging/log.h>
#include "clock_config.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "app_sco_bridge.h"

LOG_MODULE_REGISTER(app_connect);

static void connected(struct bt_conn *conn, uint8_t err);
static void disconnected(struct bt_conn *conn, uint8_t reason);
static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err);

bt_addr_t default_peer_addr;
static uint8_t default_connect_initialized;
struct bt_conn *default_conns[CONFIG_BT_MAX_CONN];

static struct bt_conn_cb conn_callbacks = {
    .connected        = connected,
    .disconnected     = disconnected,
    .security_changed = security_changed,
};

NET_BUF_POOL_DEFINE(sdp_discover_pool, 10, BT_L2CAP_BUF_SIZE(CONFIG_BT_L2CAP_TX_MTU),
		    CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static uint8_t sdp_discover_cb(struct bt_conn *conn, struct bt_sdp_client_result *result,
			       const struct bt_sdp_discover_params *params)
{
	int err;
	uint16_t value;
        size_t index;

        index = (size_t)bt_conn_index(conn);
        __ASSERT(index < ARRAY_SIZE(g_HfpAgs), "Conn index is out of bounds");

	printk("Discover done\n");

	if (result->resp_buf != NULL) {
		err = bt_sdp_get_proto_param(result->resp_buf, BT_SDP_PROTO_RFCOMM, &value);

		if (err != 0) {
			printk("Fail to parser RFCOMM the SDP response!\n");
		} else {
			printk("The server channel is %d\n", value);
			err = bt_hfp_ag_connect(conn, &g_HfpAgs[index].hfp_agHandle, value);
			if (err != 0) {
				printk("Fail to create hfp AG connection (err %d)\n", err);
			}
		}
	}

	return BT_SDP_DISCOVER_UUID_STOP;
}

static struct bt_sdp_discover_params sdp_discover = {
	.type = BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
	.func = sdp_discover_cb,
	.pool = &sdp_discover_pool,
	.uuid = BT_UUID_DECLARE_16(BT_SDP_HANDSFREE_SVCLASS),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
    int res;
    char addr[BT_ADDR_STR_LEN];

    if (err || (conn == NULL))
    {
        if (1U == default_connect_initialized)
        {
            bt_addr_to_str(&default_peer_addr, addr, sizeof(addr));
            PRINTF("ACL connection to %s failed (err 0x%02x)\n", addr, err);
            LOG_ERR("ACL connection to %s failed (err 0x%02x)", addr, err);

            switch (err) {
            case 0x04:
                PRINTF("Error: Page Timeout - Device may be:\n");
                PRINTF("  - Out of range\n");
                PRINTF("  - Powered off\n");
                PRINTF("  - Not in discoverable/connectable mode\n");
                PRINTF("  - Address incorrect from discovery\n");
                break;
            case 0x08:
                PRINTF("Error: Connection Timeout\n");
                break;
            case 0x0D:
                PRINTF("Error: Connection Terminated by Remote Host\n");
                break;
            case 0x10:
                PRINTF("Error: Connection Accept Timeout Exceeded\n");
                break;
            default:
                PRINTF("Error: Unknown connection failure\n");
                break;
            }
            default_connect_initialized = 0U;
        }
        else
        {
            PRINTF("acl connection failed (err 0x%02x)\n", err);
        }
    }
    else
    {
        default_conns[bt_conn_index(conn)] = conn;
        if (1U == default_connect_initialized)
        {
            struct bt_conn_info info;

            default_connect_initialized = 0U;
            bt_conn_get_info(conn, &info);
            if (info.type == BT_CONN_TYPE_LE)
            {
                return;
            }

            /*
             * Do an SDP Query on Successful ACL connection complete with the
             * required device
             */
            if (0 == memcmp(info.br.dst, &default_peer_addr, 6U))
            {
                  /*
                   * Do an SDP Query on Successful ACL connection complete with the
                   * required device
                   */
                  res = bt_sdp_discover(conn, &sdp_discover);
                  if (res) {
                          printk("SDP discovery failed (err %d)\r\n", res);
                  } else {
                          printk("SDP discovery started\r\n");
                  }
            }
        }
        PRINTF("ACL Connected (index:%d)\n", bt_conn_index(conn));
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    if (conn)
    {
        default_conns[bt_conn_index(conn)] = NULL;
    }
    PRINTF("acl disconnected (reason 0x%02x)\n", reason);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_to_str(bt_conn_get_dst_br(conn), addr, sizeof(addr));

    if (!err)
    {
        PRINTF("Security changed: %s level %u\n", addr, level);
    }
    else
    {
        PRINTF("Security failed: %s level %u err %d\n", addr, level, err);

        switch (err) {
        case BT_SECURITY_ERR_PIN_OR_KEY_MISSING:
            {
                bt_addr_le_t le_addr;
                struct bt_conn_info info;
                int ret;

                bt_conn_get_info(conn, &info);
                if (info.type == BT_CONN_TYPE_LE)
                {
                    return;
                }

                PRINTF("The peer device seems to have lost the bonding information.\n");
                PRINTF("Attempting to delete the bonding information...\n");
                le_addr.type = BT_ADDR_LE_PUBLIC;
                le_addr.a = *info.br.dst;
                ret = bt_unpair(BT_ID_DEFAULT, &le_addr);
                if (ret)
                {
                    PRINTF("Failed to delete bonding info (err %d).\n", ret);
                    PRINTF("Please manually delete pairing on both devices and try again.\n");
                }
                else
                {
                    PRINTF("Successfully deleted bonding info.\n");
                    PRINTF("Please also delete pairing on remote device, then try connecting again.\n");
                }
            }
            break;

        case BT_SECURITY_ERR_AUTH_FAIL:
            PRINTF("Authentication Failed - check PIN/passkey entry\n");
            break;

        case BT_SECURITY_ERR_PAIR_NOT_SUPPORTED:
            PRINTF("Pairing not supported by remote device\n");
            break;

        case BT_SECURITY_ERR_AUTH_REQUIREMENT:
            PRINTF("Authentication requirements not met\n");
            break;

        default:
            PRINTF("Security error code: %d\n", err);
            break;
        }
    }
}

void app_connect(uint8_t *addr)
{
    struct bt_conn *conn;
    char addr_str[BT_ADDR_STR_LEN];
    int err;

    /* Register HFP HF profile if not already registered (needed to connect to AG) */
    err = app_hfp_ag_ensure_registered();
    if (err)
    {
        PRINTF("Failed to register HFP HF profile (err %d)\n", err);
        LOG_ERR("HFP HF registration failed with error %d", err);
        return;
    }

    default_connect_initialized = 1U;
    memcpy(&default_peer_addr, addr, 6U);

    bt_addr_to_str((bt_addr_t *)addr, addr_str, sizeof(addr_str));
    PRINTF("Initiating connection to %s...\n", addr_str);
    LOG_INF("Attempting BR/EDR connection to %s", addr_str);

    conn = bt_conn_create_br(&default_peer_addr, BT_BR_CONN_PARAM_DEFAULT);
    if (!conn)
    {
        default_connect_initialized = 0U;
        PRINTF("Failed to create connection object (out of memory or resources)\r\n");
        LOG_ERR("bt_conn_create_br failed for %s", addr_str);
    }
    else
    {
        PRINTF("Connection object created, waiting for HCI response...\n");
        /* unref connection obj in advance as app user */
        bt_conn_unref(conn);
    }
}

void app_disconnect(uint8_t index)
{
    if (default_conns[index])
    {
        if (bt_conn_disconnect(default_conns[index], 0x13U))
        {
            PRINTF("Disconnection failed\r\n");
        }
    }
    else
    {
        PRINTF("no connection to disconnect\r\n");
    }
}

void app_delete(void)
{
    int err;
    uint8_t i;

    PRINTF("Deleting all bonded devices...\n");

    /* First, disconnect all active connections */
    for (i = 0; i < CONFIG_BT_MAX_CONN; i++)
    {
        if (default_conns[i])
        {
            PRINTF("Disconnecting connection index %d...\n", i);
            err = bt_conn_disconnect(default_conns[i], BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            if (err)
            {
                PRINTF("Failed to disconnect index %d (err %d)\n", i, err);
            }
        }
    }

    /* Delete all bonding information */
    err = bt_unpair(BT_ID_DEFAULT, NULL);
    if (err)
    {
        PRINTF("Failed to delete all bonding information (err %d)\n", err);
    }
    else
    {
        PRINTF("Successfully deleted all bonding information\n");
        PRINTF("Please also delete pairing on remote devices\n");
    }
}

void app_connect_init(void)
{
    bt_conn_cb_register(&conn_callbacks);
}

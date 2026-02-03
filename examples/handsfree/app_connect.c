/*
 * Copyright 2020, 2024 NXP
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
#include <zephyr/bluetooth/classic/hfp_hf.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include "clock_config.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "app_handsfree.h"
#include "app_connect.h"
#include "FreeRTOS.h"
#if !((defined AUTO_CONNECT_USE_BOND_INFO) && (AUTO_CONNECT_USE_BOND_INFO))
#include "lfs.h"
#include "littlefs_pl.h"
static int app_auto_connect_del_addr(bt_addr_t const *addr);
#endif

NET_BUF_POOL_DEFINE(sdp_discover_pool, 10, BT_L2CAP_BUF_SIZE(CONFIG_BT_L2CAP_TX_MTU),
		    CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static void connected(struct bt_conn *conn, uint8_t err);
static void disconnected(struct bt_conn *conn, uint8_t reason);
static void security_changed(struct bt_conn *conn, bt_security_t level,
				 enum bt_security_err err);
struct bt_conn *default_conn;

static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
};

static uint8_t default_connect_initiated;
static uint8_t connectable_set;
static bt_addr_t auto_connect_device;
#if !((defined AUTO_CONNECT_USE_BOND_INFO) && (AUTO_CONNECT_USE_BOND_INFO))
static lfs_t * lfs;
static lfs_file_t lfs_file;
#define FILE_NAME "peer_addr"
#endif

void app_hf_set_connectable(void)
{
    int err;

    if (connectable_set) {
        return;
    }

    connectable_set = 1U;

    err = bt_br_set_connectable(true, NULL);
    if (err) {
        PRINTF("BR/EDR set/rest connectable failed (err %d)\n", err);
        return;
    }
    err = bt_br_set_discoverable(true, false);
    if (err) {
        PRINTF("BR/EDR set discoverable failed (err %d)\n", err);
        return;
    }
    PRINTF("BR/EDR set connectable and discoverable done\n");
    PRINTF("Wait for connection\r\n");
}

static uint8_t sdp_discover_cb(struct bt_conn *conn, struct bt_sdp_client_result *result,
			       const struct bt_sdp_discover_params *params)
{
	int err;
	uint16_t value;

	printk("Discover done\n");

	if (result->resp_buf != NULL) {
		err = bt_sdp_get_proto_param(result->resp_buf, BT_SDP_PROTO_RFCOMM, &value);

		if (err != 0) {
			printk("Fail to parser RFCOMM the SDP response!\n");
		} else {
			printk("The server channel is %d\n", value);
			err = app_hfp_hf_connect(conn, value);
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
    if (err != 0) {
        PRINTF("Connection failed (err 0x%02x)\n", err);
        default_conn = NULL;
        if (default_connect_initiated) {
            default_connect_initiated = 0U;
            app_hf_set_connectable();
        }
    } else {
        PRINTF("Connected\n");
        default_conn = conn;
        if (default_connect_initiated) {
            struct bt_conn_info info;

            default_connect_initiated = 0U;
            bt_conn_get_info(conn, &info);
            if (info.type == BT_CONN_TYPE_LE) {
                return;
            }

            if (0U == memcmp(info.br.dst, &auto_connect_device, 6U)) {
                int res;

			/*
			 * Do an SDP Query on Successful ACL connection complete with the
			 * required device
			 */
			res = bt_sdp_discover(default_conn, &sdp_discover);
			if (res) {
				printk("SDP discovery failed (err %d)\r\n", res);
			} else {
				printk("SDP discovery started\r\n");
			}
            }
        }
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    PRINTF("Disconnected (reason 0x%02x)\n", reason);
    default_conn = NULL;
    app_hf_set_connectable();
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
				 enum bt_security_err err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_to_str(bt_conn_get_dst_br(conn), addr, sizeof(addr));

    if (err == BT_SECURITY_ERR_SUCCESS) {
        PRINTF("Security changed: %s level %u\n", addr, level);
    } else {
        PRINTF("Security failed: %s level %u err %d\n", addr, level, err);
        if (err == BT_SECURITY_ERR_PIN_OR_KEY_MISSING) {
            bt_addr_le_t addr;
            struct bt_conn_info info;
            int ret;
#if !((defined AUTO_CONNECT_USE_BOND_INFO) && (AUTO_CONNECT_USE_BOND_INFO))
            int ret2;
#endif

            bt_conn_get_info(conn, &info);
            if (info.type == BT_CONN_TYPE_LE) {
                return;
            }

            PRINTF("The peer device seems to have lost the bonding information.\n");
            PRINTF("Delete the bonding information of the peer, please try again.\n");
            addr.type = BT_ADDR_LE_PUBLIC;
            addr.a = *info.br.dst;
            ret = bt_unpair(BT_ID_DEFAULT, &addr);
#if !((defined AUTO_CONNECT_USE_BOND_INFO) && (AUTO_CONNECT_USE_BOND_INFO))
            ret2 = app_auto_connect_del_addr(&addr.a);
            if (ret || ret2) {
#else
            if (ret) {
#endif
                PRINTF("fail to delete.\n");
            } else {
                PRINTF("success to delete.\n");
            }
        }
    }
}

void app_connect_init(void)
{
    bt_conn_cb_register(&conn_callbacks);
}

#if !((defined AUTO_CONNECT_USE_BOND_INFO) && (AUTO_CONNECT_USE_BOND_INFO))
static int app_auto_connect_get_addr()
{
    int err;
    int len;

    err = lfs_file_open(lfs, &lfs_file, FILE_NAME, LFS_O_RDONLY);

    if (err) {
        return err;
    }

    len = lfs_file_read(lfs, &lfs_file, (char *)&auto_connect_device.val[0], 6U);
    if (len != 6U) {
        err = -EIO;
    } else {
        err = 0;
    }
    lfs_file_close(lfs, &lfs_file);

    return err;
}

static int app_auto_connect_del_addr(bt_addr_t const *addr)
{
    int err = lfs_remove(lfs, FILE_NAME);

    return err;
}

int app_auto_connect_save_addr(bt_addr_t const *addr)
{
    int err;
    int len;

    lfs_remove(lfs, FILE_NAME);
    err = lfs_file_open(lfs, &lfs_file, FILE_NAME, LFS_O_WRONLY | LFS_O_CREAT);

    if (err) {
        PRINTF("fail to save device addr\r\n");
        return err;
    }

    len = lfs_file_write(lfs, &lfs_file, addr, 6U);
    if (len != 6U) {
        PRINTF("fail to save device addr\r\n");
        err = -EIO;
    } else {
        err = 0;
    }
    lfs_file_close(lfs, &lfs_file);

    return err;
}
#endif

#if ((defined AUTO_CONNECT_USE_BOND_INFO) && (AUTO_CONNECT_USE_BOND_INFO))
static void bond_info(const struct bt_br_bond_info *info, void *user_data)
{
	bool *valid = (bool *)user_data;

    if (!(*valid)) {
        *valid = true;
        auto_connect_device = info->addr;
    }
}
#endif

void app_hfp_hf_auto_connect(void)
{
#if ((defined AUTO_CONNECT_USE_BOND_INFO) && (AUTO_CONNECT_USE_BOND_INFO))
    bool valid = false;

    memset(&auto_connect_device, 0, sizeof(auto_connect_device));
    bt_br_foreach_bond(bond_info, &valid);

    if (valid) {
        struct bt_conn *conn;

        default_connect_initiated = 1U;
        conn = bt_conn_create_br(&auto_connect_device, BT_BR_CONN_PARAM_DEFAULT);
        if (!conn) {
            PRINTF("Connection failed\r\n");
        } else {
            bt_conn_unref(conn);
            PRINTF("Connection pending\r\n");
        }
    } else {
        app_hf_set_connectable();
    }
#else
    lfs = lfs_pl_init();
    if ((lfs != NULL) && !app_auto_connect_get_addr()) {
        struct bt_conn *conn;

        default_connect_initiated = 1U;
        conn = bt_conn_create_br(&auto_connect_device, BT_BR_CONN_PARAM_DEFAULT);
        if (!conn) {
            PRINTF("Connection failed\r\n");
        } else {
            bt_conn_unref(conn);
            PRINTF("Connection pending\r\n");
        }
    } else {
        app_hf_set_connectable();
    }
#endif
}

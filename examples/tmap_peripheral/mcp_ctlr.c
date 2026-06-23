/** @file
 *  @brief Bluetooth Media Control Profile (MCP) Controller role.
 *
 *  Copyright 2023 NXP
 *  Copyright (c) 2024 Nordic Semiconductor ASA
 *
 *  SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/bluetooth/audio/mcc.h>
#include <zephyr/bluetooth/audio/media_proxy.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>

static struct bt_conn *default_conn;

static StaticSemaphore_t sem_discovery_done_buff;
static SemaphoreHandle_t sem_discovery_done;

static void mcc_discover_mcs_cb(struct bt_conn *conn, int err)
{
	if (err) {
		PRINTF("MCP: Discovery of MCS failed (%d)\n", err);
	} else {
		PRINTF("MCP: Discovered MCS\n");
	}
	xSemaphoreGive(sem_discovery_done);
}

static void mcc_send_command_cb(struct bt_conn *conn, int err, const struct mpl_cmd *cmd)
{
	if (err) {
		PRINTF("MCP: Command send failed (%d) - opcode: %u, param: %d\n",
			err, cmd->opcode, cmd->param);
	} else {
		PRINTF("MCP: Successfully sent command (%d) - opcode: %u, param: %d\n",
			err, cmd->opcode, cmd->param);
	}
}

static struct bt_mcc_cb mcc_cb = {
	.discover_mcs = mcc_discover_mcs_cb,
	.send_cmd = mcc_send_command_cb,
};

int mcp_ctlr_init(struct bt_conn *conn)
{
	int err;

	sem_discovery_done = xSemaphoreCreateCountingStatic(UINT32_MAX, 0, &sem_discovery_done_buff);

	default_conn = bt_conn_ref(conn);

	err = bt_mcc_init(&mcc_cb);
	if (err != 0) {
		return err;
	}

	err = bt_mcc_discover_mcs(default_conn, true);
	if (err == 0) {
		xSemaphoreTake(sem_discovery_done, portMAX_DELAY);
	}
	return err;
}

int mcp_send_cmd(uint8_t mcp_opcode)
{
	int err;
	struct mpl_cmd cmd;

	cmd.opcode = mcp_opcode;
	cmd.use_param = false;

	if (default_conn == NULL) {
		PRINTF("MCP: No connection\n");
		return -EINVAL;
	}

	err = bt_mcc_send_cmd(default_conn, &cmd);
	if (err != 0) {
		PRINTF("MCP: Command failed: %d\n", err);
	}

	return err;
}

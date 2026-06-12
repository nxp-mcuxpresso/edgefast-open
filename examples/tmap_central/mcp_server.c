/** @file
 *  @brief Bluetooth Media Control Profile (MCP) Server role.
 *
 *  Copyright 2023 NXP
 *
 *  SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/media_proxy.h>
#include <zephyr/kernel.h>

int mcp_server_init(void)
{
	int err;

	err = media_proxy_pl_init();

	return err;
}

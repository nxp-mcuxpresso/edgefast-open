/*
 * Copyright 2021 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __APP_SPP_H__
#define __APP_SPP_H__

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* Forward declarations */
struct bt_conn;

/*******************************************************************************
 * API
 ******************************************************************************/
void spp_task(void *param);
/** @brief SPP appl server channel register
 *
 *  Register spp server channel.
 *
 *  @param channel Server channel wanted to be registered.
 */
int spp_appl_server_register(uint8_t channel);

/** @brief SPP appl server register with UUID
 *
 *  Register SPP server with a 128-bit UUID instead of standard SPP service class.
 *
 *  @param str UUID string in format "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx".
 *             Only 128-bit UUID is supported for server registration.
 *
 *  Notice: This API registers a custom SPP service with a specific UUID.
 *          The RFCOMM channel will be allocated automatically.
 */
int spp_appl_server_register_uuid(const char *str);

/** @brief SPP appl connect by UUID
 *
 *  Connect to remote SPP service by UUID using SDP service discovery.
 *
 *  @param conn BR/EDR connection.
 *  @param str  UUID string. Supports:
 *              - 16-bit UUID: 4-character hex string (e.g., "1101")
 *              - 32-bit UUID: 8-character hex string
 *              - 128-bit UUID: standard format "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
 *
 *  Notice: This API performs SDP discovery to find the RFCOMM channel
 *          associated with the specified UUID, then establishes the connection.
 */
int spp_appl_connect_by_uuid(struct bt_conn *conn, const char *str);

/** @brief SPP appl connect
 *
 *  Conn spp appl.
 *
 *  @param conn    BR conn.
 *  @param channel Channel wanted to be connect.
 */
int spp_appl_connect(struct bt_conn *conn, uint8_t channel);

/** @brief SPP appl disconnect
 *
 *  Disconnect current active spp appl.
 *
 */
int spp_appl_disconnect(void);

/** @brief SPP appl send data
 *
 *  Send data through the currently active SPP connection.
 *
 *  @param data Pointer to the data buffer to be sent.
 *  @param len  Length of the data to be sent in bytes.
 *
 *  @return None
 *
 *  Notice: This function sends arbitrary data over the active SPP channel.
 *          Make sure an SPP connection is established before calling this function.
 */
int spp_appl_send(uint8_t *data, size_t len);

#endif /* __APP_SPP_H__ */

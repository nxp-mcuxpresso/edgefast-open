/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __APP_CONNECT_H__
#define __APP_CONNECT_H__

/*******************************************************************************
 * Definitions
 ******************************************************************************/

extern struct bt_conn *default_conn;

/*******************************************************************************
 * API
 ******************************************************************************/

void app_connect_init(void);

#endif /* __APP_CONNECT_H__ */
/*
 * Copyright 2020 - 2021, 2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __APP_A2DP_SINK_H__
#define __APP_A2DP_SINK_H__

/*******************************************************************************
 * Definitions
 ******************************************************************************/
/* Add forward declaration */
struct bt_conn;

/*******************************************************************************
 * API
 ******************************************************************************/

void app_a2dp_sink_task(void *param);
void app_a2dp_connect(struct bt_conn *conn);

#endif /* __BT_BR_A2DP_SINK_H__ */
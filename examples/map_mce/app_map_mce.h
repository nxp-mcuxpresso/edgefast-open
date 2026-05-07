/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __APP_MAP_MCE_H__
#define __APP_MAP_MCE_H__

/*******************************************************************************
 * Definitions
 ******************************************************************************/

enum map_mce_state {
	APP_STATE_IDLE = 0,
	APP_STATE_TRANSPORT_CONNECT,
	APP_STATE_MAS_OBEX_CONNECT,
	APP_STATE_GET_FOLDER_LISTING,
	APP_STATE_SET_FOLDER_ROOT,
	APP_STATE_SET_FOLDER_TELECOM,
	APP_STATE_SET_FOLDER_MSG,
	APP_STATE_SET_FOLDER_INBOX,
	APP_STATE_UPDATE_INBOX,
	APP_STATE_GET_MSG_LISTING,
	APP_STATE_GET_MSG,
	APP_STATE_SET_MSG_STATUS,
	APP_STATE_GET_CONVO_LISTING,
	APP_STATE_GET_MAS_INST_INFO,
	APP_STATE_SET_NTF_FILTER,
	APP_STATE_SET_NTF_REG_ON,
	APP_STATE_SET_NTF_REG_OFF,
	APP_STATE_GET_OWNER_STATUS,
	APP_STATE_SET_OWNER_STATUS,
	APP_STATE_SET_FOLDER_PARENT,
	APP_STATE_SET_FOLDER_OUTBOX,
	APP_STATE_PUSH_MSG,
	APP_STATE_MAS_OBEX_DISCONNECT,
	APP_STATE_TRANSPORT_DISCONNECT,
	APP_STATE_COUNT,
};

struct mce_mas_instance {
	struct bt_map_mce_mas mce_mas;
	struct bt_conn *conn;
	uint32_t conn_id;
	uint32_t supported_features;
	uint16_t mopl;
	uint16_t tx_cnt;
	uint16_t psm;
	uint8_t channel;
	uint8_t instance_id;
	uint8_t srm;
	bool goep_v2;
	enum map_mce_state state;
	char msg_handle[CONFIG_BT_MAP_MCE_NAME_MAX_LEN];
};

struct mce_mns_instance {
	struct bt_map_mce_mns mce_mns;
	struct bt_conn *conn;
	uint8_t srm;
	bool goep_v2;
};

/*******************************************************************************
 * API
 ******************************************************************************/
void app_map_mce_task(void *pvParameters);

#endif /* __APP_MAP_MCE_H__ */

/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __APP_SCO_BRIDGE_H__
#define __APP_SCO_BRIDGE_H__

#include <zephyr/bluetooth/classic/hfp_ag.h>

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define APP_MAX_HFP_AG_CONN (2U)

typedef struct app_hfp_ag_
{
    struct bt_hfp_ag *hfp_agHandle;
    struct bt_conn *conn;
    struct bt_conn *sco_conn;
    uint8_t selectCodec;
    uint8_t hfp_in_calling_status;
} app_hfp_ag_t;

typedef enum _hfp_ag_volume_type_t
{
    /*speaker */
    hfp_ag_volume_type_speaker = 0,
    /* mic */
    hfp_ag_volume_type_mic,
} hfp_ag_volume_type_t;

typedef enum _hf_volume_type_t
{
    /*speaker */
    hf_volume_type_speaker = 0,
    /* mic */
    hf_volume_type_mic,
} hf_volume_type_t;

extern app_hfp_ag_t g_HfpAgs[CONFIG_BT_MAX_CONN];

/*******************************************************************************
 * API
 ******************************************************************************/

/* SCO Bridge main task */
void app_sco_bridge_task(void *param);

/* Ensure HFP HF is registered (called before connecting to AG) */
int app_hfp_ag_ensure_registered(void);

/* HFP AG functions */
int app_hfp_ag_discover(struct bt_conn *conn, uint8_t channel);
int app_hfp_ag_start_incoming_call(void);
int app_hfp_ag_accept_incoming_call(uint8_t call_index);
int app_hfp_ag_stop_incoming_call(uint8_t call_index);
int app_hfp_ag_start_twc_incoming_call(void);
void app_hfp_ag_volume_update(hfp_ag_volume_type_t type, int volume);
int app_hfp_ag_codec_select(uint8_t codec);
uint8_t app_get_negotiated_codec(void);
void app_hfp_ag_open_audio(void);
void app_hfp_ag_close_audio(void);
int app_hfp_ag_set_hf_indicator(uint8_t indicator, uint8_t control);
void app_hfp_ag_select_conn(uint8_t index);

/* HFP HF functions */
int app_hfp_hf_connect(struct bt_conn *conn, uint8_t channel);
void hfp_hf_dial(const char *number);
void hfp_hf_accept_call(uint8_t index);
void hfp_hf_reject_call(uint8_t index);
void hfp_hf_terminate(void);
void hfp_hf_query_list_current_calls(void);
void hfp_hf_volume_update(hf_volume_type_t type, int volume);
void hfp_hf_last_dial(void);
int app_hfp_hf_discover(struct bt_conn *conn, uint8_t channel);

#endif /* __APP_SCO_BRIDGE_H__ */

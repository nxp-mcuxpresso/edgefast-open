/*
 * Copyright 2021, 2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __APPL_PERIPHERAL_HFP_HF_MAIN_H__
#define __APPL_PERIPHERAL_HFP_HF_MAIN_H__

/*******************************************************************************
 * Definitions
 ******************************************************************************/
struct bt_conn;
/*******************************************************************************
 * API
 ******************************************************************************/
/** @brief bt hfp volume type */
typedef enum _hf_volume_type_t
{
    /*speaker */
    hf_volume_type_speaker = 0,
    /* mic */
    hf_volume_type_mic,
} hf_volume_type_t;

int app_hfp_hf_connect(struct bt_conn *conn, uint8_t channel);
void peripheral_hfp_hf_task(void *param);
void hfp_AnswerCall(void);
void hfp_RejectCall(void);
void hfp_dial(const char *number);
void dial_memory(int location);
void hfp_last_dial(void);
void hfp_start_voice_recognition(void);
void hfp_hf_get_last_voice_tag_number(void);
void hfp_stop_voice_recognition(void);
void hfp_volume_update(hf_volume_type_t type, int volume);
void hfp_enable_ccwa( uint8_t enable);
void hfp_enable_clip( uint8_t enable);
void hfp_multiparty_call_option(uint8_t option);
void hfp_trigger_codec_connection(void);
int app_hfp_hf_discover(struct bt_conn *conn, uint8_t channel);
void hfp_hf_query_list_current_calls(void);
void hfp_terminate(void);
#endif /* __APPL_PERIPHERAL_HFP_HF_MAIN_H__ */

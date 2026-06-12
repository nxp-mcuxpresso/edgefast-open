/*
 * Copyright 2021, 2024-2025 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <porting.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/slist.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/hfp_hf.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include "fsl_debug_console.h"
#include "app_handsfree.h"
#include "sco_audio_pl_ext.h"
#include "app_shell.h"
#include "app_connect.h"

#define APP_HFP_HF_INITIAL_VGS_GAIN 12
#define APP_HFP_HF_INITIAL_VGM_GAIN 12
#define HFP_CLASS_OF_DEVICE (0x200404U)

static volatile uint8_t s_call_status = 0;

struct bt_hfp_hf *hfp_hf;
struct bt_conn *hf_sco_conn;
static struct bt_hfp_hf_call *hfp_hf_call[CONFIG_BT_HFP_HF_MAX_CALLS];

static void auth_cancel(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    PRINTF("Pairing cancelled: %s\n", addr);
}

static void passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    PRINTF("Passkey %06u\n", passkey);
}

static struct bt_conn_auth_cb auth_cb_display = {
    .cancel = auth_cancel,
    .passkey_display = passkey_display,
};

static void hf_add_a_call(struct bt_hfp_hf_call *call)
{
	ARRAY_FOR_EACH(hfp_hf_call, i) {
		if (!hfp_hf_call[i]) {
			hfp_hf_call[i] = call;
			return;
		}
	}
}

static void hf_remove_a_call(struct bt_hfp_hf_call *call)
{
	ARRAY_FOR_EACH(hfp_hf_call, i) {
		if (call == hfp_hf_call[i]) {
			hfp_hf_call[i] = NULL;
			return;
		}
	}
}

static void hf_remove_calls(void)
{
	ARRAY_FOR_EACH(hfp_hf_call, i) {
		if (hfp_hf_call[i] != NULL) {
			hfp_hf_call[i] = NULL;
		}
	}
}

static void hf_connected(struct bt_conn *conn, struct bt_hfp_hf *hf)
{
#if !((defined AUTO_CONNECT_USE_BOND_INFO) && (AUTO_CONNECT_USE_BOND_INFO))
    struct bt_conn_info info;
#endif

    default_conn = conn;
    hfp_hf = hf;
    PRINTF("HFP HF Connected success\n");

#if !((defined AUTO_CONNECT_USE_BOND_INFO) && (AUTO_CONNECT_USE_BOND_INFO))
    bt_conn_get_info(conn, &info);
    app_auto_connect_save_addr(info.br.dst);
#endif
}

static void hf_disconnected(struct bt_hfp_hf *hf)
{
    if (default_conn) {
        default_conn = NULL;
    }
    hfp_hf = NULL;
    hf_remove_calls();
    PRINTF("HFP HF Disconnected\n");
}

static void hf_sco_connected(struct bt_hfp_hf *hf, struct bt_conn *sco_conn)
{
    struct bt_conn_info info;
    uint16_t handle;
    sco_audio_ep_info ep_info;

    PRINTF("HF SCO connected %p\n", sco_conn);

    if (hf_sco_conn != NULL) {
        PRINTF("HF SCO conn %p exists\n", hf_sco_conn);
        return;
    }

    hf_sco_conn = bt_conn_ref(sco_conn);
    if (bt_hci_get_conn_handle(sco_conn, &handle) < 0) {
        PRINTF("Failed to get SCO connection handle\n");
        return;
    }

    if (bt_conn_get_info(sco_conn, &info) < 0) {
        PRINTF("Failed to get SCO connection info\n");
        return;
    }
    PRINTF("HF SCO info:\n");
    PRINTF("  SCO handle 0x%04X\n", handle);
    PRINTF("  SCO air mode %u\n", info.sco.air_mode);
    PRINTF("  SCO link type %u\n", info.sco.link_type);

	switch (info.sco.air_mode) {
	case BT_HCI_CODING_FORMAT_CVSD:
		ep_info.sample_len = 16;
		ep_info.sampl_freq = 8000;
		break;
	case BT_HCI_CODING_FORMAT_TRANSPARENT:
		ep_info.sample_len = 16;
		ep_info.sampl_freq = 16000;
		break;
	case BT_HCI_CODING_FORMAT_ULAW_LOG:
	case BT_HCI_CODING_FORMAT_ALAW_LOG:
	case BT_HCI_CODING_FORMAT_LINEAR_PCM:
	case BT_HCI_CODING_FORMAT_MSBC:
	case BT_HCI_CODING_FORMAT_LC3:
	case BT_HCI_CODING_FORMAT_G729A:
	case BT_HCI_CODING_FORMAT_VS:
	default:
		PRINTF("Unsupported air mode: %d\n", info.sco.air_mode);
		return ;
	}

    sco_audio_setup_pl_ext(&ep_info);
    sco_audio_start_pl_ext();

}

static void hf_sco_disconnected(struct bt_conn *sco_conn, uint8_t reason)
{
    PRINTF("HF SCO disconnected %p (reason %u)\n", sco_conn, reason);

    if (hf_sco_conn == sco_conn) {
        bt_conn_unref(hf_sco_conn);
        hf_sco_conn = NULL;
        sco_audio_stop_pl_ext();
    } else {
        PRINTF("Unknown SCO disconnected (%p != %p)\n", hf_sco_conn, sco_conn);
    }
}

static void hf_service(struct bt_hfp_hf *hf, uint32_t value)
{
    PRINTF("Service indicator value: %lu\n", value);
}

static void hf_outgoing(struct bt_hfp_hf *hf, struct bt_hfp_hf_call *call)
{
    hf_add_a_call(call);
    PRINTF("HF call %p outgoing\n", call);
}

static void hf_remote_ringing(struct bt_hfp_hf_call *call)
{
    PRINTF("HF remote call %p start ringing\n", call);
}

static void hf_incoming(struct bt_hfp_hf *hf, struct bt_hfp_hf_call *call)
{
    hf_add_a_call(call);
    PRINTF("HF call %p incoming\n", call);
    s_call_status = 1;
}

static void hf_incoming_held(struct bt_hfp_hf_call *call)
{
    PRINTF("HF call %p is held\n", call);
}

static void hf_accept(struct bt_hfp_hf_call *call)
{
    PRINTF("HF call %p accepted\n", call);
    s_call_status = 2;
}

static void hf_reject(struct bt_hfp_hf_call *call)
{
    hf_remove_a_call(call);
    PRINTF("HF call %p rejected\n", call);
    s_call_status = 0;
}

static void hf_terminate(struct bt_hfp_hf_call *call)
{
    hf_remove_a_call(call);
    PRINTF("HF call %p terminated\n", call);
    s_call_status = 0;
}

static void hf_held(struct bt_hfp_hf_call *call)
{
    PRINTF("HF call %p held\n", call);
    s_call_status = 2;
}

static void hf_retrieve(struct bt_hfp_hf_call *call)
{
    PRINTF("HF call %p retrieve\n", call);
}

static void hf_signal(struct bt_hfp_hf *hf, uint32_t value)
{
    PRINTF("Signal indicator value: %lu\n", value);
}

static void hf_roam(struct bt_hfp_hf *hf, uint32_t value)
{
    PRINTF("Roaming indicator value: %lu\n", value);
}

static void hf_battery(struct bt_hfp_hf *hf, uint32_t value)
{
    PRINTF("Battery indicator value: %lu\n", value);
}

static void hf_ring_indication(struct bt_hfp_hf_call *call)
{
    PRINTF("HF call %p ring\n", call);
}

static void hf_dialing(struct bt_hfp_hf *hf, int err)
{
    PRINTF("HF start dialing call: err %d\n", err);
}

#if defined(CONFIG_BT_HFP_HF_CLI)
static void hf_clip(struct bt_hfp_hf_call *call, const char *number, uint8_t type)
{
    PRINTF("HF call %p CLIP %s %d\n", call, number, type);
    PRINTF("Phone call number: %s\n", number);
}
#endif /* CONFIG_BT_HFP_HF_CLI */

#if defined(CONFIG_BT_HFP_HF_VOLUME)
static void hf_vgm(struct bt_hfp_hf *hf, uint8_t gain)
{
    PRINTF("HF VGM %d\n", gain);
}

static void hf_vgs(struct bt_hfp_hf *hf, uint8_t gain)
{
    PRINTF("HF VGS %d\n", gain);
}
#endif /* CONFIG_BT_HFP_HF_VOLUME */

static void hf_inband_ring(struct bt_hfp_hf *hf, bool inband)
{
    PRINTF("HF ring: %s\n", inband ? "in-band" : "no in-band");
}

static void hf_operator(struct bt_hfp_hf *hf, uint8_t mode, uint8_t format, const char *operator)
{
    PRINTF("HF mode %d, format %d, operator %s\n", mode, format, operator);
}

#if defined(CONFIG_BT_HFP_HF_CODEC_NEG)
static void hf_codec_negotiate(struct bt_hfp_hf *hf, uint8_t id)
{
	int err;

	PRINTF("HF codec negotiate 0x%02x\n", id);

	if (id == BT_HFP_HF_CODEC_CVSD) {
		PRINTF("HF codec negotiate CVSD\n");
	} else if (IS_ENABLED(CONFIG_BT_HFP_HF_CODEC_MSBC) && id == BT_HFP_HF_CODEC_MSBC) {
		PRINTF("HF codec negotiate mSBC\n");
	} else if (IS_ENABLED(CONFIG_BT_HFP_HF_CODEC_LC3_SWB) && id == BT_HFP_HF_CODEC_LC3_SWB) {
		PRINTF("HF codec negotiate LC3 SWB\n");
	} else {
		PRINTF("HF codec negotiate unknown codec\n");
		return;
	}
	err = bt_hfp_hf_select_codec(hf, id);
	if (err != 0) {
		PRINTF("Failed to send codec id: %d\n", err);
	}
}
#endif /* CONFIG_BT_HFP_HF_CODEC_NEG */

#if defined(CONFIG_BT_HFP_HF_3WAY_CALL)
static void hf_call_waiting(struct bt_hfp_hf_call *call, const char *number, uint8_t type)
{
    PRINTF("3way call %p waiting. number %s type %d\n", call, number, type);
    PRINTF("> CALL WAITING Received Number: %s\n", number);
    PRINTF("> Please use <multipcall> to handle multiple call operation\n");
}
#endif /* CONFIG_BT_HFP_HF_3WAY_CALL */

#if defined(CONFIG_BT_HFP_HF_ECS)
static void hf_query_call(struct bt_hfp_hf *hf, struct bt_hfp_hf_current_call *call)
{
    if (call == NULL) {
        return;
    }

    PRINTF("CLCC idx %d dir %d status %d mode %d mpty %d number %s type %d\n",
           call->index, call->dir, call->status, call->mode, call->multiparty,
           call->number != NULL ? call->number : "UNKNOWN", call->type);
}
#endif /* CONFIG_BT_HFP_HF_ECS */

static struct bt_hfp_hf_cb hf_cb = {
    .connected = hf_connected,
    .disconnected = hf_disconnected,
    .sco_connected = hf_sco_connected,
    .sco_disconnected = hf_sco_disconnected,
    .service = hf_service,
    .outgoing = hf_outgoing,
    .remote_ringing = hf_remote_ringing,
    .incoming = hf_incoming,
    .incoming_held = hf_incoming_held,
    .accept = hf_accept,
    .reject = hf_reject,
    .terminate = hf_terminate,
    .held = hf_held,
    .retrieve = hf_retrieve,
    .signal = hf_signal,
    .roam = hf_roam,
    .battery = hf_battery,
    .ring_indication = hf_ring_indication,
    .dialing = hf_dialing,
#if defined(CONFIG_BT_HFP_HF_CLI)
    .clip = hf_clip,
#endif /* CONFIG_BT_HFP_HF_CLI */
#if defined(CONFIG_BT_HFP_HF_VOLUME)
    .vgm = hf_vgm,
    .vgs = hf_vgs,
#endif /* CONFIG_BT_HFP_HF_VOLUME */
    .inband_ring = hf_inband_ring,
    .operator = hf_operator,
#if defined(CONFIG_BT_HFP_HF_CODEC_NEG)
    .codec_negotiate = hf_codec_negotiate,
#endif /* CONFIG_BT_HFP_HF_CODEC_NEG */
#if defined(CONFIG_BT_HFP_HF_3WAY_CALL)
    .call_waiting = hf_call_waiting,
#endif /* CONFIG_BT_HFP_HF_3WAY_CALL */

#if defined(CONFIG_BT_HFP_HF_ECS)
    .query_call = hf_query_call,
#endif /* CONFIG_BT_HFP_HF_ECS */
};

static void app_edgefast_hfp_hf_init(void)
{
    int err;

    err = bt_hfp_hf_register(&hf_cb);
    if (err < 0) {
        PRINTF("HFP HF Registration failed (err %d)\n", err);
    }
}

int app_hfp_hf_connect(struct bt_conn *conn, uint8_t channel)
{
    int err = 0;

    if (default_conn == conn) {
        err = bt_hfp_hf_connect(default_conn, &hfp_hf, channel);
        if (err < 0) {
            PRINTF("Fail to connect HFP HF (err: %d)\n", err);
        }
    }
    return err;
}

static void bt_ready(int err)
{

    if (err < 0) {
        PRINTF("Bluetooth init failed (err %d)\n", err);
        return;
    }

    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
        settings_load();
    }

    PRINTF("Bluetooth initialized\n");

    app_connect_init();
    bt_conn_auth_cb_register(&auth_cb_display);
    app_edgefast_hfp_hf_init();
    app_shell_init();
    app_hfp_hf_auto_connect();
}

void hfp_AnswerCall(void)
{
    int err;

    if (hfp_hf_call[0] != NULL) {
        err = bt_hfp_hf_accept(hfp_hf_call[0]);
        if (err < 0) {
            PRINTF("Failed to accept call: %d\n", err);
        } else {
            s_call_status = 2;
            PRINTF("SUCCESS: Call accepted\n");
        }
    } else {
        PRINTF("No active call to answer\n");
    }
}

void hfp_RejectCall(void)
{
    int err;

    if (hfp_hf_call[0] != NULL) {
        err = bt_hfp_hf_reject(hfp_hf_call[0]);
        if (err < 0) {
            PRINTF("Failed to reject call: %d\n", err);
        } else {
            s_call_status = 0;
            PRINTF("SUCCESS: Call rejected\n");
        }
    } else {
        PRINTF("No active call to reject\n");
    }
}

void hfp_dial(const char *number)
{
    int err;

    err = bt_hfp_hf_number_call(hfp_hf, number);
    if (err < 0) {
        PRINTF("Failed to start phone number call: %d\n", err);
    } else {
        PRINTF("SUCCESS: Dialing %s\n", number);
    }
}

void hfp_terminate(void)
{
    int err;

    err = bt_hfp_hf_terminate(hfp_hf_call[0]);
    if (err) {
        PRINTF("Failed to terminate call: %d\n", err);
    } else {
        PRINTF("SUCCESS: Call terminated\n");
    }
}

void dial_memory(int location)
{
    int err;
    char location_str[16];

    snprintf(location_str, sizeof(location_str), "%d", location);
    err = bt_hfp_hf_memory_dial(hfp_hf, location_str);
    if (err < 0) {
        PRINTF("Failed to memory dial call: %d\n", err);
    } else {
        PRINTF("SUCCESS: Memory dial initiated for location %d\n", location);
    }
}

void hfp_last_dial(void)
{
    int err;

    err = bt_hfp_hf_redial(hfp_hf);
    if (err < 0) {
        PRINTF("Failed to redial call: %d\n", err);
    } else {
        PRINTF("SUCCESS: Redial initiated\n");
    }
}

void hfp_start_voice_recognition(void)
{
    int err;

    err = bt_hfp_hf_voice_recognition(hfp_hf, true);
    if (err < 0) {
        PRINTF("Failed to start voice recognition: %d\n", err);
    } else {
        PRINTF("SUCCESS: Voice recognition started\n");
    }
}

void hfp_hf_get_last_voice_tag_number(void)
{
    int err;

    err = bt_hfp_hf_request_phone_number(hfp_hf);
    if (err < 0) {
        PRINTF("Failed to request phone number: %d\n", err);
    } else {
        PRINTF("SUCCESS: Phone number requested\n");
    }
}

void hfp_stop_voice_recognition(void)
{
    int err;

    err = bt_hfp_hf_voice_recognition(hfp_hf, false);
    if (err < 0) {
        PRINTF("Failed to stop voice recognition: %d\n", err);
    } else {
        PRINTF("SUCCESS: Voice recognition stopped\n");
    }
}

void hfp_volume_update(hf_volume_type_t type, int volume)
{
    int err;

    if (type == hf_volume_type_mic) {
        err = bt_hfp_hf_vgm(hfp_hf, (uint8_t)volume);
        if (err < 0) {
            PRINTF("Failed to set microphone volume: %d\n", err);
        } else {
            PRINTF("SUCCESS: Microphone volume set to %d\n", volume);
        }
    } else if (type == hf_volume_type_speaker) {
        err = bt_hfp_hf_vgs(hfp_hf, (uint8_t)volume);
        if (err < 0) {
            PRINTF("Failed to set speaker volume: %d\n", err);
        } else {
            PRINTF("SUCCESS: Speaker volume set to %d\n", volume);
        }
    }
}

void hfp_enable_ccwa(uint8_t enable)
{
    int err;

    err = bt_hfp_hf_call_waiting_notify(hfp_hf, (bool)enable);
    if (err < 0) {
        PRINTF("Failed to set call waiting notify: %d\n", err);
    } else {
        PRINTF("SUCCESS: Call waiting notify %s\n", enable ? "enabled" : "disabled");
    }
}

void hfp_enable_clip(uint8_t enable)
{
    int err;

    err = bt_hfp_hf_cli(hfp_hf, (bool)enable);
    if (err < 0) {
        PRINTF("Failed to set CLI: %d\n", err);
    } else {
        PRINTF("SUCCESS: CLI %s\n", enable ? "enabled" : "disabled");
    }
}

void hfp_multiparty_call_option(uint8_t option)
{
    int err;

    switch (option) {
    case 0: /* Release all held calls */
        err = bt_hfp_hf_release_all_held(hfp_hf);
        break;
    case 1: /* Release active calls and accept other */
        err = bt_hfp_hf_release_active_accept_other(hfp_hf);
        break;
    case 2: /* Hold active calls and accept other */
        err = bt_hfp_hf_hold_active_accept_other(hfp_hf);
        break;
    case 3: /* Join conversation */
        err = bt_hfp_hf_join_conversation(hfp_hf);
        break;
    case 4: /* Explicit call transfer */
        err = bt_hfp_hf_explicit_call_transfer(hfp_hf);
        break;
    default:
        PRINTF("Invalid multiparty call option: %d\n", option);
        return;
    }

    if (err < 0) {
        PRINTF("Failed to execute multiparty call option %d: %d\n", option, err);
    } else {
        PRINTF("SUCCESS: Multiparty call option %d executed\n", option);
    }
}

void hfp_trigger_codec_connection(void)
{
    int err;

    err = bt_hfp_hf_audio_connect(hfp_hf);
    if (err < 0) {
        PRINTF("Failed to start audio connection procedure: %d\n", err);
    } else {
        PRINTF("SUCCESS: Audio connection procedure started\n");
    }
}

void hfp_hf_query_list_current_calls(void)
{
    int err;

    err = bt_hfp_hf_query_list_of_current_calls(hfp_hf);
    if (err < 0) {
        PRINTF("Failed to query list of current calls: %d\n", err);
    } else {
        PRINTF("SUCCESS: Query list of current calls initiated\n");
    }
}

void peripheral_hfp_hf_task(void *pvParameters)
{
    int err;

    PRINTF("Bluetooth Handsfree demo start...\n");

    /* Initializate BT Host stack */
    err = bt_enable(bt_ready);
    if (err < 0) {
        PRINTF("Bluetooth init failed (err %d)\n", err);
        while (1) {
            vTaskDelay(2000);
        }
    }

    vTaskDelete(NULL);
}

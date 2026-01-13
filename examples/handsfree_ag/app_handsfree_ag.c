/*
 * Copyright 2021, 2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_debug_console.h"

#include "FreeRTOS.h"
#include "task.h"

#include <porting.h>
#include <string.h>
#include <stdbool.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/slist.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/hfp_ag.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include "app_handsfree_ag.h"
#include "app_connect.h"
#include "app_shell.h"
#include "app_discover.h"
#include "sco_audio_pl_ext.h"

/* User may need to change it for real production */
#define APP_CLASS_OF_DEVICE (0x200000U)
app_hfp_ag_t g_HfpAgs[CONFIG_BT_MAX_CONN];
static app_hfp_ag_t *g_HfpAg;

static struct bt_hfp_ag_call *hfp_ag_call[CONFIG_BT_HFP_AG_MAX_CALLS];

static void ag_add_a_call(struct bt_hfp_ag_call *call)
{
	ARRAY_FOR_EACH(hfp_ag_call, i) {
		if (!hfp_ag_call[i]) {
			hfp_ag_call[i] = call;
			return;
		}
	}
}

static void ag_remove_a_call(struct bt_hfp_ag_call *call)
{
	ARRAY_FOR_EACH(hfp_ag_call, i) {
		if (call == hfp_ag_call[i]) {
			hfp_ag_call[i] = NULL;
			return;
		}
	}
}

static void ag_remove_calls(void)
{
	ARRAY_FOR_EACH(hfp_ag_call, i) {
		if (hfp_ag_call[i] != NULL) {
			hfp_ag_call[i] = NULL;
		}
	}
}

static void app_ag_remove_instance(struct bt_hfp_ag *hfp_ag)
{
    for (uint8_t index = 0; index < CONFIG_BT_MAX_CONN; index++)
    {
        if (g_HfpAgs[index].hfp_agHandle == hfp_ag)
        {
            g_HfpAgs[index].hfp_agHandle = NULL;
            g_HfpAgs[index].conn = NULL;
            break;
        }
    }
}

static app_hfp_ag_t *app_ag_get_instance(struct bt_hfp_ag *hfp_ag)
{
    for (uint8_t index = 0; index < CONFIG_BT_MAX_CONN; index++)
    {
        if (g_HfpAgs[index].hfp_agHandle == hfp_ag)
        {
            return &g_HfpAgs[index];
        }
    }

    return NULL;
}

static uint8_t app_ag_instance_index(struct bt_hfp_ag *hfp_ag)
{
    for (uint8_t index = 0; index < CONFIG_BT_MAX_CONN; index++)
    {
        if (g_HfpAgs[index].hfp_agHandle == hfp_ag)
        {
            return index;
        }
    }

    return 0xFF; /* Return invalid index instead of 0 */
}

static bool app_ag_check_idle_instance(void)
{
    for (uint8_t index = 0; index < CONFIG_BT_MAX_CONN; index++)
    {
        if (g_HfpAgs[index].hfp_agHandle == NULL)
        {
            return true;
        }
    }

    return false;
}

static void ag_connected(struct bt_conn *conn, struct bt_hfp_ag *ag)
{
    size_t index;

    index = (size_t)bt_conn_index(conn);
    __ASSERT(index < ARRAY_SIZE(g_HfpAgs), "Conn index is out of bounds");

    g_HfpAgs[index].hfp_agHandle = ag;
    g_HfpAgs[index].conn = conn;
    g_HfpAgs[index].selectCodec = 1;

    g_HfpAg = &g_HfpAgs[index];

   PRINTF("AG connected");
}

static void ag_disconnected(struct bt_hfp_ag *ag)
{
	ag_remove_calls();
	PRINTF("AG disconnected");
}

static void ag_sco_connected(struct bt_hfp_ag *ag, struct bt_conn *sco_conn)
{
    struct bt_conn_info info;
    app_hfp_ag_t *hfp_ag;
    uint16_t handle;
    sco_audio_ep_info ep_info;

    PRINTF("AG SCO connected %p\n", sco_conn);

    if (bt_hci_get_conn_handle(sco_conn, &handle) == 0)
    {
        PRINTF("  SCO handle 0x%04X\n", handle);
    }

    if (bt_conn_get_info(sco_conn, &info) == 0)
    {
        PRINTF("  SCO air mode %u\n", info.sco.air_mode);
        PRINTF("  SCO link type %u\n", info.sco.link_type);
    }

    hfp_ag = app_ag_get_instance(ag);
    hfp_ag->sco_conn = sco_conn;

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
            printk("Unsupported air mode: %d\n", info.sco.air_mode);
            return ;
    }

    sco_audio_setup_pl_ext(&ep_info);
    sco_audio_start_pl_ext();
}

static void ag_sco_disconnected(struct bt_conn *sco_conn, uint8_t reason)
{
    PRINTF("AG SCO disconnected %p (reason %u)\n", sco_conn, reason);
    sco_audio_stop_pl_ext();
}

static int ag_get_indicator_value(struct bt_hfp_ag *ag, uint8_t *service, uint8_t *strength,
                                   uint8_t *roam, uint8_t *battery)
{
    if (service != NULL)
    {
        *service = 1;  /* Service available */
    }

    if (strength != NULL)
    {
        *strength = 5;  /* Full signal strength */
    }

    if (roam != NULL)
    {
        *roam = 0;  /* Not roaming */
    }

    if (battery != NULL)
    {
        *battery = 5;  /* Full battery */
    }

    return 0;
}

static int ag_get_ongoing_call(struct bt_hfp_ag *ag)
{
    /* Return -EINVAL if no ongoing calls */
    return -EINVAL;
}

static int ag_memory_dial(struct bt_hfp_ag *ag, const char *location, char **number)
{
    static char *phone = "123456789";

    PRINTF("AG memory dial location: %s\n", location);

    if (strcmp(location, "0") == 0)
    {
        *number = phone;
        return 0;
    }

    return -ENOTSUP;
}

static int ag_number_call(struct bt_hfp_ag *ag, const char *number)
{
    PRINTF("AG number call: %s\n", number);
    /* Implement number call logic here */
    return 0;
}

static void ag_outgoing(struct bt_hfp_ag *ag, struct bt_hfp_ag_call *call, const char *number)
{
    PRINTF("AG outgoing call %p, number %s\n", call, number);
    ag_add_a_call(call);
}

static void ag_incoming(struct bt_hfp_ag *ag, struct bt_hfp_ag_call *call, const char *number)
{
    PRINTF("AG incoming call %p, number %s\n", call, number);
    ag_add_a_call(call);
}

static void ag_incoming_held(struct bt_hfp_ag_call *call)
{
    PRINTF("AG incoming call %p is held\n", call);
}

static void ag_ringing(struct bt_hfp_ag_call *call, bool in_band)
{
    PRINTF("AG call %p start ringing mode %d\n", call, in_band);
}

static void ag_accept(struct bt_hfp_ag_call *call)
{
    PRINTF("AG call %p accept\n", call);
    g_HfpAg->hfp_in_calling_status = 1;
}

static void ag_held(struct bt_hfp_ag_call *call)
{
    PRINTF("AG call %p held\n", call);
}

static void ag_retrieve(struct bt_hfp_ag_call *call)
{
    PRINTF("AG call %p retrieved\n", call);
}

static void ag_reject(struct bt_hfp_ag_call *call)
{
    PRINTF("AG call %p reject\n", call);
}

static void ag_terminate(struct bt_hfp_ag_call *call)
{
    PRINTF("AG call %p terminate\n", call);
}

static void ag_codec(struct bt_hfp_ag *ag, uint32_t ids)
{
    PRINTF("AG received codec id bit map 0x%x\n", ids);
}

void ag_codec_negotiate(struct bt_hfp_ag *ag, int err)
{
    PRINTF("AG codec negotiation result %d", err);
}

static void ag_vgm(struct bt_hfp_ag *ag, uint8_t gain)
{
    PRINTF("AG received vgm %d\n", gain);
}

static void ag_vgs(struct bt_hfp_ag *ag, uint8_t gain)
{
    PRINTF("AG received vgs %d\n", gain);
}

static struct bt_hfp_ag_cb ag_cb = {
    .connected = ag_connected,
    .disconnected = ag_disconnected,
    .sco_connected = ag_sco_connected,
    .sco_disconnected = ag_sco_disconnected,
    .get_indicator_value = ag_get_indicator_value,
    .get_ongoing_call = ag_get_ongoing_call,
    .memory_dial = ag_memory_dial,
    .number_call = ag_number_call,
    .outgoing = ag_outgoing,
    .incoming = ag_incoming,
    .incoming_held = ag_incoming_held,
    .ringing = ag_ringing,
    .accept = ag_accept,
    .held = ag_held,
    .retrieve = ag_retrieve,
    .reject = ag_reject,
    .terminate = ag_terminate,
    .codec = ag_codec,
    .codec_negotiate = ag_codec_negotiate,
    .vgm = ag_vgm,
    .vgs = ag_vgs,
};

int app_hfp_ag_discover(struct bt_conn *conn, uint8_t channel)
{
    struct bt_hfp_ag *hfp_agHandle;
    int status = 0;

    if (!app_ag_check_idle_instance())
    {
        PRINTF("no idle instance\r\n");
        return 0;
    }

    status = bt_hfp_ag_connect(conn, &hfp_agHandle, channel);
    if (0 != status)
    {
        PRINTF("fail to connect hfp_hf (err: %d)\r\n", status);
    }
    return status;
}

static void bt_ready(int err)
{
    if (err)
    {
        PRINTF("Bluetooth init failed (err %d)\n", err);
        return;
    }

#if (defined(CONFIG_BT_SETTINGS) && (CONFIG_BT_SETTINGS > 0))
    settings_load();
#endif /* CONFIG_BT_SETTINGS */

    PRINTF("Bluetooth initialized\n");

    app_connect_init();
    app_discover_init();

    err = bt_br_set_connectable(true);
    if (err)
    {
        PRINTF("BR/EDR set/rest connectable failed (err %d)\n", err);
        return;
    }
    err = bt_br_set_discoverable(true, false);
    if (err)
    {
        PRINTF("BR/EDR set discoverable failed (err %d)\n", err);
        return;
    }
    PRINTF("BR/EDR set connectable and discoverable done\n");

    bt_hfp_ag_register(&ag_cb);
    app_shell_init();
}

int app_hfp_ag_start_incoming_call(void)
{
    int err;
    static char phone_number[] = "123456789";

    if (g_HfpAg == NULL || g_HfpAg->hfp_agHandle == NULL)
    {
        PRINTF("Invalid HFP AG handle\r\n");
        return -EINVAL;
    }

    /* Start incoming call */
    err = bt_hfp_ag_remote_incoming(g_HfpAg->hfp_agHandle, phone_number);
    if (err)
    {
        PRINTF("Failed to start incoming call: %d\r\n", err);
        return err;
    }

    PRINTF("Incoming call started with number: %s\r\n", phone_number);

    return 0;
}

void app_hfp_ag_open_audio()
{
    int err;

    if (g_HfpAg == NULL || g_HfpAg->hfp_agHandle == NULL)
    {
        PRINTF("Invalid HFP AG handle\r\n");
        return;
    }

    err = bt_hfp_ag_audio_connect(g_HfpAg->hfp_agHandle, g_HfpAg->selectCodec);
    if (err)
    {
        PRINTF("Failed to open audio connection: %d\r\n", err);
        return;
    }

    PRINTF("Audio connection opened\r\n");
}

void app_hfp_ag_close_audio()
{
      int err;

      err = bt_conn_disconnect(g_HfpAg->sco_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
      if (err) {
              PRINTF("Disconnect failed: %d", err);
              return;
      }
      PRINTF("Audio connection closed\r\n");
}

int app_hfp_ag_accept_incoming_call(uint8_t call_index)
{
    int err;

    if (g_HfpAg == NULL || g_HfpAg->hfp_agHandle == NULL)
    {
        PRINTF("Invalid HFP AG handle\r\n");
        return -EINVAL;
    }

    /* Accept the incoming call */
    err = bt_hfp_ag_accept(hfp_ag_call[call_index]);
    if (err)
    {
        PRINTF("Failed to accept incoming call: %d\r\n", err);
        return err;
    }

    PRINTF("Incoming call accepted\r\n");
    return 0;
}

int app_hfp_ag_stop_incoming_call(uint8_t call_index)
{
    int err;

    if (g_HfpAg == NULL || g_HfpAg->hfp_agHandle == NULL)
    {
        PRINTF("Invalid HFP AG handle\r\n");
        return -EINVAL;
    }

    /* Stop/reject the incoming call */
    if (g_HfpAg->hfp_in_calling_status == 1)
    {
      err = bt_hfp_ag_terminate(hfp_ag_call[call_index]);
    }
    else
    {
      err = bt_hfp_ag_reject(hfp_ag_call[call_index]);
    }
    if (err)
    {
        PRINTF("Failed to stop incoming call: %d\r\n", err);
        return err;
    }

    PRINTF("Incoming call stopped\r\n");
    return 0;
}

int app_hfp_ag_start_twc_incoming_call(void)
{
    int err;
    static char phone_number[] = "987654321";

    if (g_HfpAg == NULL || g_HfpAg->hfp_agHandle == NULL)
    {
        PRINTF("Invalid HFP AG handle\r\n");
        return -EINVAL;
    }

    /* Start three-way calling incoming call */
    err = bt_hfp_ag_remote_incoming(g_HfpAg->hfp_agHandle, phone_number);
    if (err)
    {
        PRINTF("Failed to start TWC incoming call: %d\r\n", err);
        return err;
    }

    PRINTF("TWC incoming call started with number: %s\r\n", phone_number);
    return 0;
}


int app_hfp_ag_codec_select(uint8_t codec)
{

    if (g_HfpAg == NULL || g_HfpAg->hfp_agHandle == NULL)
    {
        PRINTF("Invalid HFP AG handle\r\n");
        return -EINVAL;
    }

    g_HfpAg->selectCodec = codec;

    return 0;
}

void app_hfp_ag_volume_update(hfp_ag_volume_type_t type, int volume)
{
    int err;

    if (g_HfpAg == NULL || g_HfpAg->hfp_agHandle == NULL)
    {
        PRINTF("Invalid HFP AG handle\r\n");
        return;
    }

    /* Validate volume range (0-15) */
    if (volume < 0 || volume > 15)
    {
        PRINTF("Invalid volume level: %d (must be 0-15)\r\n", volume);
        return;
    }

    /* Send volume control command */
    if (type == hfp_ag_volume_type_speaker)
    {
        err = bt_hfp_ag_vgs(g_HfpAg->hfp_agHandle, (uint8_t)volume);
        if (err)
        {
            PRINTF("Failed to set speaker volume: %d\r\n", err);
        }
        else
        {
            PRINTF("Speaker volume set to: %d\r\n", volume);
        }
    }
    else if (type == hfp_ag_volume_type_mic)
    {
        err = bt_hfp_ag_vgm(g_HfpAg->hfp_agHandle, (uint8_t)volume);
        if (err)
        {
            PRINTF("Failed to set microphone volume: %d\r\n", err);
        }
        else
        {
            PRINTF("Microphone volume set to: %d\r\n", volume);
        }
    }
    else
    {
        PRINTF("Invalid volume type: %d\r\n", type);
    }
}

int app_hfp_ag_set_hf_indicator(uint8_t indicator, uint8_t control)
{
    int err;

    if (g_HfpAg == NULL || g_HfpAg->hfp_agHandle == NULL)
    {
        PRINTF("Invalid HFP AG handle\r\n");
        return -EINVAL;
    }

    /* Enable or disable HF indicator */
    err = bt_hfp_ag_hf_indicator(g_HfpAg->hfp_agHandle, indicator, control ? true : false);
    if (err)
    {
        if (err == -EOPNOTSUPP)
        {
            PRINTF("HF doesn't support indicator 0x%04X\r\n", indicator);
        }
        else
        {
            PRINTF("Failed to set HF indicator: %d\r\n", err);
        }
    }
    else
    {
        PRINTF("HF indicator 0x%04X %s\r\n", indicator, control ? "enabled" : "disabled");
    }

    return err;
}

void app_hfp_ag_select_conn(uint8_t index)
{
    struct bt_conn *acl_conn;

    /* Validate index is within bounds */
    __ASSERT(index < ARRAY_SIZE(g_HfpAgs), "Index is out of bounds");

    if (index >= ARRAY_SIZE(g_HfpAgs))
    {
        PRINTF("Invalid connection index %d (max: %d)\n", index, ARRAY_SIZE(g_HfpAgs) - 1);
        return;
    }

    acl_conn = g_HfpAgs[index].conn;

    if (acl_conn == NULL)
    {
        PRINTF("connection %d is invalid",index);
    }
    else
    {
        g_HfpAg = &g_HfpAgs[index];
    }
}

void peripheral_hfp_ag_task(void *pvParameters)
{
    int err = 0;


    PRINTF("Bluetooth Handsfree AG demo start...\n");

    /* Initializate BT Host stack */
    err = bt_enable(bt_ready);
    if (err)
    {
        PRINTF("Bluetooth init failed (err %d)\n", err);
        return;
    }
    vTaskDelete(NULL);
}

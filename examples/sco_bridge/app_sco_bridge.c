/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_debug_console.h"

#include "FreeRTOS.h"
#include "task.h"

#include <porting.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/hfp_hf.h>
#include <zephyr/bluetooth/classic/hfp_ag.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include "app_sco_bridge.h"
#include "app_connect.h"
#include "app_shell.h"
#include "app_discover.h"

#define APP_CLASS_OF_DEVICE (0x200408U)

/* SCO audio bridge buffer */

/* HFP HF (Hands-Free) instance */
static struct bt_hfp_hf *hfp_hf;
static struct bt_conn *hf_conn;
static struct bt_conn *hf_sco_conn;
static struct bt_hfp_hf_call *hfp_hf_call[CONFIG_BT_HFP_HF_MAX_CALLS];

/* HFP AG (Audio Gateway) instance */
app_hfp_ag_t g_HfpAgs[CONFIG_BT_MAX_CONN];
static struct bt_hfp_ag_call *hfp_ag_call[CONFIG_BT_HFP_AG_MAX_CALLS];
static uint8_t g_select_ag_index = 0;

/* Bridge state */
static bool bridge_active = false;

/* AG registration state */
static bool ag_registered = false;

/* Codec negotiation state */
static uint32_t phone_supported_codecs = BT_HFP_HF_CODEC_CVSD;     /* Phone (AG) codec capabilities when bridge acts as HF */
static uint32_t bridge_hf_codecs = 0;           /* Bridge's local HF codec capabilities */
static uint32_t headset_supported_codecs = BT_HFP_AG_CODEC_CVSD;   /* Headset (HF) codec capabilities when bridge acts as AG */
static uint8_t phone_negotiated_codec = BT_HFP_HF_CODEC_CVSD;  /* Codec negotiated with phone */
static bool pending_ag_sco = false;  /* Flag indicating AG SCO establishment is pending */
static uint8_t negotiate_best_codec(uint32_t hf_codecs, uint32_t ag_codecs);
static int establish_ag_sco_with_codec(uint8_t codec);
/* ============================================================================
 * HFP HF Call Management
 * ============================================================================ */
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

/* ============================================================================
 * HFP AG Call Management
 * ============================================================================ */
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

/* ============================================================================
 * SCO Audio Bridge Functions
 * ============================================================================ */
struct bt_hci_nxp_vs_cmd {
	uint16_t opcode;
	uint8_t flags;
	uint16_t data_len;
	uint8_t *data;
};

/* NXP Vendor Specific Commands */
#define BT_HCI_OP_NXP_ESCO_BRIDGE    0xFC6F  /* Opcode for eSCO bridge */

static int enable_esco_bridge(uint16_t stESCO, uint16_t ndESCO)
{
    int err;
    struct net_buf *buf;
    uint8_t cmd_data[6];

    PRINTF("Sending Vendor command for eSCO bridge now\n");
    PRINTF("  stESCO handle: 0x%04X\n", stESCO);
    PRINTF("  ndESCO handle: 0x%04X\n", ndESCO);

    /* Prepare command data */
    cmd_data[0] = 0x00;
    cmd_data[1] = 0x01;
    cmd_data[2] = (uint8_t)(stESCO & 0xFF);         /* Lower byte of stESCO */
    cmd_data[3] = (uint8_t)((stESCO >> 8) & 0xFF);  /* Upper byte of stESCO */
    cmd_data[4] = (uint8_t)(ndESCO & 0xFF);         /* Lower byte of ndESCO */
    cmd_data[5] = (uint8_t)((ndESCO >> 8) & 0xFF);  /* Upper byte of ndESCO */

    /* Allocate HCI command buffer */
    buf = bt_hci_cmd_alloc(K_FOREVER);
    if (buf == NULL) {
        PRINTF("Failed to allocate HCI command buffer\n");
        return -ENOBUFS;
    }

    /* Verify buffer has enough space */
    __ASSERT(net_buf_tailroom(buf) >= sizeof(cmd_data), "No space in buffer");

    /* Add command data to buffer */
    net_buf_add_mem(buf, cmd_data, sizeof(cmd_data));

    /* Send vendor specific command synchronously */
    err = bt_hci_cmd_send_sync(BT_HCI_OP_NXP_ESCO_BRIDGE, buf, NULL);
    if (err == -EACCES) {
        PRINTF("VS opcode %04x is disallowed\n", BT_HCI_OP_NXP_ESCO_BRIDGE);
        /* Ignore the disallowed command and continue */
        return 0;
    }

    if (err != 0) {
        PRINTF("Failed to send VS cmd for eSCO bridge: %d\n", err);
        return err;
    }

    PRINTF("eSCO bridge enabled successfully\n");

    return 0;
}

/* ============================================================================
 * HFP HF Callbacks
 * ============================================================================ */
static void hf_connected(struct bt_conn *conn, struct bt_hfp_hf *hf)
{
    int err;

    hf_conn = conn;
    hfp_hf = hf;
    PRINTF("HF: Connected\n");

#if defined(CONFIG_BT_HFP_HF_CODEC_NEG)
    /* Query and store HF codec capabilities */
    /* Build local codec bitmap based on configuration */
    bridge_hf_codecs = BIT(BT_HFP_HF_CODEC_CVSD);  /* CVSD always supported */

    if (IS_ENABLED(CONFIG_BT_HFP_HF_CODEC_MSBC)) {
        bridge_hf_codecs |= BIT(BT_HFP_HF_CODEC_MSBC);
    }

    if (IS_ENABLED(CONFIG_BT_HFP_HF_CODEC_LC3_SWB)) {
        bridge_hf_codecs |= BIT(BT_HFP_HF_CODEC_LC3_SWB);
    }

    PRINTF("HF: Local codec capabilities: 0x%08lx\n", bridge_hf_codecs);

    /* If we have phone (AG) codec info, select the best codec */
    if (phone_supported_codecs != 0) {
        phone_negotiated_codec = negotiate_best_codec(bridge_hf_codecs, phone_supported_codecs);
    }
#endif
}

static void hf_disconnected(struct bt_hfp_hf *hf)
{
    if (hf_conn) {
        hf_conn = NULL;
    }
    hfp_hf = NULL;
    hf_remove_calls();

    /* Reset codec negotiation state */
    bridge_hf_codecs = 0;
    phone_supported_codecs = 0;
    phone_negotiated_codec = BT_HFP_HF_CODEC_CVSD;
    headset_supported_codecs = BT_HFP_HF_CODEC_CVSD;
    pending_ag_sco = false;

    PRINTF("HF: Disconnected\n");
}

static void hf_sco_connected(struct bt_hfp_hf *hf, struct bt_conn *sco_conn)
{
    struct bt_conn_info info;
    uint16_t hf_sco_handle;
    int err;

    PRINTF("HF: SCO connected %p\n", sco_conn);

    if (hf_sco_conn != NULL) {
        PRINTF("HF: SCO conn already exists\n");
        return;
    }

    hf_sco_conn = bt_conn_ref(sco_conn);

    if (bt_hci_get_conn_handle(sco_conn, &hf_sco_handle) == 0) {
        PRINTF("  HF SCO handle 0x%04X\n", hf_sco_handle);
    }

    if (bt_conn_get_info(sco_conn, &info) == 0) {
        PRINTF("  HF SCO air mode %u\n", info.sco.air_mode);
        PRINTF("  HF SCO link type %u\n", info.sco.link_type);
    }

    /* Establish AG SCO connection with phone's negotiated codec */
    err = establish_ag_sco_with_codec(phone_negotiated_codec);
    if (err < 0) {
        PRINTF("  Failed to establish AG SCO: %d\n", err);
    }
}

/* Helper function to establish AG SCO connection with specified codec
 * Returns 0 on success, negative error code on failure.
 * Sets pending_ag_sco flag if headset codec capabilities are not yet known.
 */
static int establish_ag_sco_with_codec(uint8_t codec)
{
    int err;

    PRINTF("Establishing AG SCO with codec 0x%02x\n", codec);

    if (codec == BT_HFP_HF_CODEC_CVSD) {
        PRINTF("  Codec: CVSD\n");
    } else if (codec == BT_HFP_HF_CODEC_MSBC) {
        PRINTF("  Codec: mSBC\n");
    } else if (codec == BT_HFP_HF_CODEC_LC3_SWB) {
        PRINTF("  Codec: LC3-SWB\n");
    }

    err = bt_hfp_ag_audio_connect(g_HfpAgs[g_select_ag_index].hfp_agHandle, codec);

    if (err == -ENOTSUP && codec != BT_HFP_HF_CODEC_CVSD) {
        /* Headset hasn't sent codec capabilities yet (hf_codec_ids is 0).
         * Mark as pending and wait for ag_codec() callback to retry.
         */
        PRINTF("  Headset codec capabilities not received yet\n");
        PRINTF("  Marking AG SCO establishment as pending until headset sends +BAC\n");
        pending_ag_sco = true;
        return 0;  /* Not an error - will retry when codec list arrives */
    }

    if (err < 0) {
        PRINTF("  bt_hfp_ag_audio_connect() failed: %d\n", err);
        return err;
    }

    pending_ag_sco = false;
    return 0;
}

static void hf_sco_disconnected(struct bt_conn *sco_conn, uint8_t reason)
{
    PRINTF("HF: SCO disconnected (reason %u)\n", reason);

    if (hf_sco_conn == sco_conn) {
        bt_conn_unref(hf_sco_conn);
        hf_sco_conn = NULL;
    }

    /* If bridge was active, disconnect AG SCO and reset the flag */
    if (bridge_active) {
        PRINTF("Resetting bridge state\n");
        bridge_active = false;

        /* Disconnect AG SCO connection using Zephyr common API */
        if (g_HfpAgs[g_select_ag_index].sco_conn != NULL) {
            int err = bt_conn_disconnect(g_HfpAgs[g_select_ag_index].sco_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            if (err < 0) {
                PRINTF("Failed to disconnect AG SCO: %d\n", err);
            } else {
                PRINTF("AG SCO disconnect initiated\n");
            }
        }
    }
}

static void hf_service(struct bt_hfp_hf *hf, uint32_t value)
{
    PRINTF("HF: Service indicator: %lu\n", value);
}

static void hf_call(struct bt_hfp_hf *hf, uint32_t value)
{
    PRINTF("HF: Call indicator: %lu\n", value);
}

static void hf_outgoing(struct bt_hfp_hf *hf, struct bt_hfp_hf_call *call)
{
    hf_add_a_call(call);
    PRINTF("HF: Call outgoing\n");
}

static void hf_incoming(struct bt_hfp_hf *hf, struct bt_hfp_hf_call *call)
{
    hf_add_a_call(call);
    PRINTF("HF: Call incoming\n");
}

static void hf_accept(struct bt_hfp_hf_call *call)
{
    PRINTF("HF: Call accepted\n");
}

static void hf_reject(struct bt_hfp_hf_call *call)
{
    hf_remove_a_call(call);
    PRINTF("HF: Call rejected\n");
}

static void hf_terminate(struct bt_hfp_hf_call *call)
{
    hf_remove_a_call(call);
    PRINTF("HF: Call terminated\n");
}

static void hf_ring_indication(struct bt_hfp_hf_call *call)
{
    PRINTF("HF: Ring indication\n");
}

#if defined(CONFIG_BT_HFP_HF_CODEC_NEG)
static void hf_codec_negotiate(struct bt_hfp_hf *hf, uint8_t id)
{
    int err;

    PRINTF("HF: Codec negotiate 0x%02x\n", id);
    if (id == BT_HFP_HF_CODEC_CVSD) {
        PRINTF("  Codec: CVSD\n");
    } else if (IS_ENABLED(CONFIG_BT_HFP_HF_CODEC_MSBC) && id == BT_HFP_HF_CODEC_MSBC) {
        PRINTF("  Codec: mSBC\n");
    } else if (IS_ENABLED(CONFIG_BT_HFP_HF_CODEC_LC3_SWB) && id == BT_HFP_HF_CODEC_LC3_SWB) {
        PRINTF("  Codec: LC3-SWB\n");
    } else {
        PRINTF("  Codec: Unknown (0x%02x)\n", id);
        return;
    }

    err = bt_hfp_hf_select_codec(hf, id);
    if (err != 0) {
        PRINTF("Failed to select codec: %d\n", err);
    }
        /* Store the negotiated codec from phone (when bridge acts as HF) */
    phone_negotiated_codec = id;
    PRINTF("  Stored phone negotiated codec: 0x%02x\n", phone_negotiated_codec);
}
#endif /* CONFIG_BT_HFP_HF_CODEC_NEG */

static struct bt_hfp_hf_cb hf_cb = {
    .connected = hf_connected,
    .disconnected = hf_disconnected,
    .sco_connected = hf_sco_connected,
    .sco_disconnected = hf_sco_disconnected,
    .service = hf_service,
    .outgoing = hf_outgoing,
    .incoming = hf_incoming,
    .accept = hf_accept,
    .reject = hf_reject,
    .terminate = hf_terminate,
    .ring_indication = hf_ring_indication,
#if defined(CONFIG_BT_HFP_HF_CODEC_NEG)
    .codec_negotiate = hf_codec_negotiate,
#endif /* CONFIG_BT_HFP_HF_CODEC_NEG */
};

/* ============================================================================
 * Codec Negotiation Helper Functions
 * ============================================================================ */

/* Select the best mutually supported codec based on priority:
 * 1. LC3-SWB (highest quality, if supported by both)
 * 2. mSBC (better quality than CVSD)
 * 3. CVSD (baseline, always supported)
 */
static uint8_t negotiate_best_codec(uint32_t codecs_a, uint32_t codecs_b)
{
    uint32_t common_codecs = codecs_a & codecs_b;

    PRINTF("Codec negotiation:\n");
    PRINTF("  Device A supported codecs: 0x%08lx\n", codecs_a);
    PRINTF("  Device B supported codecs: 0x%08lx\n", codecs_b);
    PRINTF("  Common codecs: 0x%08lx\n", common_codecs);

    /* Check for LC3-SWB support (highest priority) */
    if (IS_ENABLED(CONFIG_BT_HFP_HF_CODEC_LC3_SWB) &&
        (common_codecs & BIT(BT_HFP_HF_CODEC_LC3_SWB))) {
        PRINTF("  Selected codec: LC3-SWB\n");
        return BT_HFP_HF_CODEC_LC3_SWB;
    }

    /* Check for mSBC support (medium priority) */
    if (IS_ENABLED(CONFIG_BT_HFP_HF_CODEC_MSBC) &&
        (common_codecs & BIT(BT_HFP_HF_CODEC_MSBC))) {
        PRINTF("  Selected codec: mSBC\n");
        return BT_HFP_HF_CODEC_MSBC;
    }

    /* Fall back to CVSD (always supported) */
    PRINTF("  Selected codec: CVSD (fallback)\n");
    return BT_HFP_HF_CODEC_CVSD;
}

/* Get the currently negotiated codec with phone */
uint8_t app_get_negotiated_codec(void)
{
    return phone_negotiated_codec;
}

/* ============================================================================
 * HFP AG Callbacks
 * ============================================================================ */
static void ag_codec(struct bt_hfp_ag *ag, uint32_t ids)
{
    uint8_t codec_to_use;

    PRINTF("AG: Received codec id bitmap from headset: 0x%08lx\n", ids);

    /* Store headset's codec capabilities (when bridge acts as AG) */
    headset_supported_codecs = ids;

    /* Display supported codecs */
    if (ids & BIT(BT_HFP_AG_CODEC_CVSD)) {
        PRINTF("  Headset supports: CVSD\n");
    }
    if (ids & BIT(BT_HFP_AG_CODEC_MSBC)) {
        PRINTF("  Headset supports: mSBC\n");
    }
    if (ids & BIT(BT_HFP_AG_CODEC_LC3_SWB)) {
        PRINTF("  Headset supports: LC3-SWB\n");
    }

    /* If AG SCO establishment was pending, retry now that we have codec capabilities */
    if (pending_ag_sco) {
        PRINTF("  Pending AG SCO detected, establishing now\n");

        /* Determine best codec between phone's negotiated codec and headset capabilities */
        if (phone_negotiated_codec != BT_HFP_HF_CODEC_CVSD && !(ids & BIT(phone_negotiated_codec))) {
            /* Phone negotiated advanced codec but headset doesn't support it */
            PRINTF("  Headset doesn't support phone's codec 0x%02x, selecting best alternative\n",
                   phone_negotiated_codec);
            codec_to_use = negotiate_best_codec(BIT(BT_HFP_HF_CODEC_CVSD) | BIT(phone_negotiated_codec), ids);
        } else {
            /* Use phone's negotiated codec */
            codec_to_use = phone_negotiated_codec;
        }

        establish_ag_sco_with_codec(codec_to_use);
    }
}

static void ag_codec_negotiate(struct bt_hfp_ag *ag, int err)
{
    if (err == 0) {
        PRINTF("AG: Codec negotiation successful with codec 0x%02x\n", phone_negotiated_codec);
    } else {
        PRINTF("AG: Codec negotiation failed: %d\n", err);
    }
}

static void ag_connected(struct bt_conn *conn, struct bt_hfp_ag *ag)
{
    int err;

    g_HfpAgs[g_select_ag_index].hfp_agHandle = ag;
    g_HfpAgs[g_select_ag_index].conn = conn;
    PRINTF("AG: Connected\n");

#if defined(CONFIG_BT_HFP_AG_CODEC_NEG)
    /* Use phone (peer device) codec capabilities for AG side
     * The bridge's AG side should support whatever codecs the phone supports
     * so we can properly bridge the call between phone and headset
     */
    PRINTF("AG: Using phone codec capabilities: 0x%08lx\n", phone_supported_codecs);

    /* If we have both headset and phone codec info, we can preemptively
     * determine the codec to use when SCO is requested
     */
    if (headset_supported_codecs != 0 && phone_supported_codecs != 0) {
        uint8_t selected_codec;
        int err;

        /* Select best codec that both headset and phone support
         * headset_supported_codecs: What the headset (HF) supports (e.g., CVSD only)
         * phone_supported_codecs: What the phone (AG) supports (e.g., CVSD | MSBC)
         * Result should be the intersection (e.g., CVSD)
         */
        selected_codec = negotiate_best_codec(headset_supported_codecs, phone_supported_codecs);
        PRINTF("AG: Best codec between headset and phone: 0x%02x\n", selected_codec);

        /* Update HF side codec to match the selected codec for the bridge */
        if (hfp_hf != NULL) {
            err = bt_hfp_hf_set_codecs(hfp_hf, BIT(selected_codec));
            if (err != 0) {
                PRINTF("AG: Failed to set HF codec 0x%02x: %d\n", selected_codec, err);
            } else {
                PRINTF("AG: Successfully set HF codec to 0x%02x\n", selected_codec);
            }
        } else {
            PRINTF("AG: Warning - HF not connected, cannot set codec\n");
        }
    } else if (phone_negotiated_codec != BT_HFP_HF_CODEC_CVSD) {
        /* Phone has negotiated a codec, but we don't have headset capabilities yet */
        PRINTF("AG: Phone negotiated codec 0x%02x, waiting for headset capabilities\n", phone_negotiated_codec);
    }
#endif
}

static void ag_disconnected(struct bt_hfp_ag *ag)
{
    ag_remove_calls();

    /* Reset AG codec state */
    headset_supported_codecs = 0;
    pending_ag_sco = false;

    g_HfpAgs[g_select_ag_index].hfp_agHandle = NULL;
    g_HfpAgs[g_select_ag_index].conn = NULL;
    PRINTF("AG: Disconnected\n");
}

static void ag_sco_connected(struct bt_hfp_ag *ag, struct bt_conn *sco_conn)
{
    struct bt_conn_info info;
    uint16_t ag_sco_handle = 0;
    uint16_t hf_sco_handle = 0;

    PRINTF("AG: SCO connected %p\n", sco_conn);

    if (bt_hci_get_conn_handle(sco_conn, &ag_sco_handle) == 0) {
        PRINTF("  AG SCO handle 0x%04X\n", ag_sco_handle);
    }

    if (bt_conn_get_info(sco_conn, &info) == 0) {
        PRINTF("  SCO air mode %u\n", info.sco.air_mode);
        PRINTF("  SCO link type %u\n", info.sco.link_type);
    }

    g_HfpAgs[g_select_ag_index].sco_conn = sco_conn;

    /* Get HF SCO handle and establish eSCO bridge */
    if (hf_sco_conn != NULL) {
        if (bt_hci_get_conn_handle(hf_sco_conn, &hf_sco_handle) == 0) {
            PRINTF("  HF SCO handle 0x%04X\n", hf_sco_handle);

            /* Enable eSCO bridge between HF and AG */
            int err = enable_esco_bridge(hf_sco_handle, ag_sco_handle);
            if (err < 0) {
                PRINTF("Failed to enable eSCO bridge: %d\n", err);
            } else {
                PRINTF("eSCO bridge established successfully\n");
                bridge_active = true;
            }
        } else {
            PRINTF("Failed to get HF SCO handle\n");
        }
    } else {
        PRINTF("HF SCO connection not available yet\n");
    }
}

static void ag_sco_disconnected(struct bt_conn *sco_conn, uint8_t reason)
{
    PRINTF("AG: SCO disconnected (reason %u)\n", reason);
    g_HfpAgs[g_select_ag_index].sco_conn = NULL;
}

static int ag_get_indicator_value(struct bt_hfp_ag *ag, uint8_t *service, uint8_t *strength,
                                   uint8_t *roam, uint8_t *battery)
{
    if (service != NULL) {
        *service = 1;
    }
    if (strength != NULL) {
        *strength = 5;
    }
    if (roam != NULL) {
        *roam = 0;
    }
    if (battery != NULL) {
        *battery = 5;
    }
    return 0;
}

static void ag_outgoing(struct bt_hfp_ag *ag, struct bt_hfp_ag_call *call, const char *number)
{
    PRINTF("AG: Outgoing call to %s\n", number);
    ag_add_a_call(call);
}

static void ag_incoming(struct bt_hfp_ag *ag, struct bt_hfp_ag_call *call, const char *number)
{
    PRINTF("AG: Incoming call from %s\n", number);
    ag_add_a_call(call);
}

static void ag_accept(struct bt_hfp_ag_call *call)
{
    PRINTF("AG: Call accepted\n");
}

static void ag_reject(struct bt_hfp_ag_call *call)
{
    PRINTF("AG: Call rejected\n");
    ag_remove_a_call(call);
}

static void ag_terminate(struct bt_hfp_ag_call *call)
{
    PRINTF("AG: Call terminated\n");
    ag_remove_a_call(call);
}

static struct bt_hfp_ag_cb ag_cb = {
    .connected = ag_connected,
    .disconnected = ag_disconnected,
    .sco_connected = ag_sco_connected,
    .sco_disconnected = ag_sco_disconnected,
    .get_indicator_value = ag_get_indicator_value,
    .outgoing = ag_outgoing,
    .incoming = ag_incoming,
    .accept = ag_accept,
    .reject = ag_reject,
    .terminate = ag_terminate,
    .codec = ag_codec,
    .codec_negotiate = ag_codec_negotiate,
};

/* ============================================================================
 * Authentication Callbacks
 * ============================================================================ */
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

/* ============================================================================
 * HFP AG Registration (on-demand)
 * ============================================================================ */
int app_hfp_ag_ensure_registered(void)
{
    int err;

    if (ag_registered) {
        return 0;  /* Already registered */
    }

    PRINTF("Registering HFP AG profile for incoming connections...\n");
    err = bt_hfp_ag_register(&ag_cb);
    if (err < 0) {
        PRINTF("HFP AG register failed (err %d)\n", err);
        return err;
    }

    ag_registered = true;
    PRINTF("HFP AG profile registered successfully\n");
    return 0;
}

/* ============================================================================
 * HFP HF Command Functions (for shell)
 * ============================================================================ */
void hfp_hf_dial(const char *number)
{
    int err;

    if (!hfp_hf) {
        PRINTF("HF not connected\n");
        return;
    }

    err = bt_hfp_hf_number_call(hfp_hf, number);
    if (err < 0) {
        PRINTF("Failed to dial: %d\n", err);
    } else {
        PRINTF("Dialing %s\n", number);
    }
}

void hfp_hf_accept_call(uint8_t index)
{
    int err;

    if (index >= CONFIG_BT_HFP_HF_MAX_CALLS || !hfp_hf_call[index]) {
        PRINTF("No call at index %u\n", index);
        return;
    }

    err = bt_hfp_hf_accept(hfp_hf_call[index]);
    if (err < 0) {
        PRINTF("Failed to accept call: %d\n", err);
    } else {
        PRINTF("Call accepted\n");
    }
}

void hfp_hf_reject_call(uint8_t index)
{
    int err;

    if (index >= CONFIG_BT_HFP_HF_MAX_CALLS || !hfp_hf_call[index]) {
        PRINTF("No call at index %u\n", index);
        return;
    }

    /* Try to reject incoming call first, if that fails try to terminate active call */
    err = bt_hfp_hf_reject(hfp_hf_call[index]);
    if (err < 0) {
        /* If reject fails, try terminate (for active calls) */
        err = bt_hfp_hf_terminate(hfp_hf_call[index]);
        if (err < 0) {
            PRINTF("Failed to end/reject call: %d\n", err);
        } else {
            PRINTF("Call ended\n");
        }
    } else {
        PRINTF("Call rejected\n");
    }
}

void hfp_hf_query_list_current_calls(void)
{
    int err;

    if (!hfp_hf) {
        PRINTF("HF not connected\n");
        return;
    }

    err = bt_hfp_hf_query_list_of_current_calls(hfp_hf);
    if (err < 0) {
        PRINTF("Failed to query calls: %d\n", err);
    } else {
        PRINTF("Query calls list\n");
    }
}

void hfp_hf_volume_update(hf_volume_type_t type, int volume)
{
    int err;

    if (!hfp_hf) {
        PRINTF("HF not connected\n");
        return;
    }

    if (type == hf_volume_type_mic) {
        err = bt_hfp_hf_vgm(hfp_hf, (uint8_t)volume);
        if (err < 0) {
            PRINTF("Failed to update mic volume: %d\n", err);
        } else {
            PRINTF("Mic volume updated to %d\n", volume);
        }
    } else if (type == hf_volume_type_speaker) {
        err = bt_hfp_hf_vgs(hfp_hf, (uint8_t)volume);
        if (err < 0) {
            PRINTF("Failed to update speaker volume: %d\n", err);
        } else {
            PRINTF("Speaker volume updated to %d\n", volume);
        }
    }
}

void hfp_hf_last_dial(void)
{
    int err;

    if (!hfp_hf) {
        PRINTF("HF not connected\n");
        return;
    }

    err = bt_hfp_hf_redial(hfp_hf);
    if (err < 0) {
        PRINTF("Failed to redial: %d\n", err);
    } else {
        PRINTF("Redialing last number\n");
    }
}

void hfp_hf_terminate(void)
{
    int err;

    if (!hfp_hf_call[0]) {
        PRINTF("No active call to terminate\n");
        return;
    }

    err = bt_hfp_hf_terminate(hfp_hf_call[0]);
    if (err < 0) {
        PRINTF("Failed to terminate call: %d\n", err);
    } else {
        PRINTF("Call terminated\n");
    }
}

/* ============================================================================
 * Bluetooth Ready Callback
 * ============================================================================ */
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

    /* Initialize connection management */
    app_connect_init();
    app_discover_init();

    /* Set BR/EDR connectable and discoverable */
    err = bt_br_set_connectable(true, NULL);
    if (err)
    {
        PRINTF("BR/EDR set connectable failed (err %d)\n", err);
        return;
    }
    err = bt_br_set_discoverable(true, false);
    if (err)
    {
        PRINTF("BR/EDR set discoverable failed (err %d)\n", err);
        return;
    }
    PRINTF("BR/EDR set connectable and discoverable done\n");

    /* Register HFP HF profile at startup (for outgoing connections to AG devices) */
    err = bt_hfp_hf_register(&hf_cb);
    if (err)
    {
        PRINTF("HFP HF register failed (err %d)\n", err);
        return;
    }
    PRINTF("HFP HF profile registered\n");

    /* Initialize shell commands */
    app_shell_init();

    PRINTF("SCO Bridge ready\n");
}

/* ============================================================================
 * Main Task
 * ============================================================================ */
void app_sco_bridge_task(void *param)
{
    int err;

    PRINTF("\n");
    PRINTF("******************************\n");
    PRINTF("*  SCO Bridge Demo Start     *\n");
    PRINTF("******************************\n");
    PRINTF("\n");

    /* Register authentication callbacks before bt_enable */
    err = bt_conn_auth_cb_register(&auth_cb_display);
    if (err)
    {
        PRINTF("Failed to register authentication callbacks (err %d)\n", err);
        return;
    }

    /* Initialize Bluetooth with bt_ready callback */
    err = bt_enable(bt_ready);
    if (err)
    {
        PRINTF("Bluetooth init failed (err %d)\n", err);
        return;
    }

    /* Task completed, delete itself */
    vTaskDelete(NULL);
}

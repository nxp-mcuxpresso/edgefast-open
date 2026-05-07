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
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include "app_pbap_pce.h"
#include "app_connect.h"
#include "app_discover.h"

#define LOCAL_SRM_ENABLED  BIT(0)
#define PEER_SRM_ENABLED   BIT(1)
#define LOCAL_SRMP_ENABLED BIT(2)
#define PEER_SRMP_ENABLED  BIT(3)

static bool is_srm_enable(struct app_pbap_pce *pbap_pce)
{
    if (!pbap_pce->goep_v2)
    {
        return false;
    }

    return ((pbap_pce->srm_srmp & LOCAL_SRM_ENABLED) != 0) && ((pbap_pce->srm_srmp & PEER_SRM_ENABLED) != 0);
}

static bool wait_to_send_cmd(struct app_pbap_pce *pbap_pce)
{
    if (!is_srm_enable(pbap_pce))
    {
        return true;
    }

    return ((pbap_pce->srm_srmp & LOCAL_SRMP_ENABLED) != 0) || ((pbap_pce->srm_srmp & PEER_SRMP_ENABLED) != 0);
}

enum __packed bt_pbap_app_state
{
    PBAP_STATE_CONNECTED                            = 0,
    PBAP_STATE_DISCONNECTED_END                     = 1,
    PBAP_STATE_PULL_PHONEPHOOK_END                  = 2,
    PBAP_STATE_SET_PHONE_BOOK_FOR_VCARD_LISTING_END = 3,
    PBAP_STATE_SET_PHONE_BOOK_FOR_VCARD_ENTRY_END   = 4,
    PBAP_STATE_PULL_VCARD_LISTING_END               = 5,
    PBAP_STATE_PULL_VCARD_ENTRY_END                 = 6,
};

struct app_pbap_pce app_pce;

#define PBAP_PCE_SUPPORTED_FEATURES                                                                      \
    (BT_PBAP_SUPPORTED_FEATURE_DOWNLOAD | BT_PBAP_SUPPORTED_FEATURE_BROWSING |                           \
     BT_PBAP_SUPPORTED_FEATURE_DATABASE_IDENTIFIER | BT_PBAP_SUPPORTED_FEATURE_FOLDER_VERSION_COUNTERS | \
     BT_PBAP_SUPPORTED_FEATURE_VCARD_SELECTOR | BT_PBAP_SUPPORTED_FEATURE_ENHANCED_MISSED_CALLS |        \
     BT_PBAP_SUPPORTED_FEATURE_UCI_VCARD_PROPERTY | BT_PBAP_SUPPORTED_FEATURE_UID_VCARD_PROPERTY |       \
     BT_PBAP_SUPPORTED_FEATURE_CONTACT_REFERENCING | BT_PBAP_SUPPORTED_FEATURE_DEFAULT_CONTACT_IMAGE)

#define PBAP_PCE_APP_MOPL 300
#define PBAP_POOL_BUF_SIZE \
    MAX(BT_RFCOMM_BUF_SIZE(CONFIG_BT_GOEP_RFCOMM_MTU), BT_L2CAP_BUF_SIZE(CONFIG_BT_GOEP_L2CAP_MTU))

NET_BUF_POOL_FIXED_DEFINE(tx_pool, CONFIG_BT_MAX_CONN, PBAP_POOL_BUF_SIZE, CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static struct bt_sdp_attribute pbap_pce_attrs[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
			BT_SDP_ARRAY_16(BT_SDP_PBAP_PCE_SVCLASS)
		},
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROFILE_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 8),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_PBAP_SVCLASS)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(0x0102)
			},
			)
		},
		)
	),
	BT_SDP_SERVICE_NAME("Phonebook Access PCE"),
};

static struct bt_sdp_record pbap_pce_rec = BT_SDP_RECORD(pbap_pce_attrs);

struct pbap_pce_test_state
{
    enum bt_pbap_app_state current_state;
    char *name;
    void (*pbap_pce_test_func)(struct app_pbap_pce *pce, char *name);
};

static uint8_t pbap_pce_ascii_to_unicode(const char *src, uint8_t *dest, uint16_t dest_size, bool big_endian)
{
    uint8_t src_len;
    uint8_t required_size;
    uint8_t i;

    if (src == NULL || dest == NULL)
    {
        return 0;
    }

    src_len       = strlen(src);
    required_size = (src_len + 1U) * 2U;

    if (dest_size < required_size)
    {
        printk("Buffer too small: required %u, available %u", required_size, dest_size);
        return 0;
    }

    if (big_endian)
    {
        for (i = 0; i < src_len; i++)
        {
            dest[i * 2U]      = 0x00;
            dest[i * 2U + 1U] = (uint8_t)src[i];
        }
    }
    else
    {
        for (i = 0; i < src_len; i++)
        {
            dest[i * 2U]      = (uint8_t)src[i];
            dest[i * 2U + 1U] = 0x00;
        }
    }

    dest[src_len * 2]     = 0x00;
    dest[src_len * 2 + 1] = 0x00;

    return required_size;
}

static int pbap_alloc_tx_buf(struct app_pbap_pce *app_pce)
{
    if (app_pce->tx_buf != NULL)
    {
        printk("Buf %p is using\n", app_pce->tx_buf);
        return -EBUSY;
    }

    app_pce->tx_buf = bt_pbap_pce_create_pdu(&app_pce->pbap_pce, &tx_pool);

    if (app_pce->tx_buf == NULL)
    {
        printk("Fail to allocate tx buffer\n");
        return -ENOBUFS;
    }
    return 0;
}

static int pbap_pce_add_initial_headers(struct app_pbap_pce *app_pce, const char *type, const char *name)
{
    int err;
    int length;
    uint8_t unicode_trans[60] = {0};

    app_pce->srm_srmp   = 0;
    app_pce->first_recv = true;

    err = pbap_alloc_tx_buf(app_pce);
    if (err != 0)
    {
        printk("Fail to allocate tx buffer %d\n", err);
        goto failed;
    }

    err = bt_obex_add_header_conn_id(app_pce->tx_buf, app_pce->conn_id);
    if (err != 0)
    {
        printk("Fail to add connection id %d", err);
        goto failed;
    }

    if (app_pce->goep_v2)
    {
        err = bt_obex_add_header_srm(app_pce->tx_buf, 0x01);
        if (err != 0)
        {
            printk("Fail to add srm header %d", err);
            goto failed;
        }
        app_pce->srm_srmp |= LOCAL_SRM_ENABLED;
    }

    length = pbap_pce_ascii_to_unicode(name, unicode_trans, sizeof(unicode_trans), true);
    err    = bt_obex_add_header_name(app_pce->tx_buf, length, unicode_trans);
    if (err != 0)
    {
        printk("Fail to add name header %d", err);
        goto failed;
    }

    /* Add type header */
    err = bt_obex_add_header_type(app_pce->tx_buf, strlen(type) + 1U, type);
    if (err != 0)
    {
        printk("Fail to add type header %d", err);
        goto failed;
    }

    return 0;

failed:
    net_buf_unref(app_pce->tx_buf);
    app_pce->tx_buf = NULL;
    return err;
}

static void pbap_pce_test_func_pull_phone_book(struct app_pbap_pce *app_pce, char *name)
{
    int err;

    err = pbap_pce_add_initial_headers(app_pce, BT_PBAP_PULL_PHONE_BOOK_TYPE, name);
    if (err != 0)
    {
        return;
    }

    err = bt_pbap_pce_pull_phone_book(&app_pce->pbap_pce, app_pce->tx_buf);
    if (err != 0)
    {
        printk("Fail to pull phonebook %d\n", err);
    }

    app_pce->tx_buf = NULL;
}

static void pbap_pce_test_func_set_phone_book(struct app_pbap_pce *app_pce, char *name)
{
    uint16_t len = 0;
    int err;
    uint8_t unicode_trans[50] = {0};

    err = pbap_alloc_tx_buf(app_pce);
    if (err != 0)
    {
        printk("Fail to allocate tx buffer %d\n", err);
        return;
    }

    len = pbap_pce_ascii_to_unicode((uint8_t *)name, (uint8_t *)unicode_trans, sizeof(unicode_trans), true);
    err = bt_obex_add_header_conn_id(app_pce->tx_buf, app_pce->conn_id);
    if (err != 0)
    {
        printk("Fail to add connection id %d", err);
        goto failed;
    }

    err = bt_obex_add_header_name(app_pce->tx_buf, len, unicode_trans);
    if (err != 0)
    {
        printk("Fail to add name header %d", err);
        goto failed;
    }

    err = bt_pbap_pce_set_phone_book(&app_pce->pbap_pce, BT_PBAP_SET_PHONE_BOOK_FLAGS_DOWN_OR_ROOT, app_pce->tx_buf);
    if (err != 0)
    {
        printk("Fail to create set path cmd %d", err);
        goto failed;
    }

    app_pce->tx_buf = NULL;
    return;
failed:
    net_buf_unref(app_pce->tx_buf);
    app_pce->tx_buf = NULL;
}

static void pbap_pce_test_func_pull_vcard_listing(struct app_pbap_pce *app_pce, char *name)
{
    int err;

    err = pbap_pce_add_initial_headers(app_pce, BT_PBAP_PULL_VCARD_LISTING_TYPE, name);
    if (err != 0)
    {
        return;
    }

    err = bt_pbap_pce_pull_vcard_listing(&app_pce->pbap_pce, app_pce->tx_buf);
    if (err != 0)
    {
        printk("Fail to pull vcard listing %d\n", err);
    }

    app_pce->tx_buf = NULL;
}

static void pbap_pce_test_func_pull_vcard_entry(struct app_pbap_pce *app_pce, char *name)
{
    int err;

    err = pbap_pce_add_initial_headers(app_pce, BT_PBAP_PULL_VCARD_ENTRY_TYPE, name);
    if (err != 0)
    {
        return;
    }

    err = bt_pbap_pce_pull_vcard_entry(&app_pce->pbap_pce, app_pce->tx_buf);
    if (err != 0)
    {
        printk("Fail to pull vcard entry %d\n", err);
    }

    app_pce->tx_buf = NULL;
}

#define PBAP_PCE_PULL_PHONE_BOOK_NAME                  "telecom/pb.vcf"
#define PBAP_PCE_PULL_VCARD_LISTING_NAME               "pb"
#define PBAP_PCE_SET_PHONE_BOOK_NAME_FOR_VCARD_LISTING "telecom"
#define PBAP_PCE_SET_PHONE_BOOK_NAME_FOR_VCARD_ENTRY   "pb"
#define PBAP_PCE_PULL_VCARD_ENTRY_NAME                 "0.vcf"

struct pbap_pce_test_state test_case[] = {
    {PBAP_STATE_CONNECTED, PBAP_PCE_PULL_PHONE_BOOK_NAME, pbap_pce_test_func_pull_phone_book},
    {PBAP_STATE_PULL_PHONEPHOOK_END, PBAP_PCE_SET_PHONE_BOOK_NAME_FOR_VCARD_LISTING, pbap_pce_test_func_set_phone_book},
    {PBAP_STATE_SET_PHONE_BOOK_FOR_VCARD_LISTING_END, PBAP_PCE_PULL_VCARD_LISTING_NAME,
     pbap_pce_test_func_pull_vcard_listing},
    {PBAP_STATE_PULL_VCARD_LISTING_END, PBAP_PCE_SET_PHONE_BOOK_NAME_FOR_VCARD_ENTRY,
     pbap_pce_test_func_set_phone_book},
    {PBAP_STATE_SET_PHONE_BOOK_FOR_VCARD_ENTRY_END, PBAP_PCE_PULL_VCARD_ENTRY_NAME,
     pbap_pce_test_func_pull_vcard_entry},
};

static void pbap_pce_process_test(void)
{
    uint8_t i;

    for (i = 0; i < ARRAY_SIZE(test_case); i++)
    {
        if (app_pce.current_state == test_case[i].current_state)
        {
            if (test_case[i].pbap_pce_test_func != NULL)
            {
                test_case[i].pbap_pce_test_func(&app_pce, test_case[i].name);
            }
            break;
        }
    }
}

static void pbap_pce_connected(
    struct bt_pbap_pce *pbap_pce, uint8_t rsp_code, uint8_t version, uint16_t mopl, struct net_buf *buf)
{
    int err;
    struct app_pbap_pce *app_pce = CONTAINER_OF(pbap_pce, struct app_pbap_pce, pbap_pce);

    printk("pbap connect result %s, mopl %d\n", bt_obex_rsp_code_to_str(rsp_code), mopl);
    err = bt_obex_get_header_conn_id(buf, &app_pce->conn_id);
    if (err != 0)
    {
        printk("No connection ID header found (err: %d)", err);
        return;
    }
    app_pce->current_state        = PBAP_STATE_CONNECTED;
    app_pce->set_phone_book_count = 0;
    app_pce->first_recv           = true;
    app_pce->srm_srmp             = 0;
    pbap_pce_process_test();
}

struct pbap_hdr
{
    const uint8_t *value;
    uint16_t length;
};

static int pbap_check_srm(struct net_buf *buf)
{
    uint8_t srm;
    int err;

    err = bt_obex_get_header_srm(buf, &srm);
    if (err != 0)
    {
        printk("Failed to get header SRM %d", err);
        return err;
    }

    if (srm != 0x01)
    {
        printk("SRM mismatched %u != 0x01", srm);
        return -EINVAL;
    }

    return 0;
}

static int pbap_check_srmp(struct net_buf *buf)
{
    uint8_t srmp;
    int err;

    err = bt_obex_get_header_srm_param(buf, &srmp);
    if (err != 0)
    {
        return err;
    }

    if (srmp != 0x01)
    {
        return -EINVAL;
    }

    return 0;
}

static int pce_get_body_data(uint8_t rsp_code, struct net_buf *buf, struct pbap_hdr *body)
{
    int err;

    if (rsp_code == BT_PBAP_RSP_CODE_CONTINUE)
    {
        err = bt_obex_get_header_body(buf, &body->length, &body->value);
    }
    else
    {
        err = bt_obex_get_header_end_body(buf, &body->length, &body->value);
    }

    return err;
}

static void pbap_pce_pull_phone_book_cb(struct bt_pbap_pce *pbap_pce, uint8_t rsp_code, struct net_buf *buf)
{
    struct pbap_hdr body;
    int err;
    struct app_pbap_pce *app_pce = CONTAINER_OF(pbap_pce, struct app_pbap_pce, pbap_pce);

    body.length = 0;
    body.value  = NULL;

    printk("pbap pull phone book result %s\n", bt_obex_rsp_code_to_str(rsp_code));

    if (rsp_code != BT_PBAP_RSP_CODE_SUCCESS && rsp_code != BT_PBAP_RSP_CODE_CONTINUE)
    {
        return;
    }

    if (app_pce->goep_v2)
    {
        if (app_pce->first_recv)
        {
            err = pbap_check_srm(buf);
            if (err != 0)
            {
                return;
            }
            app_pce->srm_srmp |= PEER_SRM_ENABLED;
        }

        app_pce->srm_srmp &= ~PEER_SRMP_ENABLED;
        err = pbap_check_srmp(buf);
        if (err == 0)
        {
            app_pce->srm_srmp |= PEER_SRMP_ENABLED;
        }
    }

    pce_get_body_data(rsp_code, buf, &body);
    if (body.length > 0 && body.value != NULL)
    {
        printk("=========body=========\n");
        printk("%.*s\n", body.length, body.value);
        printk("=========body=========\n");
    }

    if (rsp_code == BT_PBAP_RSP_CODE_SUCCESS)
    {
        app_pce->current_state = PBAP_STATE_PULL_PHONEPHOOK_END;
        pbap_pce_process_test();
    }
    else
    {
        if (wait_to_send_cmd(app_pce))
        {
            err = pbap_alloc_tx_buf(app_pce);
            if (err != 0)
            {
                printk("Fail to allocate tx buffer %d\n", err);
                return;
            }

            err = bt_pbap_pce_pull_phone_book(&app_pce->pbap_pce, app_pce->tx_buf);
            if (err != 0)
            {
                net_buf_unref(app_pce->tx_buf);
                printk("Fail to pull phonebook %d\n", err);
            }
            app_pce->tx_buf = NULL;
            return;
        }
    }
    app_pce->first_recv = false;
}

static void pbap_pce_pull_vcard_listing_cb(struct bt_pbap_pce *pbap_pce, uint8_t rsp_code, struct net_buf *buf)
{
    struct pbap_hdr body;
    int err;
    struct app_pbap_pce *app_pce = CONTAINER_OF(pbap_pce, struct app_pbap_pce, pbap_pce);

    body.length = 0;
    body.value  = NULL;

    printk("pbap pull vcard listing result %s\n", bt_obex_rsp_code_to_str(rsp_code));
    if (rsp_code != BT_PBAP_RSP_CODE_SUCCESS && rsp_code != BT_PBAP_RSP_CODE_CONTINUE)
    {
        return;
    }

    if (app_pce->goep_v2)
    {
        if (app_pce->first_recv)
        {
            err = pbap_check_srm(buf);
            if (err != 0)
            {
                return;
            }
            app_pce->srm_srmp |= PEER_SRM_ENABLED;
        }

        app_pce->srm_srmp &= ~PEER_SRMP_ENABLED;
        err = pbap_check_srmp(buf);
        if (err == 0)
        {
            app_pce->srm_srmp |= PEER_SRMP_ENABLED;
        }
    }

    pce_get_body_data(rsp_code, buf, &body);
    if (body.length > 0 && body.value != NULL)
    {
        printk("=========body=========\n");
        printk("%.*s\n", body.length, body.value);
        printk("=========body=========\n");
    }

    if (rsp_code == BT_PBAP_RSP_CODE_SUCCESS)
    {
        app_pce->current_state = PBAP_STATE_PULL_VCARD_LISTING_END;
        pbap_pce_process_test();
    }
    else
    {
        if (wait_to_send_cmd(app_pce))
        {
            err = pbap_alloc_tx_buf(app_pce);
            if (err != 0)
            {
                printk("Fail to allocate tx buffer %d\n", err);
                return;
            }

            err = bt_pbap_pce_pull_vcard_listing(&app_pce->pbap_pce, app_pce->tx_buf);
            if (err != 0)
            {
                net_buf_unref(app_pce->tx_buf);
                printk("Fail to pull vcard listing %d\n", err);
            }
            app_pce->tx_buf = NULL;
            return;
        }
    }
    app_pce->first_recv = false;
}

static void pbap_pce_pull_vcard_entry_cb(struct bt_pbap_pce *pbap_pce, uint8_t rsp_code, struct net_buf *buf)
{
    struct pbap_hdr body;
    int err;
    struct app_pbap_pce *app_pce = CONTAINER_OF(pbap_pce, struct app_pbap_pce, pbap_pce);

    body.length = 0;
    body.value  = NULL;

    printk("pbap pull vcard entry result %s\n", bt_obex_rsp_code_to_str(rsp_code));
    if (rsp_code != BT_PBAP_RSP_CODE_SUCCESS && rsp_code != BT_PBAP_RSP_CODE_CONTINUE)
    {
        return;
    }

    if (app_pce->goep_v2)
    {
        if (app_pce->first_recv)
        {
            err = pbap_check_srm(buf);
            if (err != 0)
            {
                return;
            }
            app_pce->srm_srmp |= PEER_SRM_ENABLED;
        }

        app_pce->srm_srmp &= ~PEER_SRMP_ENABLED;
        err = pbap_check_srmp(buf);
        if (err == 0)
        {
            app_pce->srm_srmp |= PEER_SRMP_ENABLED;
        }
    }

    pce_get_body_data(rsp_code, buf, &body);
    if (body.length > 0 && body.value != NULL)
    {
        printk("=========body=========\n");
        printk("%.*s\n", body.length, body.value);
        printk("=========body=========\n");
    }

    if (rsp_code == BT_PBAP_RSP_CODE_SUCCESS)
    {
        err = bt_pbap_pce_disconnect(&app_pce->pbap_pce, NULL);
        if (err != 0)
        {
            printk("Fail to disconnect %d\n", err);
        }
    }
    else
    {
        if (wait_to_send_cmd(app_pce))
        {
            err = pbap_alloc_tx_buf(app_pce);
            if (err != 0)
            {
                printk("Fail to allocate tx buffer %d\n", err);
                return;
            }
            err = bt_pbap_pce_pull_vcard_entry(&app_pce->pbap_pce, app_pce->tx_buf);
            if (err != 0)
            {
                net_buf_unref(app_pce->tx_buf);
                printk("Fail to pull vcard entry %d\n", err);
            }
            app_pce->tx_buf = NULL;
            return;
        }
    }
    app_pce->first_recv = false;
}

static void pbap_pce_set_phone_book_cb(struct bt_pbap_pce *pbap_pce, uint8_t rsp_code, struct net_buf *buf)
{
    struct app_pbap_pce *app_pce = CONTAINER_OF(pbap_pce, struct app_pbap_pce, pbap_pce);

    printk("PBAP set phonebook result %s\n", bt_obex_rsp_code_to_str(rsp_code));
    if (app_pce->set_phone_book_count == 0)
    {
        app_pce->set_phone_book_count++;
        app_pce->current_state = PBAP_STATE_SET_PHONE_BOOK_FOR_VCARD_LISTING_END;
    }
    else
    {
        app_pce->set_phone_book_count = 0;
        app_pce->current_state        = PBAP_STATE_SET_PHONE_BOOK_FOR_VCARD_ENTRY_END;
    }
    pbap_pce_process_test();
}

static void pbap_pce_disconnected(struct bt_pbap_pce *pbap, uint8_t rsp_code, struct net_buf *buf)
{
    printk("pbap disconnect result %s\n", bt_obex_rsp_code_to_str(rsp_code));
}

static int pbap_pce_obex_connect(struct app_pbap_pce *app_pbap_pce, uint16_t mopl)
{
    int err;
    struct bt_obex_tlv appl_param;

    err = pbap_alloc_tx_buf(app_pbap_pce);
    if (err != 0)
    {
        printk("Fail to allocate tx buffer %d\n", err);
        return err;
    }

    printk("create pbap obex connection\n");

    if (app_pbap_pce->goep_v2)
    {
        uint32_t supported_features = PBAP_PCE_SUPPORTED_FEATURES;
        appl_param.type             = BT_PBAP_APPL_PARAM_TAG_ID_SUPPORTED_FEATURES;
        appl_param.data_len         = 4;
        appl_param.data             = (uint8_t *)&supported_features;
        err                         = bt_obex_add_header_app_param(app_pbap_pce->tx_buf, 1, &appl_param);
        if (err != 0)
        {
            printk("Fail to add application parameter %d\n", err);
            return err;
        }
    }

    err = bt_pbap_pce_connect(&app_pbap_pce->pbap_pce, mopl, app_pbap_pce->tx_buf);
    if (err != 0)
    {
        printk("Fail to connect %d\n", err);
    }

    app_pbap_pce->tx_buf = NULL;
    return err;
}

static void pbap_pce_rfcomm_transport_connected(struct bt_conn *conn, struct bt_pbap_pce *pbap_pce)
{
    int err;
    struct app_pbap_pce *app_pce = CONTAINER_OF(pbap_pce, struct app_pbap_pce, pbap_pce);

    app_pce->goep_v2 = false;
    printk("PBAP PCE %p rfcomm transport connected on %p\n", pbap_pce, conn);
    err = pbap_pce_obex_connect(app_pce, PBAP_PCE_APP_MOPL);
    if (err != 0)
    {
        printk("Fail to obex connect %d\n", err);
    }

    return;
}

static void pbap_pce_rfcomm_transport_disconnected(struct bt_pbap_pce *pbap_pce)
{
    struct app_pbap_pce *app_pce = CONTAINER_OF(pbap_pce, struct app_pbap_pce, pbap_pce);

    printk("PBAP PCE %p rfcomm transport disconnected\n", pbap_pce);
    app_pce->tx_buf = NULL;
}

static void pbap_pce_l2cap_transport_connected(struct bt_conn *conn, struct bt_pbap_pce *pbap_pce)
{
    int err;
    struct app_pbap_pce *app_pce = CONTAINER_OF(pbap_pce, struct app_pbap_pce, pbap_pce);

    app_pce->goep_v2 = true;
    printk("PBAP PCE %p l2cap transport connected on %p\n", pbap_pce, conn);
    err = pbap_pce_obex_connect(app_pce, PBAP_PCE_APP_MOPL);
    if (err != 0)
    {
        printk("Fail to obex connect %d\n", err);
    }

    return;
}

static void pbap_pce_l2cap_transport_disconnected(struct bt_pbap_pce *pbap_pce)
{
    struct app_pbap_pce *app_pce = CONTAINER_OF(pbap_pce, struct app_pbap_pce, pbap_pce);

    printk("PBAP PCE %p l2cap transport disconnected\n", pbap_pce);
    app_pce->tx_buf = NULL;
}

struct bt_pbap_pce_cb pce_cb = {
    .rfcomm_connected    = pbap_pce_rfcomm_transport_connected,
    .rfcomm_disconnected = pbap_pce_rfcomm_transport_disconnected,
    .l2cap_connected     = pbap_pce_l2cap_transport_connected,
    .l2cap_disconnected  = pbap_pce_l2cap_transport_disconnected,
    .connect             = pbap_pce_connected,
    .disconnect          = pbap_pce_disconnected,
    .pull_phone_book     = pbap_pce_pull_phone_book_cb,
    .pull_vcard_listing  = pbap_pce_pull_vcard_listing_cb,
    .pull_vcard_entry    = pbap_pce_pull_vcard_entry_cb,
    .set_phone_book      = pbap_pce_set_phone_book_cb,
};

static void bt_ready(int err)
{
    if (err != 0)
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

    err = bt_sdp_register_service(&pbap_pce_rec);
    if (err != 0)
    {
        printk("Failed to register SDP record");
        return;
    }

    err = bt_br_set_connectable(true, NULL);
    if (err != 0)
    {
        PRINTF("BR/EDR set/rest connectable failed (err %d)\n", err);
        return;
    }

    err = bt_br_set_discoverable(true, false);
    if (err != 0)
    {
        PRINTF("BR/EDR set discoverable failed (err %d)\n", err);
        return;
    }
    PRINTF("BR/EDR set connectable and discoverable done\n");

    app_discover();
}

void pbap_pce_task(void *pvParameters)
{
    int err = 0;

    PRINTF("Bluetooth PBAP PCE demo start...\n");

    /* Initializate BT Host stack */
    err = bt_enable(bt_ready);
    if (err != 0)
    {
        PRINTF("Bluetooth init failed (err %d)\n", err);
        return;
    }
    vTaskDelete(NULL);
}

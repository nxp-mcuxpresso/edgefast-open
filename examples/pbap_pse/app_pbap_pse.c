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
#include <zephyr/bluetooth/classic/pbap.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include "app_pbap_pse.h"
#include "app_connect.h"

static uint8_t rfcomm_channel         = CONFIG_BT_PBAP_PSE_RFCOMM_CHANNEL;
static uint16_t l2cap_psm             = CONFIG_BT_PBAP_PSE_L2CAP_PSM;
static uint8_t supported_repositories = CONFIG_BT_PBAP_PSE_SUPPORTED_REPOSITORIES;
static uint32_t supported_features    = CONFIG_BT_PBAP_PSE_SUPPORTED_FEATURES;

static struct bt_pbap_pse_l2cap l2cap_server;
static struct bt_pbap_pse_rfcomm rfcomm_server;
struct bt_conn *default_conn;

static atomic_t abort_req = ATOMIC_INIT(0);

#define PSE_SEND_DATA_LENGTH_MAX 100
#define PBAP_POOL_BUF_SIZE \
    MAX(BT_RFCOMM_BUF_SIZE(CONFIG_BT_GOEP_RFCOMM_MTU), BT_L2CAP_BUF_SIZE(CONFIG_BT_GOEP_L2CAP_MTU))

NET_BUF_POOL_FIXED_DEFINE(pse_tx_pool, CONFIG_BT_MAX_CONN, PBAP_POOL_BUF_SIZE, CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static TaskHandle_t data_send_task_handle;
static StackType_t data_send_task_stack[CONFIG_BT_PBAP_PSE_DATA_SEND_TASK_STACK_SIZE];
static StaticTask_t data_send_task_tcb;

#define LOCAL_SRM_ENABLED  BIT(0)
#define PEER_SRM_ENABLED   BIT(1)
#define LOCAL_SRMP_ENABLED BIT(2)
#define PEER_SRMP_ENABLED  BIT(3)

typedef int (*operation_func)(struct bt_pbap_pse *pbap_pse, uint8_t rsp_code, struct net_buf *buf);
struct rsp_data_operation
{
    char *data;
    uint16_t sent_length;
    uint16_t remain_length;
    operation_func func;
};

struct app_pbap_pse
{
    struct bt_pbap_pse pbap_pse;
    uint32_t conn_id;
    bool goep_v2;
    bool first_recv;
    uint8_t srm_srmp;
    struct net_buf *tx_buf;
    struct rsp_data_operation operation;
};
static struct app_pbap_pse app_pse;

static bool is_srm_enable(struct app_pbap_pse *pbap_pse)
{
    if (!pbap_pse->goep_v2)
    {
        return false;
    }

    return ((pbap_pse->srm_srmp & LOCAL_SRM_ENABLED) != 0) && ((pbap_pse->srm_srmp & PEER_SRM_ENABLED) != 0);
}

static bool wait_to_send_cmd(struct app_pbap_pse *pbap_pse)
{
    if (!is_srm_enable(pbap_pse))
    {
        return true;
    }

    return ((pbap_pse->srm_srmp & LOCAL_SRMP_ENABLED) != 0) || ((pbap_pse->srm_srmp & PEER_SRMP_ENABLED) != 0);
}

static int pbap_check_srm(struct net_buf *buf)
{
    uint8_t srm;
    int err;

    err = bt_obex_get_header_srm(buf, &srm);
    if (err != 0)
    {
        PRINTF("Failed to get header SRM %d", err);
        return err;
    }

    if (srm != 0x01)
    {
        PRINTF("SRM mismatched %u != 0x01", srm);
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

static struct bt_sdp_attribute pbap_pse_attrs[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
			BT_SDP_ARRAY_16(BT_SDP_PBAP_PSE_SVCLASS)
		},
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROTO_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 17),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_PROTO_L2CAP)
			},
			)
		},
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 5),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_PROTO_RFCOMM)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT8),
				&rfcomm_channel
			},
			)
		},
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_PROTO_OBEX)
			},
			)
		},
		)
	),
	BT_SDP_SERVICE_NAME("Phonebook Access PSE"),
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
	{
		BT_SDP_ATTR_GOEP_L2CAP_PSM,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
			&l2cap_psm
		}
	},
	{
		BT_SDP_ATTR_SUPPORTED_REPOSITORIES,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT8),
			&supported_repositories
		}
	},
	{
		BT_SDP_ATTR_PBAP_SUPPORTED_FEATURES,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT32),
			&supported_features
		}
	},
};

static struct bt_sdp_record pbap_pse_rec = BT_SDP_RECORD(pbap_pse_attrs);

static int pbap_alloc_tx_buf(struct app_pbap_pse *app_pse)
{
    if (app_pse->tx_buf != NULL)
    {
        PRINTF("Buf %p is using\n", app_pse->tx_buf);
        return -EBUSY;
    }

    app_pse->tx_buf = bt_pbap_pse_create_pdu(&app_pse->pbap_pse, &pse_tx_pool);

    if (app_pse->tx_buf == NULL)
    {
        PRINTF("Fail to allocate tx buffer\n");
        return -ENOBUFS;
    }
    return 0;
}

static char pbap_pse_phonebook_example[] =
    "BEGIN:VCARD\n"
    "VERSION:2.1\n"
    "FN;CHARSET=UTF-8:descvs\n"
    "N;CHARSET=UTF-8:descvs\n"
    "END:VCARD";

static char pbap_pse_vcard_listing[] =
    "<?xml version=\"1.0\"?><!DOCTYPE vcard-listing SYSTEM \"vcard-listing.dtd\">"
    "<vCard-listing version=\"1.0\"><card handle=\"1.vcf\" name=\"qwe\"/>"
    "<card handle=\"2.vcf\" name=\"qwe\"/></vCard-listing>";

static char pbap_pse_vcard_entry[] =
    "BEGIN:VCARD\n"
    "VERSION:2.1\n"
    "FN:\n"
    "N:\n"
    "TEL;X-0:1155\n"
    "X-IRMC-CALL-DATETIME;DIALED:20220913T110607\n"
    "END:VCARD";

static int send_body_chunk(struct app_pbap_pse *app_pse,
                           uint16_t *sent_length,
                           uint16_t *remain_length,
                           uint16_t chunk_size,
                           operation_func func,
                           const char data[])
{
    uint8_t rsp_code;
    uint16_t bytes_to_send;
    int err;

    if (*remain_length <= chunk_size)
    {
        bytes_to_send = *remain_length;
        rsp_code      = BT_PBAP_RSP_CODE_SUCCESS;
    }
    else
    {
        bytes_to_send = chunk_size;
        rsp_code      = BT_OBEX_RSP_CODE_CONTINUE;
    }

    if (rsp_code == BT_PBAP_RSP_CODE_SUCCESS)
    {
        err = bt_obex_add_header_end_body(app_pse->tx_buf, bytes_to_send, (uint8_t *)&data[*sent_length]);
        app_pse->first_recv = true;
    }
    else
    {
        err                 = bt_obex_add_header_body(app_pse->tx_buf, bytes_to_send, (uint8_t *)&data[*sent_length]);
        app_pse->first_recv = false;
    }

    if (err != 0)
    {
        PRINTF("Fail to add body header %d\n", err);
        return err;
    }

    err = func(&app_pse->pbap_pse, rsp_code, app_pse->tx_buf);
    if (err != 0)
    {
        PRINTF("Fail to send pull phonebook rsp %d\n", err);
        return err;
    }

    *sent_length += bytes_to_send;
    *remain_length -= bytes_to_send;
    app_pse->tx_buf = NULL;

    return 0;
}

static void data_send_task(void *pvParameters)
{
    struct app_pbap_pse *app_pse = (struct app_pbap_pse *)pvParameters;
    int err;

    while (true)
    {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (app_pse->operation.remain_length > 0)
        {
            if (atomic_get(&abort_req) == 1)
            {
                break;
            }

            if (wait_to_send_cmd(app_pse))
            {
                break;
            }

            err = pbap_alloc_tx_buf(app_pse);
            if (err != 0)
            {
                break;
            }

            err = send_body_chunk(app_pse, &app_pse->operation.sent_length, &app_pse->operation.remain_length,
                                  PSE_SEND_DATA_LENGTH_MAX, app_pse->operation.func, app_pse->operation.data);
            if (err != 0)
            {
                if (app_pse->tx_buf != NULL)
                {
                    net_buf_unref(app_pse->tx_buf);
                    app_pse->tx_buf = NULL;
                }
                break;
            }
        }
    }
}

static void pbap_pse_pull_rsp(struct bt_pbap_pse *pbap_pse, struct net_buf *buf, operation_func func, char data[])
{
    struct app_pbap_pse *app_pse = CONTAINER_OF(pbap_pse, struct app_pbap_pse, pbap_pse);
    int err;

    if (app_pse->operation.remain_length == 0)
    {
        app_pse->operation.sent_length   = 0;
        app_pse->operation.remain_length = strlen(data);
    }

    err = pbap_alloc_tx_buf(app_pse);
    if (err != 0)
    {
        return;
    }

    if (app_pse->goep_v2)
    {
        if (app_pse->first_recv)
        {
            app_pse->srm_srmp = 0;
            err               = pbap_check_srm(buf);
            if (err != 0)
            {
                PRINTF("Fail to check SRM %d\n", err);
                goto fail;
            }
            app_pse->srm_srmp |= PEER_SRM_ENABLED;

            err = bt_obex_add_header_srm(app_pse->tx_buf, 0x01);
            if (err != 0)
            {
                PRINTF("Fail to add SRM header %d\n", err);
                goto fail;
            }
            app_pse->srm_srmp |= LOCAL_SRM_ENABLED;
        }

        app_pse->srm_srmp &= ~PEER_SRMP_ENABLED;
        err = pbap_check_srmp(buf);
        if (err == 0)
        {
            app_pse->srm_srmp |= PEER_SRMP_ENABLED;
        }
    }

    err = send_body_chunk(app_pse, &app_pse->operation.sent_length, &app_pse->operation.remain_length,
                          PSE_SEND_DATA_LENGTH_MAX, func, data);
    if (err != 0)
    {
        goto fail;
    }

    if (wait_to_send_cmd(app_pse))
    {
        return;
    }

    if (app_pse->operation.remain_length > 0 && app_pse->goep_v2)
    {
        app_pse->operation.func = func;
        app_pse->operation.data = data;
        xTaskNotifyGive(data_send_task_handle);
    }
    return;

fail:
    if (app_pse->tx_buf != NULL)
    {
        net_buf_unref(app_pse->tx_buf);
        app_pse->tx_buf = NULL;
    }
}

static void pbap_pse_pull_phonebook_cb(struct bt_pbap_pse *pbap_pse, struct net_buf *buf)
{
    PRINTF("pbap_pse get pull_phone_book request\n");
    pbap_pse_pull_rsp(pbap_pse, buf, bt_pbap_pse_pull_phone_book_rsp, pbap_pse_phonebook_example);
}

static void pbap_pse_pull_vcard_listing_cb(struct bt_pbap_pse *pbap_pse, struct net_buf *buf)
{
    PRINTF("pbap_pse get pull_vcard_listing request\n");
    pbap_pse_pull_rsp(pbap_pse, buf, bt_pbap_pse_pull_vcard_listing_rsp, pbap_pse_vcard_listing);
}

static void pbap_pse_pull_vcard_entry_cb(struct bt_pbap_pse *pbap_pse, struct net_buf *buf)
{
    PRINTF("pbap_pse get pull_vcard_entry request\n");
    pbap_pse_pull_rsp(pbap_pse, buf, bt_pbap_pse_pull_vcard_entry_rsp, pbap_pse_vcard_entry);
}

static void pbap_pse_set_phone_book_cb(struct bt_pbap_pse *pbap_pse, uint8_t flags, struct net_buf *buf)
{
    int err;

    err = bt_pbap_pse_set_phone_book_rsp(pbap_pse, BT_PBAP_RSP_CODE_SUCCESS, NULL);
    if (err != 0)
    {
        PRINTF("Fail to send set phone book rsp %d\n", err);
    }
}

static void pbap_pse_connected(struct bt_pbap_pse *pbap_pse, uint8_t version, uint16_t mopl, struct net_buf *buf)
{
    int err;
    struct app_pbap_pse *app_pse = CONTAINER_OF(pbap_pse, struct app_pbap_pse, pbap_pse);

    PRINTF("pbap connect version %u, mopl %d\n", version, mopl);

    err = pbap_alloc_tx_buf(app_pse);
    if (err != 0)
    {
        return;
    }

    err = bt_pbap_pse_connect_rsp(&app_pse->pbap_pse, 300, BT_PBAP_RSP_CODE_SUCCESS, app_pse->tx_buf);
    if (err != 0)
    {
        PRINTF("Fail to send conn rsp %d\n", err);
        if (app_pse->tx_buf != NULL)
        {
            net_buf_unref(app_pse->tx_buf);
        }
    }
    app_pse->tx_buf = NULL;

    atomic_set(&abort_req, 0);
    app_pse->operation.sent_length   = 0;
    app_pse->operation.remain_length = 0;
    app_pse->operation.data          = NULL;
    app_pse->srm_srmp                = 0;
    app_pse->first_recv              = true;
}

static void pbap_pse_disconnected(struct bt_pbap_pse *pbap_pse, struct net_buf *buf)
{
    int err;
    struct app_pbap_pse *app_pse = CONTAINER_OF(pbap_pse, struct app_pbap_pse, pbap_pse);

    PRINTF("pbap disconnect requested by pce\n");

    err = bt_pbap_pse_disconnect_rsp(pbap_pse, BT_PBAP_RSP_CODE_SUCCESS, NULL);
    if (err != 0)
    {
        PRINTF("Fail to send set phone book rsp %d\n", err);
    }

    app_pse->operation.sent_length   = 0;
    app_pse->operation.remain_length = 0;
    app_pse->operation.data          = NULL;
}

static void pbap_pse_rfcomm_transport_connected(struct bt_conn *conn, struct bt_pbap_pse *pbap_pse)
{
    struct app_pbap_pse *app_pse = CONTAINER_OF(pbap_pse, struct app_pbap_pse, pbap_pse);

    PRINTF("PBAP PSE %p rfcomm transport connected on %p\n", pbap_pse, conn);
    app_pse->goep_v2 = false;
}

static void pbap_pse_rfcomm_transport_disconnected(struct bt_pbap_pse *pbap_pse)
{
    PRINTF("PBAP PSE %p rfcomm transport disconnected\n", pbap_pse);
}

static void pbap_pse_l2cap_transport_connected(struct bt_conn *conn, struct bt_pbap_pse *pbap_pse)
{
    struct app_pbap_pse *app_pse = CONTAINER_OF(pbap_pse, struct app_pbap_pse, pbap_pse);

    PRINTF("PBAP PSE %p l2cap transport connected on %p\n", pbap_pse, conn);
    app_pse->goep_v2 = true;
}

static void pbap_pse_l2cap_transport_disconnected(struct bt_pbap_pse *pbap_pse)
{
    PRINTF("PBAP PSE %p l2cap transport disconnected\n", pbap_pse);
}

static void pbap_pse_abort_rsp_cb(struct bt_pbap_pse *pbap_pse, struct net_buf *buf)
{
    int err;
    struct app_pbap_pse *app_pse = CONTAINER_OF(pbap_pse, struct app_pbap_pse, pbap_pse);

    PRINTF("pbap_pse get abort request\n");

    err = bt_pbap_pse_abort_rsp(pbap_pse, BT_PBAP_RSP_CODE_SUCCESS, NULL);
    if (err != 0)
    {
        PRINTF("Fail to send abort rsp %d\n", err);
    }

    atomic_set(&abort_req, 1);
    app_pse->operation.sent_length   = 0;
    app_pse->operation.remain_length = 0;
    app_pse->operation.data          = NULL;
}

static struct bt_pbap_pse_cb pse_cb = {
    .rfcomm_connected    = pbap_pse_rfcomm_transport_connected,
    .rfcomm_disconnected = pbap_pse_rfcomm_transport_disconnected,
    .l2cap_connected     = pbap_pse_l2cap_transport_connected,
    .l2cap_disconnected  = pbap_pse_l2cap_transport_disconnected,
    .connect             = pbap_pse_connected,
    .disconnect          = pbap_pse_disconnected,
    .pull_phone_book     = pbap_pse_pull_phonebook_cb,
    .pull_vcard_listing  = pbap_pse_pull_vcard_listing_cb,
    .pull_vcard_entry    = pbap_pse_pull_vcard_entry_cb,
    .set_phone_book      = pbap_pse_set_phone_book_cb,
    .abort               = pbap_pse_abort_rsp_cb,
};

static int l2cap_accept(struct bt_conn *conn, struct bt_pbap_pse_l2cap *pbap_pse_l2cap, struct bt_pbap_pse **pbap_pse)
{
    *pbap_pse = &app_pse.pbap_pse;

    return 0;
}

static int rfcomm_accept(struct bt_conn *conn,
                         struct bt_pbap_pse_rfcomm *pbap_pse_rfcomm,
                         struct bt_pbap_pse **pbap_pse)
{
    *pbap_pse = &app_pse.pbap_pse;

    return 0;
}

static int pse_rfcomm_register()
{
    int err;

    rfcomm_server.server.rfcomm.channel = CONFIG_BT_PBAP_PSE_RFCOMM_CHANNEL;
    rfcomm_server.accept                = rfcomm_accept;
    err                                 = bt_pbap_pse_rfcomm_register(&rfcomm_server);
    if (err != 0)
    {
        PRINTF("Fail to register RFCOMM server (error %d)\n", err);
        rfcomm_server.server.rfcomm.channel = 0;
        return -ENOEXEC;
    }

    PRINTF("RFCOMM server (channel %02x) is registered\n", rfcomm_server.server.rfcomm.channel);

    return 0;
}

static int pse_l2cap_register()
{
    int err;

    l2cap_server.server.l2cap.psm = CONFIG_BT_PBAP_PSE_L2CAP_PSM;
    l2cap_server.accept           = l2cap_accept;
    err                           = bt_pbap_pse_l2cap_register(&l2cap_server);
    if (err != 0)
    {
        PRINTF("Fail to register L2CAP server (error %d)\n", err);
        l2cap_server.server.l2cap.psm = 0;
        return err;
    }
    PRINTF("L2cap server (psm %02x) is registered\n", l2cap_server.server.l2cap.psm);
    return 0;
}

static int pbap_register()
{
    int err;

    err = bt_sdp_register_service(&pbap_pse_rec);
    if (err != 0)
    {
        PRINTF("Failed to register SDP record\n");
        return err;
    }

    err = pse_rfcomm_register();
    if (err != 0)
    {
        return err;
    }

    err = pse_l2cap_register();
    if (err != 0)
    {
        return err;
    }

    err = bt_pbap_pse_register(&app_pse.pbap_pse, &pse_cb);
    if (err != 0)
    {
        PRINTF("Fail to register server %d\n", err);
        return err;
    }
    PRINTF("PBAP PSE is registered\n");
    return 0;
}

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

    err = pbap_register();
    if (err != 0)
    {
        return;
    }

    app_connect_init();

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

    data_send_task_handle = xTaskCreateStatic(data_send_task, "data send",
                                              CONFIG_BT_PBAP_PSE_DATA_SEND_TASK_STACK_SIZE / sizeof(StackType_t),
                                              &app_pse, tskIDLE_PRIORITY + 1,
                                              data_send_task_stack, &data_send_task_tcb);
    if (data_send_task_handle == NULL)
    {
        PRINTF("Failed to create data send task\n");
        return;
    }
}

void pbap_pse_task(void *pvParameters)
{
    int err = 0;

    PRINTF("Bluetooth PBAP PSE demo start...\n");

    /* Initializate BT Host stack */
    err = bt_enable(bt_ready);
    if (err != 0)
    {
        PRINTF("Bluetooth init failed (err %d)\n", err);
        return;
    }
    vTaskDelete(NULL);
}
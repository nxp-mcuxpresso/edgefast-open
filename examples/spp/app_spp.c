/*
 * Copyright 2021 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <ctype.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>

#include <zephyr/settings/settings.h>

#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/bluetooth/classic/sdp.h>

#include "fsl_debug_console.h"
#include "app_connect.h"
#include "app_shell.h"
#include "app_discover.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define SPP_RFCOMM_MTU     CONFIG_BT_RFCOMM_L2CAP_MTU
#define SDP_CLIENT_BUF_LEN 512

#define SPP_RFCOMM_CHANNEL_MIN 1U
#define SPP_RFCOMM_CHANNEL_MAX 31U

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

enum __packed spp_state {
    /* SPP_STATE_DISCONNECTED:
     * No active SPP connection. Resources are released or not allocated.
     */
    SPP_STATE_DISCONNECTED,
    /* SPP_STATE_CONNECTING:
     * Connection is in progress (e.g., SDP discovery or RFCOMM setup).
     * Not yet available for data transfer.
     */
    SPP_STATE_CONNECTING,
    /* SPP_STATE_CONNECTED:
     * RFCOMM DLC is established and SPP is ready for data transfer.
     */
    SPP_STATE_CONNECTED,
    /* SPP_STATE_DISCONNECTING:
     * Disconnection is in progress; cleanup and resource release pending.
     */
    SPP_STATE_DISCONNECTING,
};

struct bt_spp;

enum bt_spp_mode {
    /** SPP mode is unknown. */
    BT_SPP_MODE_UNKNOWN,
    /** SPP mode using UUID for service discovery. */
    BT_SPP_MODE_UUID,
    /** SPP mode using RFCOMM channel for service connection. */
    BT_SPP_MODE_CHANNEL,
};

struct bt_spp_client {
    /** @internal SPP SDP discover params */
    struct bt_sdp_discover_params sdp_discover;
};

struct bt_spp_server {
    /** Pointer to the SDP record exposed for this server.
     *
     *  If NULL when registering, the registration will fail. The record must
     *  remain valid for the lifetime of the registration.
     */
    struct bt_sdp_record *sdp_record;

    union {
        /** UUID (service) to discovery remote SPP service */
        const struct bt_uuid *uuid;
        /** Rfcomm channel to connect remote SPP server */
        uint8_t channel;
    };

    /*  SPP mode: UUID or channel */
    enum bt_spp_mode mode;

    /** RFCOMM server instance.
     *
     *  Populated/used by the stack to accept incoming RFCOMM connections
     *  for this SPP service. Do not modify directly from application code.
     */
    struct bt_rfcomm_server rfcomm_server;
};

struct bt_spp {
    /** @internal RFCOMM Data Link Connection (DLC) */
    struct bt_rfcomm_dlc rfcomm_dlc;

    union {
        /** UUID (service) to discovery remote SPP service */
        const struct bt_uuid *uuid;
        /** Rfcomm channel to connect remote SPP server */
        uint8_t channel;
    };

    enum bt_spp_mode mode;

    /** @internal Connection state (atomic) */
    atomic_t state;

    /** @internal Role-specific context
     *
     *  Union containing context specific to the role of this SPP instance:
     *   - client: SDP discovery parameters and UUID used when initiating a
     *             connection to a remote SPP service.
     *   - server: server registration and accept callback context for
     *             accepted incoming connections.
     *
     *  Access the appropriate member only when the instance role is known.
     */
    union {
        struct bt_spp_client client;
        struct bt_spp_server server;
    };
};

/*******************************************************************************
 * Variables
 ******************************************************************************/
NET_BUF_POOL_FIXED_DEFINE(sdp_pool, CONFIG_BT_MAX_CONN, SDP_CLIENT_BUF_LEN, 8, NULL);
NET_BUF_POOL_FIXED_DEFINE(tx_pool, CONFIG_BT_MAX_CONN,
                          BT_L2CAP_BUF_SIZE(SPP_RFCOMM_MTU), 16, NULL);

static uint8_t spp_channel;

static union {
    struct bt_uuid_16 u16;
    struct bt_uuid_32 u32;
    struct bt_uuid_128 u128;
} spp_uuid;

static struct bt_sdp_attribute spp_attrs_channel[] = {
    BT_SDP_NEW_SERVICE,
    BT_SDP_LIST(
        BT_SDP_ATTR_SVCLASS_ID_LIST,
        BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
        BT_SDP_DATA_ELEM_LIST(
        {
            BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
            BT_SDP_ARRAY_16(BT_SDP_SERIAL_PORT_SVCLASS)
        },
        )
    ),
    BT_SDP_LIST(
        BT_SDP_ATTR_PROTO_DESC_LIST,
        BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 12),
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
                &spp_channel
            },
            )
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
                BT_SDP_ARRAY_16(BT_SDP_SERIAL_PORT_SVCLASS)
            },
            {
                BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
                BT_SDP_ARRAY_16(0x0102)
            },
            )
        },
        )
    ),
    BT_SDP_SERVICE_NAME("Serial Port"),
};

static struct bt_sdp_record spp_rec_channel = BT_SDP_RECORD(spp_attrs_channel);

static uint8_t spp_uuid_buf[BT_UUID_SIZE_128];

static struct bt_sdp_attribute spp_attrs_uuid128[] = {
    BT_SDP_NEW_SERVICE,
    BT_SDP_LIST(
        BT_SDP_ATTR_SVCLASS_ID_LIST,
        BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 17),
        BT_SDP_DATA_ELEM_LIST(
        {
            BT_SDP_TYPE_SIZE(BT_SDP_UUID128),
            spp_uuid_buf
        },
        )
    ),
    BT_SDP_LIST(
        BT_SDP_ATTR_PROTO_DESC_LIST,
        BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 12),
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
                &spp_channel
            },
            )
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
                BT_SDP_ARRAY_16(BT_SDP_SERIAL_PORT_SVCLASS)
            },
            {
                BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
                BT_SDP_ARRAY_16(0x0102)
            },
            )
        },
        )
    ),
    BT_SDP_SERVICE_NAME("Serial Port"),
};

static struct bt_sdp_record spp_rec_uuid128 = BT_SDP_RECORD(spp_attrs_uuid128);

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void spp_hexdump(const uint8_t *data, size_t len);
static void bt_spp_connected(struct bt_rfcomm_dlc *dlci);
static void bt_spp_disconnected(struct bt_rfcomm_dlc *dlci);
static void bt_spp_recv(struct bt_rfcomm_dlc *dlci, struct net_buf *buf);
static int bt_spp_uuid_from_str(const char *str, struct bt_uuid *uuid);
static int bt_spp_accept(struct bt_conn *conn, struct bt_rfcomm_server *server,
                         struct bt_rfcomm_dlc **dlc);
static int bt_spp_server_register(struct bt_spp_server *server);
static int bt_spp_connect_rfcomm(struct bt_conn *conn, struct bt_spp *spp);
static uint8_t sdp_discover_cb(struct bt_conn *conn, struct bt_sdp_client_result *response,
                               const struct bt_sdp_discover_params *params);
static int bt_spp_connect(struct bt_conn *conn, struct bt_spp *spp);
static int bt_spp_send(struct bt_spp *spp, struct net_buf *buf);
static int bt_spp_disconnect(struct bt_spp *spp);
static void bt_ready(int err);

/*******************************************************************************
 * Code
 ******************************************************************************/
static void spp_hexdump(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        if (i % 16 == 0)
        {
            PRINTF("\r\n%04x: ", (unsigned int)i);
        }
        PRINTF("%02x ", data[i]);

        /* Print ASCII representation at the end of each 16-byte line */
        if ((i % 16 == 15) || (i == len - 1))
        {
            /* Add padding if last line is incomplete */
            size_t padding = (i % 16 == 15) ? 0 : (15 - (i % 16));
            for (size_t j = 0; j < padding; j++)
            {
                PRINTF("   ");
            }

            PRINTF(" |");
            /* Print ASCII characters for this line */
            size_t line_start = (i / 16) * 16;
            size_t line_end = (i % 16 == 15) ? i : i;
            for (size_t j = line_start; j <= line_end; j++)
            {
                char c = (data[j] >= 32 && data[j] <= 126) ? data[j] : '.';
                PRINTF("%c", c);
            }
            PRINTF("|");
        }
    }
    PRINTF("\r\n");
}

static void bt_spp_connected(struct bt_rfcomm_dlc *dlci)
{
    struct bt_spp *spp = CONTAINER_OF(dlci, struct bt_spp, rfcomm_dlc);

    atomic_set(&spp->state, SPP_STATE_CONNECTED);
    PRINTF("SPP: connected (spp=%p, channel=%u)\r\n", spp, spp->channel);
}

static void bt_spp_disconnected(struct bt_rfcomm_dlc *dlci)
{
    struct bt_spp *spp = CONTAINER_OF(dlci, struct bt_spp, rfcomm_dlc);

    atomic_set(&spp->state, SPP_STATE_DISCONNECTED);
    PRINTF("SPP: disconnected (spp=%p)\r\n", spp);
}

static void bt_spp_recv(struct bt_rfcomm_dlc *dlci, struct net_buf *buf)
{
    struct bt_spp *spp = CONTAINER_OF(dlci, struct bt_spp, rfcomm_dlc);

    PRINTF("SPP: rx data (spp=%p, len=%u)\r\n", spp, (unsigned int)buf->len);
    spp_hexdump(buf->data, buf->len);
}

static int bt_spp_uuid_from_str(const char *str, struct bt_uuid *uuid)
{
    if (str == NULL || uuid == NULL)
    {
        return -EINVAL;
    }

    switch (strlen(str))
    {
    case BT_UUID_SIZE_16 * 2:
    {
        uint16_t *p = &BT_UUID_16(uuid)->val;

        if (sscanf(str, "%04hx", p) != 1)
        {
            return -EINVAL;
        }

        uuid->type = BT_UUID_TYPE_16;
        return 0;
    }
    case BT_UUID_SIZE_32 * 2:
    {
        uint32_t *p = &BT_UUID_32(uuid)->val;

        if (sscanf(str, "%08x", (unsigned int *)p) != 1)
        {
            return -EINVAL;
        }

        uuid->type = BT_UUID_TYPE_32;
        return 0;
    }
    case BT_UUID_STR_LEN - 1:
    {

        uint32_t b[BT_UUID_SIZE_128];
        uint8_t *p = BT_UUID_128(uuid)->val;

        if (sscanf(str,
                   "%02x%02x%02x%02x-%02x%02x-%02x%02x"
                   "-%02x%02x-%02x%02x%02x%02x%02x%02x",
                   &b[15], &b[14], &b[13], &b[12],
                   &b[11], &b[10], &b[9],  &b[8],
                   &b[7],  &b[6],  &b[5],  &b[4],
                   &b[3],  &b[2],  &b[1],  &b[0]) != BT_UUID_SIZE_128)
        {
            return -EINVAL;
        }

        for (int i = 0; i < BT_UUID_SIZE_128; i++)
        {
            p[i] = (uint8_t)(b[i] & 0xFFU);
        }

        uuid->type = BT_UUID_TYPE_128;
        return 0;
    }
    default:
        return -EINVAL;
    }
}

static struct bt_spp default_spp;

static struct bt_rfcomm_dlc_ops spp_rfcomm_ops = {
    .recv = bt_spp_recv,
    .connected = bt_spp_connected,
    .disconnected = bt_spp_disconnected,
};

static int bt_spp_accept(struct bt_conn *conn, struct bt_rfcomm_server *server,
                         struct bt_rfcomm_dlc **dlc)
{
    struct bt_spp_server *spp_srv = CONTAINER_OF(server, struct bt_spp_server, rfcomm_server);

    if (atomic_get(&default_spp.state) != SPP_STATE_DISCONNECTED)
    {
        PRINTF("SPP: reject incoming RFCOMM (state=%ld)\r\n",
               (long)atomic_get(&default_spp.state));
        return -EALREADY;
    }

    atomic_set(&default_spp.state, SPP_STATE_CONNECTING);
    default_spp.rfcomm_dlc.ops = &spp_rfcomm_ops;
    default_spp.rfcomm_dlc.mtu = SPP_RFCOMM_MTU;
    default_spp.rfcomm_dlc.required_sec_level = BT_SECURITY_L2;
    *dlc = &default_spp.rfcomm_dlc;

    PRINTF("SPP: accepted incoming connection (conn=%p, mode=%u)\r\n", conn, spp_srv->mode);
    return 0;
}

static int bt_spp_server_register(struct bt_spp_server *server)
{
    int err;

    if (server == NULL || server->sdp_record == NULL)
    {
        PRINTF("SPP: server register failed (server=%p, sdp_record=%p)\r\n", server,
               server ? server->sdp_record : NULL);
        return -EINVAL;
    }

    server->rfcomm_server.accept = bt_spp_accept;
    err = bt_rfcomm_server_register(&server->rfcomm_server);
    if (err < 0)
    {
        PRINTF("SPP: RFCOMM server register failed (err=%d)\r\n", err);
        return err;
    }

    err = bt_sdp_register_service(server->sdp_record);
    if (err < 0)
    {
        PRINTF("SPP: SDP service register failed (err=%d)\r\n", err);
        return err;
    }

    return 0;
}

static int bt_spp_connect_rfcomm(struct bt_conn *conn, struct bt_spp *spp)
{
    int err;

    spp->rfcomm_dlc.ops = &spp_rfcomm_ops;
    spp->rfcomm_dlc.mtu = SPP_RFCOMM_MTU;
    spp->rfcomm_dlc.required_sec_level = BT_SECURITY_L2;

    err = bt_rfcomm_dlc_connect(conn, &spp->rfcomm_dlc, spp->channel);
    if (err < 0) {
        PRINTF("SPP: RFCOMM DLC connect failed (conn=%p, channel=%u, err=%d)\r\n", conn,
               spp->channel, err);
        return err;
    }

    return 0;
}

static uint8_t sdp_discover_cb(struct bt_conn *conn, struct bt_sdp_client_result *response,
                   const struct bt_sdp_discover_params *params)
{
    uint16_t channel;
    int err;
    char uuid_str[BT_UUID_STR_LEN];
    struct bt_spp *spp = CONTAINER_OF(params, struct bt_spp, client.sdp_discover);

    if (response == NULL) {
        bt_uuid_to_str(params->uuid, uuid_str, sizeof(uuid_str));
        PRINTF("SPP: SDP service not found (conn=%p, uuid=%s)\r\n", conn, uuid_str);
        atomic_set(&spp->state, SPP_STATE_DISCONNECTED);
        return BT_SDP_DISCOVER_UUID_CONTINUE;
    }

    if (response->resp_buf == NULL) {
        bt_uuid_to_str(params->uuid, uuid_str, sizeof(uuid_str));
        PRINTF("SPP: SDP response buffer is NULL (conn=%p, uuid=%s)\r\n", conn, uuid_str);
        goto exit;
    }

    err = bt_sdp_get_proto_param(response->resp_buf, BT_SDP_PROTO_RFCOMM, &channel);
    if (err < 0) {
        bt_uuid_to_str(params->uuid, uuid_str, sizeof(uuid_str));
        PRINTF("SPP: SDP parse failed to get RFCOMM channel (conn=%p, uuid=%s, err=%d)\r\n",
               conn, uuid_str, err);
        goto exit;
    }

    bt_uuid_to_str(params->uuid, uuid_str, sizeof(uuid_str));
    PRINTF("SPP: SDP found RFCOMM channel (conn=%p, uuid=%s, channel=%u)\r\n", conn,
           uuid_str, channel);

    spp->channel = channel;
    err = bt_spp_connect_rfcomm(conn, spp);
    if (err < 0) {
        PRINTF("SPP: connect failed after SDP (conn=%p, channel=%u, err=%d)\r\n", conn,
               spp->channel, err);
        goto exit;
    }

    return BT_SDP_DISCOVER_UUID_STOP;

exit:
    atomic_set(&spp->state, SPP_STATE_DISCONNECTED);
    return BT_SDP_DISCOVER_UUID_STOP;
}

static int bt_spp_connect(struct bt_conn *conn, struct bt_spp *spp)
{
    int err;
    char uuid_str[BT_UUID_STR_LEN];

    if (atomic_get(&spp->state) != SPP_STATE_DISCONNECTED) {
        PRINTF("SPP: connect rejected (state=%ld)\r\n", (long)atomic_get(&spp->state));
        return -EALREADY;
    }

    if (spp->mode == BT_SPP_MODE_CHANNEL) {
        err = bt_spp_connect_rfcomm(conn, spp);
        if (err < 0) {
            PRINTF("SPP: connect by channel failed (conn=%p, channel=%u, err=%d)\r\n",
                   conn, spp->channel, err);
            return err;
        }
    } else if (spp->mode == BT_SPP_MODE_UUID) {
        spp->client.sdp_discover.type = BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR;
        spp->client.sdp_discover.func = sdp_discover_cb;
        spp->client.sdp_discover.pool = &sdp_pool;
        spp->client.sdp_discover.uuid = spp->uuid;

        err = bt_sdp_discover(conn, &spp->client.sdp_discover);
        if (err < 0) {
            bt_uuid_to_str(spp->uuid, uuid_str, sizeof(uuid_str));
            PRINTF("SPP: SDP discover failed (conn=%p, uuid=%s, err=%d)\r\n", conn,
                   uuid_str, err);
            return err;
        }
    }

    atomic_set(&spp->state, SPP_STATE_CONNECTING);
    return 0;
}

int spp_appl_server_register_uuid(const char *str)
{
    int err;
    struct bt_uuid_128 uuid128;

    if (str == NULL) {
        PRINTF("SPP: UUID string is NULL\r\n");
        return -EINVAL;
    }

    if (bt_spp_uuid_from_str(str, (struct bt_uuid *)&uuid128) < 0) {
        PRINTF("SPP: invalid UUID string: '%s'\r\n", str);
        return -EINVAL;
    }

    if (uuid128.uuid.type != BT_UUID_TYPE_128) {
        PRINTF("SPP: only 128-bit UUID is supported (got type=%u)\r\n",
               uuid128.uuid.type);
        return -EINVAL;
    }

    memcpy(spp_uuid_buf, uuid128.val, BT_UUID_SIZE_128);

    default_spp.server.sdp_record = &spp_rec_uuid128;
    default_spp.server.rfcomm_server.channel = 0U;

    err = bt_spp_server_register(&default_spp.server);
    if (err < 0) {
        PRINTF("SPP: register server failed (uuid=%s, err=%d)\r\n", str, err);
        default_spp.server.sdp_record = NULL;
        default_spp.server.rfcomm_server.channel = 0U;
        return err;
    }

    spp_channel = default_spp.server.rfcomm_server.channel;

    PRINTF("SPP: server registered (uuid=%s, channel=%u)\r\n", str, spp_channel);
    return 0;
}

int spp_appl_server_register(uint8_t channel)
{
    int err;

    spp_channel = channel;

    default_spp.server.sdp_record = &spp_rec_channel;
    default_spp.server.rfcomm_server.channel = spp_channel;

    err = bt_spp_server_register(&default_spp.server);
    if (err < 0) {
        PRINTF("SPP: register server failed (channel=%u, err=%d)\r\n", spp_channel, err);
        default_spp.server.sdp_record = NULL;
        default_spp.server.rfcomm_server.channel = 0U;
        return err;
    }

    PRINTF("SPP: server registered (channel=%u)\r\n", spp_channel);
    return 0;
}

int spp_appl_connect(struct bt_conn *conn, uint8_t channel)
{
    int err;

    if (conn == NULL) {
        PRINTF("SPP: invalid connection (conn is NULL)\r\n");
        return -EINVAL;
    }

    default_spp.mode = BT_SPP_MODE_CHANNEL;
    default_spp.channel = channel;

    err = bt_spp_connect(conn, &default_spp);
    if (err < 0) {
        PRINTF("SPP: connect by channel failed (channel=%u, err=%d)\r\n", channel, err);
        default_spp.mode = BT_SPP_MODE_UNKNOWN;
        default_spp.uuid = NULL;
        return err;
    }

    PRINTF("SPP: connect started (channel=%u)\r\n", channel);
    return 0;
}

int spp_appl_connect_by_uuid(struct bt_conn *conn, const char *str)
{
    int err;
    size_t len;

    /* Validate input parameters */
    if (conn == NULL) {
        PRINTF("SPP: invalid connection (conn is NULL)\r\n");
        return -EINVAL;
    }

    if (str == NULL) {
        PRINTF("SPP: UUID string is NULL\r\n");
        return -EINVAL;
    }

    len = strlen(str);

    /* Parse UUID based on string length */
    if (len == (BT_UUID_SIZE_16 * 2)) {
        /* 16-bit UUID */
        uint16_t val;
        spp_uuid.u16.uuid.type = BT_UUID_TYPE_16;
        hex2bin(str, len, (uint8_t *)&val, sizeof(val));
        spp_uuid.u16.val = sys_be16_to_cpu(val);
    } else if (len == (BT_UUID_SIZE_32 * 2)) {
        /* 32-bit UUID */
        uint32_t val;
        spp_uuid.u32.uuid.type = BT_UUID_TYPE_32;
        hex2bin(str, len, (uint8_t *)&val, sizeof(val));
        spp_uuid.u32.val = sys_be32_to_cpu(val);
    } else if (len == (BT_UUID_STR_LEN - 1)) {
        /* 128-bit UUID string format */
        if (bt_spp_uuid_from_str(str, (struct bt_uuid *)&spp_uuid) < 0) {
            PRINTF("SPP: invalid UUID string: '%s'\r\n", str);
            return -EINVAL;
        }
    } else {
        PRINTF("SPP: invalid UUID '%s' (expected 16/32-bit hex or UUID-128 string)\r\n", str);
        return -EINVAL;
    }

    /* Configure SPP connection parameters */
    default_spp.mode = BT_SPP_MODE_UUID;
    default_spp.uuid = (const struct bt_uuid *)&spp_uuid;

    /* Initiate connection */
    err = bt_spp_connect(conn, &default_spp);
    if (err < 0) {
        char uuid_str[BT_UUID_STR_LEN];
        bt_uuid_to_str((const struct bt_uuid *)&spp_uuid, uuid_str, sizeof(uuid_str));
        PRINTF("SPP: connect by UUID failed (uuid=%s, err=%d)\r\n", uuid_str, err);
        /* Cleanup on failure */
        default_spp.mode = BT_SPP_MODE_UNKNOWN;
        default_spp.uuid = NULL;
        return err;
    }

    {
        char uuid_str[BT_UUID_STR_LEN];
        bt_uuid_to_str((const struct bt_uuid *)&spp_uuid, uuid_str, sizeof(uuid_str));
        PRINTF("SPP: connect started (uuid=%s)\r\n", uuid_str);
    }

    return 0;
}


static int bt_spp_send(struct bt_spp *spp, struct net_buf *buf)
{
    int err;

    if (atomic_get(&spp->state) != SPP_STATE_CONNECTED) {
        PRINTF("SPP: send rejected (not connected, state=%ld)\r\n",
               (long)atomic_get(&spp->state));
        return -ENOTCONN;
    }

    err = bt_rfcomm_dlc_send(&spp->rfcomm_dlc, buf);
    if (err < 0) {
        PRINTF("SPP: RFCOMM send failed (len=%u, err=%d)\r\n", (unsigned int)buf->len,
               err);
        return err;
    }

    return 0;
}

int spp_appl_send(uint8_t *data, size_t len)
{
    int err;
    struct net_buf *buf;
    size_t tailroom;

    if (data == NULL || len == 0) {
        PRINTF("SPP: invalid data or length\r\n");
        return -EINVAL;
    }

    buf = bt_rfcomm_create_pdu(&tx_pool);
    if (buf == NULL) {
        PRINTF("SPP: TX buffer allocation failed\r\n");
        return -ENOMEM;
    }

    len = MIN(SPP_RFCOMM_MTU, len);
    PRINTF("SPP: tx data (len=%zu)\r\n", len);

    tailroom = net_buf_tailroom(buf);
    if (len > tailroom) {
        PRINTF("SPP: tx len %zu exceeds buffer tailroom %zu\r\n", len, tailroom);
        net_buf_unref(buf);
        return -EMSGSIZE;
    }

    net_buf_add_mem(buf, data, len);

    err = bt_spp_send(&default_spp, buf);
    if (err < 0) {
        PRINTF("SPP: send failed (err=%d)\r\n", err);
        net_buf_unref(buf);
        return err;
    }

    return 0;
}

static int bt_spp_disconnect(struct bt_spp *spp)
{
    int err;

    if (atomic_get(&spp->state) != SPP_STATE_CONNECTED) {
        PRINTF("SPP: disconnect rejected (not connected, state=%ld)\r\n",
               (long)atomic_get(&spp->state));
        return -ENOTCONN;
    }

    err = bt_rfcomm_dlc_disconnect(&spp->rfcomm_dlc);
    if (err < 0) {
        PRINTF("SPP: RFCOMM disconnect failed (err=%d)\r\n", err);
        return err;
    }

    atomic_set(&spp->state, SPP_STATE_DISCONNECTING);
    return 0;
}

int spp_appl_disconnect(void)
{
    int err;

    err = bt_spp_disconnect(&default_spp);
    if (err < 0) {
        PRINTF("SPP: disconnect failed (err=%d)\r\n", err);
        return err;
    }

    PRINTF("SPP: disconnecting...\r\n");
    return 0;
}

static void bt_ready(int err) {
    if (err) {
        PRINTF("Bluetooth init failed (err %d)\n", err);
        return;
    }

#if (defined(CONFIG_BT_SETTINGS) && (CONFIG_BT_SETTINGS > 0))
    settings_load();
#endif

    PRINTF("Bluetooth initialized\n");

    app_connect_init();
    app_discover_init();

    err = bt_br_set_connectable(true, NULL);
    if (err) {
        PRINTF("BR/EDR set/rest connectable failed (err %d)\n", err);
        return;
    }
    err = bt_br_set_discoverable(true, false);
    if (err) {
        PRINTF("BR/EDR set discoverable failed (err %d)\n", err);
        return;
    }
    PRINTF("BR/EDR set connectable and discoverable done\n");

    app_shell_init();
}

void spp_task(void *pvParameters) {
    int err;

    PRINTF("Bluetooth SPP demo start...\n");

    /* Initialize BT Host stack */
    err = bt_enable(bt_ready);
    if (err) {
        PRINTF("Bluetooth init failed (err %d)\n", err);
        while (1) {
            vTaskDelay(2000);
        }
    }

    vTaskDelete(NULL);
}

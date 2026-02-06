/*
 * Copyright 2024-2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <porting.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/a2dp.h>
#include <zephyr/bluetooth/classic/a2dp_codec_sbc.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include "clock_config.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "app_connect.h"

#include "zephyr/sys/ring_buffer.h"
#include <zephyr/bluetooth/sbc.h>

#if 1 /* Code change for bridge. */
#include "unicast_media_sender.h"

static OSA_SEMAPHORE_HANDLE_DEFINE(a2dp_sink_init_done);
extern int a2dp_sink_audio_frame_size;
bt_addr_t a2dp_bdaddr;

extern int a2dp_audio_sample_rate;
extern int a2dp_audio_channels;
extern int a2dp_audio_bits;

RING_BUF_DECLARE(a2dp_ring_buf, 48000 / 100 * 4 * 32);
/* The PCM data size for one sbc frame
 * (channel num: 2, sample size: 2, max subband: 8, max block length: 16)
 */
#define A2DP_SBC_ONE_FRAME_MAX_SIZE (2 * 2 * 16 * 8)
static uint16_t pcm_frame_buffer[A2DP_SBC_ONE_FRAME_MAX_SIZE / 2];
static bool sbc_first_data = true;
static uint32_t sbc_expected_ts = 0;
static struct sbc_decoder decoder;
#endif

#define SAMPLE_BIT_WIDTH (16U)

#define A2DP_CLASS_OF_DEVICE (0x200404U)

struct bt_a2dp *default_a2dp;
BT_A2DP_SBC_SINK_EP_DEFAULT(sbc_sink_ep);
static struct bt_a2dp_stream sbc_stream;
BT_A2DP_SBC_EP_CFG_DEFAULT(sbc_cfg, A2DP_SBC_SAMP_FREQ_44100);

static struct bt_sdp_attribute a2dp_sink_attrs[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3), /* 35 03 */
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UUID16), /* 19 */
			BT_SDP_ARRAY_16(BT_SDP_AUDIO_SINK_SVCLASS) /* 11 0B */
		},
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROTO_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 16),/* 35 10 */
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),/* 35 06 */
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16), /* 19 */
				BT_SDP_ARRAY_16(BT_SDP_PROTO_L2CAP) /* 01 00 */
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16), /* 09 */
				BT_SDP_ARRAY_16(BT_UUID_AVDTP_VAL) /* 00 19 */
			},
			)
		},
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),/* 35 06 */
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16), /* 19 */
				BT_SDP_ARRAY_16(BT_UUID_AVDTP_VAL) /* 00 19 */
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16), /* 09 */
				BT_SDP_ARRAY_16(AVDTP_VERSION) /* AVDTP version: 01 03 */
			},
			)
		},
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROFILE_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 8), /* 35 08 */
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6), /* 35 06 */
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16), /* 19 */
				BT_SDP_ARRAY_16(BT_SDP_ADVANCED_AUDIO_SVCLASS) /* 11 0d */
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16), /* 09 */
				BT_SDP_ARRAY_16(0x0104U) /* 01 04 */
			},
			)
		},
		)
	),
	BT_SDP_SERVICE_NAME("A2DPSink"),
	BT_SDP_SUPPORTED_FEATURES(0x0001U),
};

static struct bt_sdp_record a2dp_sink_rec = BT_SDP_RECORD(a2dp_sink_attrs);

void app_a2dp_connect(struct bt_conn *conn)
{
	default_a2dp = bt_a2dp_connect(conn);
	if (default_a2dp == NULL) {
		PRINTF ("fail to connect a2dp\r\n");
	}
}

static void sbc_stop_play(void)
{
	PRINTF("stream stopped\n");
}

void sbc_stream_configured(struct bt_a2dp_stream *stream)
{
	uint32_t sample_freq;
	uint8_t channel_num;
	struct bt_a2dp_codec_sbc_params *sbc_config = (struct bt_a2dp_codec_sbc_params *)
						      &sbc_cfg.codec_config->codec_ie[0];

	channel_num = bt_a2dp_sbc_get_channel_num(sbc_config);
	sample_freq = bt_a2dp_sbc_get_sampling_frequency(sbc_config);

#if 1 /* Code change for bridge. */
	a2dp_audio_sample_rate  = sample_freq;
	a2dp_audio_channels = channel_num;
	a2dp_audio_bits = 16;

	PRINTF("a2dp configure sample rate %dHz, ch %d\r\n", a2dp_audio_sample_rate, a2dp_audio_channels);
#endif

	PRINTF("stream configured\n");
}

void sbc_stream_established(struct bt_a2dp_stream *stream)
{
	PRINTF("stream established\n");
}

void sbc_stream_released(struct bt_a2dp_stream *stream)
{
	sbc_stop_play();
}

void sbc_stream_started(struct bt_a2dp_stream *stream)
{
	PRINTF("stream started\n");
#if 1 /* Code change for bridge. */
	sbc_setup_decoder(&decoder);
	ring_buf_reset(&a2dp_ring_buf);
	sbc_first_data = true;
	OSA_SemaphorePost(a2dp_sink_init_done);
#endif
}

void sbc_stream_suspended(struct bt_a2dp_stream *stream)
{
	sbc_stop_play();
}

void sbc_stream_recv(struct bt_a2dp_stream *stream, struct net_buf *buf, uint16_t seq_num,
		     uint32_t ts)
{
	struct bt_a2dp_codec_sbc_params *sbc_config = (struct bt_a2dp_codec_sbc_params *)
						      &sbc_cfg.codec_config->codec_ie[0];

	uint8_t channel_num = bt_a2dp_sbc_get_channel_num(sbc_config);
	uint8_t sbc_hdr = net_buf_pull_u8(buf);
	uint8_t num_frames = BT_A2DP_SBC_MEDIA_HDR_NUM_FRAMES_GET(sbc_hdr);

	/* first handle the ts. */
	if (sbc_first_data) {
		sbc_expected_ts = ts;
		sbc_first_data = false;
	}

	/* check if there is a gap in the timestamp. */
	if (ts != sbc_expected_ts) {
		/* if we have a gap and the ring buffer have enough space, then fill the gap. */
		if (ts > sbc_expected_ts) {
			uint32_t gap_samples = ts - sbc_expected_ts;
			uint32_t gap_bytes = gap_samples * channel_num * (SAMPLE_BIT_WIDTH / 8U);

			if (ring_buf_space_get(&a2dp_ring_buf) >= gap_bytes) {
				uint8_t zero_buf[4];
				memset(zero_buf, 0, sizeof(zero_buf));
				for(int i = 0; i < gap_samples; i++)
				{
					ring_buf_put(&a2dp_ring_buf, zero_buf, sizeof(zero_buf));
				}
			}
		}
		sbc_expected_ts = ts;
	}

	/* process the sbc frames */
	uint32_t samples_count = 0;
	const void *in_data = (void *)buf->data;
	uint16_t len = buf->len;
	for(int i = 0; i < num_frames; i++)
	{
		size_t out_size = sizeof(pcm_frame_buffer);
		int err = sbc_decode(&decoder, &in_data, (uint32_t*)&len, pcm_frame_buffer, (uint32_t*)&out_size);
		if(err == 0)
		{
			if(out_size <= ring_buf_space_get(&a2dp_ring_buf))
			{
				ring_buf_put(&a2dp_ring_buf, (uint8_t *)pcm_frame_buffer, out_size);
				samples_count += out_size / (channel_num * (SAMPLE_BIT_WIDTH / 8U));
			}
		}
		else
		{
			PRINTF("sbc decode error %d\r\n", err);
			break;
		}
	}

	sbc_expected_ts = ts + samples_count;
}

static struct bt_a2dp_stream_ops stream_ops = {
	.configured = sbc_stream_configured,
	.established = sbc_stream_established,
	.released = sbc_stream_released,
	.started = sbc_stream_started,
	.suspended = sbc_stream_suspended,
	.recv = sbc_stream_recv,
};

void app_a2dp_connected(struct bt_a2dp *a2dp, int err)
{
	if (err == 0) {
#if !((defined AUTO_CONNECT_USE_BOND_INFO) && (AUTO_CONNECT_USE_BOND_INFO))
		struct bt_conn_info info;
#endif

		default_a2dp = a2dp;
		PRINTF("a2dp connected success\r\n");
#if !((defined AUTO_CONNECT_USE_BOND_INFO) && (AUTO_CONNECT_USE_BOND_INFO))
		if (default_conn != NULL) {
			bt_conn_get_info(default_conn, &info);
			app_auto_connect_save_addr(info.br.dst);
		}
#endif
	} else {
		PRINTF("a2dp connected fail\r\n");
	}
}

void app_a2dp_disconnected(struct bt_a2dp *a2dp)
{
	default_a2dp = NULL;
	PRINTF("a2dp disconnected\n");
}

int app_a2dp_config_req(struct bt_a2dp *a2dp, struct bt_a2dp_ep *ep,
			struct bt_a2dp_codec_cfg *codec_cfg, struct bt_a2dp_stream **stream,
			uint8_t *rsp_err_code)
{
	uint32_t sample_rate;

	*sbc_cfg.codec_config = *codec_cfg->codec_config;

	bt_a2dp_stream_cb_register(&sbc_stream, &stream_ops);
	*stream = &sbc_stream;
	*rsp_err_code = 0;

	PRINTF("receive requesting config and accept\n");
	sample_rate = bt_a2dp_sbc_get_sampling_frequency(
		(struct bt_a2dp_codec_sbc_params *)&codec_cfg->codec_config->codec_ie[0]);
	PRINTF("sample rate %dHz\n", sample_rate);

	return 0;
}

static struct bt_a2dp_cb a2dp_cb = {
	.connected = app_a2dp_connected,
	.disconnected = app_a2dp_disconnected,
	.config_req = app_a2dp_config_req,
};

static void app_edgefast_a2dp_init(void)
{
	bt_sdp_register_service(&a2dp_sink_rec);
	bt_a2dp_register_ep(&sbc_sink_ep, BT_AVDTP_AUDIO, BT_AVDTP_SINK);
	bt_a2dp_register_cb(&a2dp_cb);
}

static void bt_ready(int err)
{
	struct net_buf *buf = NULL;
	struct bt_hci_cp_write_class_of_device *cp;

	if (err) {
		PRINTF("Bluetooth init failed (err %d)\n", err);
		return;
	}

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		settings_load();
	}

	PRINTF("Bluetooth initialized\n");

	buf = bt_hci_cmd_alloc(K_FOREVER);
	if (buf != NULL) {
		cp = net_buf_add(buf, sizeof(*cp));
		sys_put_le24(A2DP_CLASS_OF_DEVICE, &cp->class_of_device[0]);
		err = bt_hci_cmd_send_sync(BT_HCI_OP_WRITE_CLASS_OF_DEVICE, buf, NULL);
	} else {
		err = -ENOBUFS;
	}

	if (err != 0) {
		PRINTF("setting class of device failed\n");
	}

	app_connect_init();
	app_edgefast_a2dp_init();
	app_a2dp_snk_auto_connect();
	// codec_play_init();
}

static StackType_t xStack[ configMINIMAL_STACK_SIZE * 8 ];
static StaticTask_t xTaskBuffer;

void app_a2dp_sink_task(void *pvParameters)
{
	int err;

    (void)OSA_SemaphoreCreate(a2dp_sink_init_done, 0);

    PRINTF("Bluetooth A2dp Bridge demo start...\n");

	/* Initializate BT Host stack */
	err = bt_enable(bt_ready);
	if (err) {
		PRINTF("Bluetooth init failed (err %d)\n", err);
		while (1) {
			vTaskDelay(2000);
		}
	}

#if 1 /* Code change for bridge. */
    OSA_SemaphoreWait(a2dp_sink_init_done, osaWaitForever_c);

    if (NULL == xTaskCreateStatic(unicast_media_sender_task, "unicast_media_sender_task", configMINIMAL_STACK_SIZE * 8, NULL, tskIDLE_PRIORITY + 1, xStack, &xTaskBuffer))
    {
        PRINTF("unicast_media_sender_task creation failed!\r\n");
        while (1)
            ;
    }
#endif

	vTaskDelete(NULL);
}

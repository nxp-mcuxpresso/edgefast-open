/*
 * Copyright 2024-2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
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
#include <zephyr/bluetooth/classic/a2dp.h>
#include <zephyr/bluetooth/classic/a2dp_codec_sbc.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/sbc.h>
#include "clock_config.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "app_connect.h"
#include "app_discover.h"
#include "a2dp_media_data.h"
#include "app_shell.h"

/* User may need to change it for real production */
#define APP_CLASS_OF_DEVICE (0x200000U)

struct bt_a2dp_app {
	struct bt_a2dp *a2dp;
	struct bt_a2dp_ep *sbc_source_ep;
	struct bt_a2dp_ep *sbc_sink_ep;
	struct bt_a2dp_stream sbc_stream;
	struct k_work_delayable initiate_stream_work;
	struct k_work_delayable start_stream_work;
#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	struct bt_a2dp_codec_sbc_params sbc_params;
	bool reconfiguring;
	bool stream_configured;
	bool ready_start;
#endif
	bool a2dp_connect_initiated;
	bool peer_sbc_found;
	volatile bool started;
};

#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
#define APP_A2DP_COUNT (2)
#else
#define APP_A2DP_COUNT (1)
#endif

struct bt_a2dp_app app_a2dps[APP_A2DP_COUNT];

BT_A2DP_SBC_SOURCE_EP_DEFAULT(sbc_source_ep1);
#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
BT_A2DP_SBC_SOURCE_EP_DEFAULT(sbc_source_ep2);
#endif
struct bt_a2dp_codec_ie sbc_sink_ep_capabilities;
static struct bt_a2dp_ep sbc_sink_ep1 = {
	.codec_cap = &sbc_sink_ep_capabilities,
};
#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
struct bt_a2dp_codec_ie sbc_sink_ep_capabilities2;
static struct bt_a2dp_ep sbc_sink_ep2 = {
	.codec_cap = &sbc_sink_ep_capabilities2,
};

#define APP_SBC_SINK_EPS() static struct bt_a2dp_ep *app_sbc_sink_eps[APP_A2DP_COUNT] = {&sbc_sink_ep1, &sbc_sink_ep2}
#define APP_SBC_SOURCE_EPS() static struct bt_a2dp_ep *app_sbc_source_eps[APP_A2DP_COUNT] = {&sbc_source_ep1, &sbc_source_ep2}
#endif
BT_A2DP_SBC_EP_CFG_DEFAULT(sbc_cfg_default, A2DP_SBC_SAMP_FREQ_44100);

/* sbc audio stream control variables */
struct sbc_encoder encoder;
static TimerHandle_t a2dp_src_timer;
static uint32_t a2dp_src_missed_count;
static volatile bool a2dp_src_playback;
static volatile int media_index;
static uint32_t a2dp_src_sf;
static uint8_t a2dp_src_nc;
static uint32_t send_samples_count;
static uint16_t send_count;
static uint8_t send_interval;
/* max pcm data size per interval. The max sample freq is 48K.
 * interval * 48 * 2 (max channels) * 2 (sample width) * 2 (the worst case: send two intervals'
 * data if timer is blocked)
 */
static uint8_t a2dp_pcm_buffer[CONFIG_BT_A2DP_SOURCE_DATA_SEND_INTERVAL * 48 * 2 * 2 * 2];
NET_BUF_POOL_DEFINE(a2dp_tx_pool, CONFIG_BT_A2DP_SOURCE_DATA_BUF_COUNT,
		    BT_L2CAP_BUF_SIZE(CONFIG_BT_A2DP_SOURCE_DATA_BUF_SIZE),
		    CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static void initiate_stream_work_handle(struct k_work *work);
#define CONFIGURE_SBC_DELAY K_MSEC(2000)

NET_BUF_POOL_DEFINE(sdp_discover_pool, 2, BT_L2CAP_BUF_SIZE(CONFIG_BT_L2CAP_TX_MTU),
		    CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

struct k_work_q audio_play_work_q;
static K_KERNEL_STACK_DEFINE(audio_play_work_q_thread_stack, CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE);

static struct bt_sdp_attribute a2dp_source_attrs[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
			BT_SDP_ARRAY_16(BT_SDP_AUDIO_SOURCE_SVCLASS)
		},
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROTO_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 16),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_PROTO_L2CAP)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(BT_UUID_AVDTP_VAL)
			},
			)
		},
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_UUID_AVDTP_VAL)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(AVDTP_VERSION)
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
				BT_SDP_ARRAY_16(BT_SDP_ADVANCED_AUDIO_SVCLASS)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(0x0104U)
			},
			)
		},
		)
	),
	BT_SDP_SERVICE_NAME("A2DPSource"),
	BT_SDP_SUPPORTED_FEATURES(0x0001U),
};

static struct bt_sdp_record a2dp_source_rec = BT_SDP_RECORD(a2dp_source_attrs);

static bool a2dp_produce_media_check(uint32_t samples_num)
{
	uint16_t  medialen;

	/* Music Audio is Stereo */
	medialen = (samples_num * a2dp_src_nc * 2);

	/* need to use a2dp_pcm_buffer to do memcpy */
	if ((a2dp_src_nc == 1) || ((media_index + (samples_num << 2)) > sizeof(media_data))) {
		if (medialen > sizeof(a2dp_pcm_buffer)) {
			return false;
		}
	}

	return true;
}

static uint8_t *a2dp_produce_media(uint32_t samples_num)
{
	uint8_t *media = NULL;
	uint16_t  medialen;

	/* Music Audio is Stereo */
	medialen = (samples_num * a2dp_src_nc * 2);

	/* For mono or dual configuration, skip alternative samples */
	if (a2dp_src_nc == 1) {
		uint16_t index;

		media = (uint8_t *)&a2dp_pcm_buffer[0];
		if (medialen > sizeof(a2dp_pcm_buffer)) {
			PRINTF("need increase a2dp_pcm_buffer\n");
			return NULL;
		}

		for (index = 0; index < samples_num; index++) {
			media[(2 * index)] = *((uint8_t *)media_data + media_index);
			media[(2 * index) + 1] = *((uint8_t *)media_data + media_index + 1);
			/* Update the media index */
			media_index += 4u;
			if (media_index >= sizeof(media_data)) {
				media_index = 0U;
			}
		}
	} else {
		if ((media_index + (samples_num << 2)) > sizeof(media_data)) {
			uint32_t remaining_samples = samples_num;
			uint8_t *media_dst;

			media = (uint8_t *)&a2dp_pcm_buffer[0];
			if (medialen > sizeof(a2dp_pcm_buffer)) {
				PRINTF("need increase a2dp_pcm_buffer\n");
				return NULL;
			}

			media_dst = media;
			while (remaining_samples > 0) {
				uint32_t samples_to_copy = MIN(remaining_samples, (sizeof(media_data) - media_index) >> 2);

				memcpy(media_dst, ((uint8_t *)media_data + media_index), samples_to_copy << 2);
				/* Update the media index */
				media_index += (samples_to_copy << 2);
				if (media_index >= sizeof(media_data)) {
					media_index = 0U;
				}
				media_dst += (samples_to_copy << 2);
				remaining_samples -= samples_to_copy;
			}
		} else {
			media = ((uint8_t *)media_data + media_index);
			/* Update the media index */
			media_index += (samples_num << 2);
			if (media_index >= sizeof(media_data)) {
				media_index = 0U;
			}
		}
	}

	return media;
}

static TickType_t previous_ticks;

static TickType_t audio_play_get_ticks(void)
{
	TickType_t ticks;

	/* Get the current time */
	if (0U != __get_IPSR()) {
		ticks = xTaskGetTickCountFromISR();
	} else {
		ticks = xTaskGetTickCount();
	}

	return ticks;
}

static void audio_play_time_reset(void)
{
	previous_ticks = audio_play_get_ticks();
}

/* return ms */
static uint32_t audio_play_time_delta(void)
{
	TickType_t current_ticks;
	TickType_t ticks;

	current_ticks = audio_play_get_ticks();
	if (current_ticks > previous_ticks) {
		ticks = current_ticks - previous_ticks;
	} else {
		ticks = portMAX_DELAY - previous_ticks + 1 + current_ticks;
	}

	previous_ticks = current_ticks;

	return ((uint32_t)((uint64_t)(ticks)*1000uL / (uint64_t)configTICK_RATE_HZ));
}

static bool audio_play_check(struct bt_a2dp_app* app_a2dp)
{
	uint32_t a2dp_src_num_samples;
	uint32_t pcm_frame_samples;
	uint8_t frame_num;
	struct net_buf *buf;
	uint32_t pdu_len;
	uint32_t cal_interval;

	struct bt_a2dp_stream *stream = &app_a2dp->sbc_stream;

	buf = bt_a2dp_stream_create_pdu(&a2dp_tx_pool, K_FOREVER);
	if (buf->len + sbc_frame_encoded_bytes(&encoder) > bt_a2dp_get_mtu(stream)) {
		net_buf_unref(buf);
		PRINTF("error, the stream mtu is too small\n");
		return false;
	}

	cal_interval = CONFIG_BT_A2DP_SOURCE_DATA_SEND_INTERVAL;

	while (true) {
		if (cal_interval == 0) {
			net_buf_unref(buf);
			PRINTF("error, the stream mtu is too small\n");
			return false;
		}

		a2dp_src_num_samples = (uint16_t)((cal_interval * a2dp_src_sf) / 1000);
		pcm_frame_samples = sbc_frame_samples(&encoder);
		frame_num = a2dp_src_num_samples / pcm_frame_samples;
		if (frame_num * pcm_frame_samples < a2dp_src_num_samples) {
			frame_num++;
		}
		a2dp_src_num_samples = frame_num * pcm_frame_samples;

		pdu_len = buf->len + frame_num * sbc_frame_encoded_bytes(&encoder);

		if (pdu_len > net_buf_tailroom(buf)) {
			PRINTF("Better to increase buf size %d > %d\n", pdu_len,
			       net_buf_tailroom(buf));
			PRINTF("stream interval is changed: %dms -> %dms\n", cal_interval,
			       cal_interval / 2);
			cal_interval /= 2;
			continue;
		}

		if (pdu_len > bt_a2dp_get_mtu(stream)) {
			PRINTF("Better to decrease CONFIG_BT_A2DP_SOURCE_DATA_SEND_INTERVAL"
				"%d > %d\n", pdu_len, bt_a2dp_get_mtu(stream));
			PRINTF("stream interval is changed: %dms -> %dms\n", cal_interval,
			       cal_interval / 2);
			cal_interval /= 2;
			continue;
		}

		if (!a2dp_produce_media_check(a2dp_src_num_samples)) {
			PRINTF("Better to increase a2dp_pcm_buffer\n");
			PRINTF("stream interval is changed: %dms -> %dms\n", cal_interval,
			       cal_interval / 2);
			cal_interval /= 2;
			continue;
		}

		break;
	}

	net_buf_unref(buf);

#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	if ((send_interval == 0) || (send_interval > cal_interval)) {
		send_interval = cal_interval;
	}
#else
	send_interval = cal_interval;
#endif

	PRINTF("send interval is: %dms\n", send_interval);

	return true;
}

static uint32_t get_min_mtu(void)
{
#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	uint32_t mtu_ret = 0xFFFFFFFFu;
	uint32_t mtu;

	for (uint8_t i = 0; i < APP_A2DP_COUNT; i++) {
		if (app_a2dps[i].a2dp != NULL && app_a2dps[i].started) {
			mtu = bt_a2dp_get_mtu(&app_a2dps[i].sbc_stream);
			if (mtu != 0) {
				if (mtu_ret > mtu) {
					mtu_ret = mtu;
				}
			}
		}
	}

	return mtu_ret;
#else
	if (app_a2dps[0].a2dp != NULL && app_a2dps[0].started) {
		return bt_a2dp_get_mtu(&app_a2dps[0].sbc_stream);
	} else {
		return 0xFFFFFFFFu;
	}
#endif
}

static bool audio_stream_started(void)
{
	if (!a2dp_src_playback) {
		return false;
	}

#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	for (uint8_t idx = 0; idx < APP_A2DP_COUNT; ++idx) {
		if (app_a2dps[idx].a2dp == NULL || !app_a2dps[idx].started) {
			return false;
		}
	}
#else
	if (app_a2dps[0].a2dp == NULL || !app_a2dps[0].started) {
		return false;
	}
#endif

	return true;
}

static void audio_work_handler(struct k_work *work)
{
	int64_t period_ms;
	uint32_t a2dp_src_num_samples;
	uint8_t *pcm_data;
	uint8_t index;
	uint32_t pcm_frame_size;
	uint32_t pcm_frame_samples;
	uint32_t encoded_frame_size;
	struct net_buf *buf;
	uint8_t *sbc_hdr;
	uint32_t pdu_len;
	uint32_t out_size;
	int err;
	uint8_t frame_num = 0;
	uint8_t remaining_frame_num;

	/* If stopped then return */
	if (!audio_stream_started()) {
		return;
	}

	period_ms = audio_play_time_delta();

	pcm_frame_size = sbc_frame_bytes(&encoder);
	pcm_frame_samples = sbc_frame_samples(&encoder);
	encoded_frame_size = sbc_frame_encoded_bytes(&encoder);

	/* Get the number of samples */
	a2dp_src_num_samples = (uint16_t)((period_ms * a2dp_src_sf) / 1000);
	a2dp_src_missed_count += (uint32_t)((period_ms * a2dp_src_sf) % 1000);
	a2dp_src_missed_count += ((a2dp_src_num_samples % pcm_frame_samples) * 1000);
	a2dp_src_num_samples = (a2dp_src_num_samples / pcm_frame_samples) * pcm_frame_samples;
	remaining_frame_num = a2dp_src_num_samples / pcm_frame_samples;

	/* Raw adjust for the drift */
	while (a2dp_src_missed_count >= (1000 * pcm_frame_samples)) {
		a2dp_src_num_samples += pcm_frame_samples;
		remaining_frame_num++;
		a2dp_src_missed_count -= (1000 * pcm_frame_samples);
	}

	do {
		buf = bt_a2dp_stream_create_pdu(&a2dp_tx_pool, K_FOREVER);
		if (buf == NULL) {
			/* fail */
			PRINTF("no buf\n");
			return;
		}

		sbc_hdr = net_buf_add(buf, 1u);

		a2dp_src_num_samples = remaining_frame_num * pcm_frame_samples;
		pdu_len = buf->len + remaining_frame_num * encoded_frame_size;

		frame_num = remaining_frame_num;
		/* adjust the buf size */
		while ((pdu_len > net_buf_tailroom(buf)) ||
		       (pdu_len > get_min_mtu()) ||
		       (!a2dp_produce_media_check(a2dp_src_num_samples))) {
			if (!audio_stream_started() || pdu_len <= encoded_frame_size) {
				net_buf_unref(buf);
				return;
			}

			pdu_len -= encoded_frame_size;
			a2dp_src_num_samples -= pcm_frame_samples;
			frame_num--;
		}

		pcm_data = a2dp_produce_media(a2dp_src_num_samples);
		if (pcm_data == NULL) {
			net_buf_unref(buf);
			PRINTF("no media data\n");
			return;
		}

		for (index = 0; index < frame_num; index++) {
			out_size = sbc_encode(&encoder, (uint8_t *)&pcm_data[index * pcm_frame_size],
					      net_buf_tail(buf));
			if (encoded_frame_size != out_size) {
				PRINTF("sbc encode fail\n");
				continue;
			}

			net_buf_add(buf, encoded_frame_size);
		}

		*sbc_hdr = (uint8_t)BT_A2DP_SBC_MEDIA_HDR_ENCODE(frame_num, 0, 0, 0);

#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
		uint8_t streams_count = 0;
		struct bt_a2dp_stream *streams[APP_A2DP_COUNT];

		for (uint8_t idx = 0; idx < APP_A2DP_COUNT; ++idx) {
			if (app_a2dps[idx].a2dp != NULL && app_a2dps[idx].started) {
				streams[idx] = &app_a2dps[idx].sbc_stream;
				streams_count++;
			} else {
				streams[idx] = NULL;
			}
		}

		for (uint8_t idx = 0; idx < APP_A2DP_COUNT; ++idx) {
			if (streams[idx] != NULL) {
				struct net_buf *sent_buf;

				streams_count--;

				if (streams_count == 0) {
					sent_buf = buf;
				} else {
					sent_buf = net_buf_clone(buf, K_FOREVER);
				}

				err = bt_a2dp_stream_send(streams[idx], sent_buf, send_count, send_samples_count);
				if (err != 0) {
					net_buf_unref(sent_buf);
					PRINTF("  Failed to send SBC audio data on streams(%d)\n", err);
				}
			}
		}
#else
		err = bt_a2dp_stream_send(&app_a2dps[0].sbc_stream, buf, send_count, send_samples_count);
		if (err != 0) {
			net_buf_unref(buf);
			PRINTF("  Failed to send SBC audio data on streams(%d)\n", err);
		}
#endif

		send_count++;
		send_samples_count += a2dp_src_num_samples;
		remaining_frame_num -= frame_num;
	} while(remaining_frame_num > 0);
}

static K_WORK_DEFINE(audio_work, audio_work_handler);

static void a2dp_playback_timeout_handler(TimerHandle_t timer_id)
{
	k_work_submit_to_queue(&audio_play_work_q, &audio_work);
}

static void a2dp_stop_playback_timer(void)
{
#if 0
	for (uint8_t idx = 0; idx < APP_A2DP_COUNT; ++idx) {
		if (app_a2dps[idx].started) {
			return;
		}
	}
#endif

	a2dp_src_playback = false;
	if (a2dp_src_timer != NULL) {
		/* Stop Audio Source */
		xTimerStop(a2dp_src_timer, 0);
		xTimerDelete(a2dp_src_timer, 0);
		a2dp_src_timer = NULL;
	}
}

static void a2dp_stop_playback_stream(struct bt_a2dp_app* app_a2dp)
{
	app_a2dp->started = false;
	a2dp_stop_playback_timer();
}

static void a2dp_start_playback_timer(void)
{
#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
#if 0
	if (a2dp_src_playback) {
		return;
	}
#endif
	for (uint8_t idx = 0; idx < APP_A2DP_COUNT; ++idx) {
		if (!app_a2dps[idx].started) {
			return;
		}
	}
#endif

	a2dp_stop_playback_timer();
	a2dp_src_playback = true;
	audio_play_time_reset();
	a2dp_src_timer = xTimerCreate("a2dp play", (send_interval /
				      portTICK_PERIOD_MS), pdTRUE, NULL,
				      a2dp_playback_timeout_handler);
	xTimerStart(a2dp_src_timer, 0);
}

static void a2dp_start_playback_stream(struct bt_a2dp_app* app_a2dp)
{
	app_a2dp->started = true;
	a2dp_start_playback_timer();
}

#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
static struct bt_a2dp_app *get_app_a2dp_by_stream(struct bt_a2dp_stream *stream)
{
	for (uint8_t idx = 0; idx < APP_A2DP_COUNT; ++idx) {
		if (&app_a2dps[idx].sbc_stream == stream) {
			return &app_a2dps[idx];
		}
	}

	return NULL;
}

#define GET_APP_A2DP_BY_STREAM() \
	struct bt_a2dp_app *app_a2dp; \
	do { \
		app_a2dp = get_app_a2dp_by_stream(stream); \
		if (app_a2dp == NULL) { \
			return; \
		} \
	} while(0)
#else
#define GET_APP_A2DP_BY_STREAM() struct bt_a2dp_app *app_a2dp = &app_a2dps[0]
#endif

#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
static bool stream_ready_check(void)
{
	for (uint8_t idx = 0; idx < APP_A2DP_COUNT; ++idx) {
		if (!app_a2dps[idx].ready_start || !app_a2dps[idx].stream_configured) {
			return false;
		}
	}

	return true;
}
#endif

static void start_stream_work_handle(struct k_work *work)
{
	struct bt_a2dp_app *app_a2dp =
		CONTAINER_OF(work, struct bt_a2dp_app, start_stream_work.work);

	if (app_a2dp->a2dp == NULL) {
		return;
	}

#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	if (audio_play_check(app_a2dp)) {
		app_a2dp->ready_start = true;
	}

	if (stream_ready_check()) {
		for (uint8_t idx = 0; idx < APP_A2DP_COUNT; ++idx) {
			bt_a2dp_stream_start(&app_a2dps[idx].sbc_stream);
		}
	} else {
		PRINTF("wait other stream ready to start playing\n");
	}
#else
	if (audio_play_check(app_a2dp)) {
		bt_a2dp_stream_start(&app_a2dp->sbc_stream);
	}
#endif
}

static void stream_check_and_start(struct bt_a2dp_app *app_a2dp)
{
	k_work_init_delayable(&app_a2dp->start_stream_work, start_stream_work_handle);
	k_work_schedule(&app_a2dp->start_stream_work, K_MSEC(500));
}

static void sbc_stream_configured(struct bt_a2dp_stream *stream)
{
	struct sbc_encoder_init_param param;
	struct bt_a2dp_codec_sbc_params *sbc_config = (struct bt_a2dp_codec_sbc_params *)
						      &sbc_cfg_default.codec_config->codec_ie[0];

	PRINTF("stream configured\n");

#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	GET_APP_A2DP_BY_STREAM();

	app_a2dp->stream_configured = true;
	app_a2dp->sbc_params = *sbc_config;
#endif
	a2dp_src_sf = bt_a2dp_sbc_get_sampling_frequency(sbc_config);
	a2dp_src_nc = bt_a2dp_sbc_get_channel_num(sbc_config);

	param.bit_rate = CONFIG_BT_A2DP_SOURCE_SBC_BIT_RATE_DEFAULT;
	param.samp_freq = a2dp_src_sf;
	param.blk_len = bt_a2dp_sbc_get_block_length(sbc_config);
	param.subband = bt_a2dp_sbc_get_subband_num(sbc_config);
	param.alloc_mthd = bt_a2dp_sbc_get_allocation_method(sbc_config);
	param.ch_mode = bt_a2dp_sbc_get_channel_mode(sbc_config);
	param.ch_num = bt_a2dp_sbc_get_channel_num(sbc_config);
	param.min_bitpool = sbc_config->min_bitpool;
	param.max_bitpool = sbc_config->max_bitpool;

	if (sbc_setup_encoder(&encoder, &param) != 0) {
		PRINTF("sbc encoder initialization fail\n");
		return;
	}

#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	if (app_a2dp->reconfiguring) {
		app_a2dp->reconfiguring = false;
		stream_check_and_start(app_a2dp);
		return;
	}
#endif

	bt_a2dp_stream_establish(stream);
}

static void sbc_stream_established(struct bt_a2dp_stream *stream)
{
	GET_APP_A2DP_BY_STREAM();

	PRINTF("stream established\n");

#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	if (memcmp(&app_a2dp->sbc_params, &sbc_cfg_default.codec_config->codec_ie, sizeof(app_a2dp->sbc_params))) {
		/* Two sbc encoders can be used for the two a2dp streams too, but it needs more resource. */
		PRINTF("sbc encoder is not same, need to reconfigure for dual a2dp\n");
		app_a2dp->reconfiguring = true;
		bt_a2dp_stream_reconfig(stream, &sbc_cfg_default);
		return;
	}
#endif
	stream_check_and_start(app_a2dp);
}

static void sbc_stream_released(struct bt_a2dp_stream *stream)
{
	GET_APP_A2DP_BY_STREAM();

#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	app_a2dp->stream_configured = false;
	app_a2dp->ready_start = false;
#endif
	app_a2dp->sbc_source_ep = NULL;
	PRINTF("stream released\n");
	a2dp_stop_playback_stream(app_a2dp);
}

static void sbc_stream_started(struct bt_a2dp_stream *stream)
{
	GET_APP_A2DP_BY_STREAM();

	PRINTF("stream started\n");
	/* Start Audio Source */
	a2dp_start_playback_stream(app_a2dp);
}

static void sbc_stream_suspended(struct bt_a2dp_stream *stream)
{
	GET_APP_A2DP_BY_STREAM();

	PRINTF("stream suspended\n");
	a2dp_stop_playback_stream(app_a2dp);
}

static struct bt_a2dp_stream_ops sbc_stream_ops = {
	.configured = sbc_stream_configured,
	.established = sbc_stream_established,
	.released = sbc_stream_released,
	.started = sbc_stream_started,
	.suspended = sbc_stream_suspended,
};

#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
static struct bt_a2dp_app *get_app_a2dp(struct bt_a2dp *a2dp)
{
	for (uint8_t idx = 0; idx < APP_A2DP_COUNT; ++idx) {
		if (app_a2dps[idx].a2dp == a2dp) {
			return &app_a2dps[idx];
		}
	}

	return NULL;
}

#define GET_APP_A2DP_RET(ret) \
	struct bt_a2dp_app *app_a2dp; \
	do { \
		app_a2dp = get_app_a2dp(a2dp); \
		if (app_a2dp == NULL) { \
			return ret; \
		} \
	} while(0)

#define GET_APP_A2DP() \
	struct bt_a2dp_app *app_a2dp; \
	do { \
		app_a2dp = get_app_a2dp(a2dp); \
		if (app_a2dp == NULL) { \
			return; \
		} \
	} while(0)
#else
#define GET_APP_A2DP_RET(ret) struct bt_a2dp_app *app_a2dp = &app_a2dps[0]
#define GET_APP_A2DP() struct bt_a2dp_app *app_a2dp = &app_a2dps[0]
#endif

static struct bt_a2dp_app *get_app_a2dp_new(struct bt_a2dp *a2dp)
{
#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	for (uint8_t idx = 0; idx < APP_A2DP_COUNT; ++idx) {
		if (app_a2dps[idx].a2dp == a2dp) {
			return &app_a2dps[idx];
		}
	}

	for (uint8_t idx = 0; idx < APP_A2DP_COUNT; ++idx) {
		if (app_a2dps[idx].a2dp == NULL) {
			app_a2dps[idx].a2dp = a2dp;
			return &app_a2dps[idx];
		}
	}

	return NULL;
#else
	return &app_a2dps[0];
#endif
}

static struct bt_a2dp *get_a2dp_by_conn(struct bt_conn *conn)
{
#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	struct bt_conn *get_conn;

	if (conn == NULL) {
		return NULL;
	}

	for (uint8_t idx = 0; idx < APP_A2DP_COUNT; ++idx) {
		if (app_a2dps[idx].a2dp == NULL) {
			continue;
		}

		get_conn = bt_a2dp_get_conn(app_a2dps[idx].a2dp);
		if (get_conn == NULL) {
			continue;
		}

		if (get_conn == conn) {
			bt_conn_unref(get_conn);
			return app_a2dps[idx].a2dp;
		} else {
			bt_conn_unref(get_conn);
		}
	}

	return NULL;
#else
	return app_a2dps[0].a2dp;
#endif

	
}

static void set_app_a2dp_source_ep(struct bt_a2dp_app *app_a2dp)
{
#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	app_a2dp->sbc_source_ep = NULL;

	for (uint8_t idx = 0; idx < APP_A2DP_COUNT; ++idx) {
		if (app_a2dps[idx].sbc_source_ep != NULL) {
			if (app_a2dps[idx].sbc_source_ep == &sbc_source_ep1 &&
			    app_a2dps[idx].stream_configured) {
				app_a2dp->sbc_source_ep = &sbc_source_ep2;
				break;
			} else {
				app_a2dp->sbc_source_ep = &sbc_source_ep1;
				break;
			}
		}
	}

	if (app_a2dp->sbc_source_ep == NULL) {
		app_a2dp->sbc_source_ep = &sbc_source_ep1;
	}
#else
	app_a2dp->sbc_source_ep = &sbc_source_ep1;
#endif
}

static uint8_t a2dp_discover_ep_cb(struct bt_a2dp *a2dp,
		struct bt_a2dp_ep_info *info, struct bt_a2dp_ep **ep)
{
	GET_APP_A2DP_RET(BT_A2DP_DISCOVER_EP_STOP);

	if (app_a2dp->peer_sbc_found) {
		int err;

		bt_a2dp_stream_cb_register(&app_a2dp->sbc_stream, &sbc_stream_ops);
		set_app_a2dp_source_ep(app_a2dp);
		err = bt_a2dp_stream_config(a2dp, &app_a2dp->sbc_stream,
					    app_a2dp->sbc_source_ep, app_a2dp->sbc_sink_ep,
					    &sbc_cfg_default);
		if (err != 0) {
			PRINTF("fail to configure stream\n");
		}

		return BT_A2DP_DISCOVER_EP_STOP;
	}

	if (info != NULL) {
		PRINTF("find one endpoint:");

		if ((info->codec_type == BT_A2DP_SBC) &&
		    (ep != NULL)) {
			PRINTF("it is SBC codec and use it\r\n");

			app_a2dp->peer_sbc_found = true;
			*ep = app_a2dp->sbc_sink_ep; /* get the peer's sbc endpoint */
		} else {
			PRINTF("it is not SBC codec\n");
		}
	}

	return BT_A2DP_DISCOVER_EP_CONTINUE;
}

static struct bt_avdtp_sep_info found_seps[CONFIG_BT_A2DP_SOURCE_EPS_DISCOVER_COUNT];

static struct bt_a2dp_discover_param ep_discover_param = {
	.cb = a2dp_discover_ep_cb,
	.seps_info = &found_seps[0],
	.sep_count = CONFIG_BT_A2DP_SOURCE_EPS_DISCOVER_COUNT,
	.avdtp_version = 0x0104U,
};

void app_a2dp_connected(struct bt_a2dp *a2dp, int err)
{
	struct bt_a2dp_app *app_a2dp = get_app_a2dp_new(a2dp);

	if (app_a2dp == NULL) {
		return;
	}

	app_a2dp->a2dp = a2dp;
	app_a2dp->sbc_source_ep = NULL;

	if (err == 0) {
		if (app_a2dp->a2dp_connect_initiated) {
			initiate_stream_work_handle(&app_a2dp->initiate_stream_work.work);
		} else {
			/* usually the peer headset will configure a2dp_source demo automatically if connecting from peer headset.
			 * there are headsets that doesn't do it.
			 */
			k_work_schedule(&app_a2dp->initiate_stream_work, CONFIGURE_SBC_DELAY);
		}

		PRINTF("a2dp connected success\r\n");
	} else {
		app_a2dp->a2dp = NULL;

		PRINTF("a2dp connected fail\r\n");
	}

	app_a2dp->a2dp_connect_initiated = false;
}

void app_a2dp_disconnected(struct bt_a2dp *a2dp)
{
	GET_APP_A2DP();

	app_a2dp->a2dp = NULL;

	a2dp_stop_playback_stream(app_a2dp);
	PRINTF("a2dp disconnected\r\n");
}

static int app_config_req(struct bt_a2dp *a2dp, struct bt_a2dp_ep *ep,
		struct bt_a2dp_codec_cfg *codec_cfg, struct bt_a2dp_stream **stream,
		uint8_t *rsp_err_code)
{
	GET_APP_A2DP_RET(-EIO);

	app_a2dp->sbc_source_ep = ep;

	k_work_cancel_delayable(&app_a2dp->initiate_stream_work);
	bt_a2dp_stream_cb_register(&app_a2dp->sbc_stream, &sbc_stream_ops);
	*stream = &app_a2dp->sbc_stream;
	*rsp_err_code = 0;

	return 0;
}

void app_config_rsp(struct bt_a2dp_stream *stream, uint8_t rsp_err_code)
{
#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	GET_APP_A2DP_BY_STREAM();

	if (app_a2dp->reconfiguring && rsp_err_code != 0) {
		app_a2dp->reconfiguring = false;
		PRINTF("fail to reconfigure\n");
	}
#endif
}

static uint8_t sdp_discover_cb(struct bt_conn *conn, struct bt_sdp_client_result *result,
			       const struct bt_sdp_discover_params *params)
{
	int err;
	uint16_t value;

	PRINTF("Discover done\n");

	if (result != NULL && result->resp_buf != NULL) {
		err = bt_sdp_get_proto_param(result->resp_buf, BT_SDP_PROTO_L2CAP, &value);

		if (err != 0) {
			PRINTF("PSM is not found\r\n");
		} else if (value == BT_UUID_AVDTP_VAL) {
			struct bt_a2dp *a2dp;

			PRINTF("The A2DP server found, connecting a2dp\r\n");
			err = bt_sdp_get_proto_param(result->resp_buf, BT_UUID_AVDTP_VAL, &value);
			if (err != 0) {
				PRINTF("AVDTP version not found, err %d", err);
				return BT_SDP_DISCOVER_UUID_CONTINUE;
			}

			ep_discover_param.avdtp_version = value;
			a2dp = bt_a2dp_connect(conn);
			if (a2dp == NULL) {
				PRINTF("Fail to create A2DP connection (err %d)\r\n", err);
			} else {
				struct bt_a2dp_app *app_a2dp = get_app_a2dp_new(a2dp);

				if (app_a2dp == NULL) {
					return BT_SDP_DISCOVER_UUID_STOP;
				}
				app_a2dp->a2dp = a2dp;
				app_a2dp->a2dp_connect_initiated = true;
			}
		} else {
			PRINTF("Don't find A2DP server\r\n");
		}
	}

	return BT_SDP_DISCOVER_UUID_STOP;
}

static struct bt_sdp_discover_params sdp_discover = {
	.type = BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
	.func = sdp_discover_cb,
	.pool = &sdp_discover_pool,
	.uuid = BT_UUID_DECLARE_16(BT_SDP_AUDIO_SINK_SVCLASS),
};

void app_sdp_discover_a2dp_sink(struct bt_conn *conn)
{
	int res;
	sdp_discover.func = sdp_discover_cb;
	res = bt_sdp_discover(conn, &sdp_discover);
	if (res) {
		PRINTF("SDP discovery failed: result\r\n");
	} else {
		PRINTF("SDP discovery started\r\n");
	}
}

static uint8_t sdp_discover_version_cb(struct bt_conn *conn, struct bt_sdp_client_result *result,
				       const struct bt_sdp_discover_params *params)
{
	int err;
	uint16_t value;
	struct bt_a2dp *a2dp = get_a2dp_by_conn(conn);

	if (a2dp == NULL) {
		return BT_SDP_DISCOVER_UUID_CONTINUE;
	}

	GET_APP_A2DP_RET(BT_SDP_DISCOVER_UUID_STOP);

	if (result != NULL && result->resp_buf != NULL) {
		err = bt_sdp_get_proto_param(result->resp_buf, BT_SDP_PROTO_L2CAP, &value);

		if (err != 0) {
			PRINTF("PSM is not found\r\n");
		} else if (value == BT_UUID_AVDTP_VAL) {
			err = bt_sdp_get_proto_param(result->resp_buf, BT_UUID_AVDTP_VAL, &value);
			if (err != 0) {
				PRINTF("AVDTP version not found, err %d", err);
				return BT_SDP_DISCOVER_UUID_CONTINUE;
			}

			ep_discover_param.avdtp_version = value;
			app_a2dp->peer_sbc_found = false;
			err = bt_a2dp_discover(a2dp, &ep_discover_param);
			if (err != 0) {
				PRINTF("fail to discover\n");
			}
		} else {
			PRINTF("Don't find A2DP server\r\n");
		}
	}

	return BT_SDP_DISCOVER_UUID_STOP;
}

void app_sdp_discover_a2dp_sink_version(struct bt_conn *conn)
{
	int res;
	sdp_discover.func = sdp_discover_version_cb;
	res = bt_sdp_discover(conn, &sdp_discover);
	if (res) {
		PRINTF("SDP discovery failed: result\r\n");
	} else {
		PRINTF("SDP discovery started\r\n");
	}
}

static void initiate_stream_work_handle(struct k_work *work)
{
	struct bt_conn *conn;
	struct bt_a2dp_app *app_a2dp =
		CONTAINER_OF(work, struct bt_a2dp_app, initiate_stream_work.work);

	if (app_a2dp->a2dp == NULL) {
		return;
	}

	conn = bt_a2dp_get_conn(app_a2dp->a2dp);
	if (conn == NULL) {
		return;
	}

	app_sdp_discover_a2dp_sink_version(conn);
	bt_conn_unref(conn);
}

static struct bt_a2dp_cb a2dp_cb = {
	.connected = app_a2dp_connected,
	.disconnected = app_a2dp_disconnected,
	.config_req = app_config_req,
	.config_rsp = app_config_rsp,
};

static void app_edgefast_a2dp_init(void)
{
#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	APP_SBC_SINK_EPS();
	APP_SBC_SOURCE_EPS();

	for (uint8_t idx = 0; idx < APP_A2DP_COUNT; ++idx) {
		k_work_init_delayable(&app_a2dps[idx].initiate_stream_work,
				      initiate_stream_work_handle);
		app_a2dps[idx].sbc_sink_ep = app_sbc_sink_eps[idx];

		bt_a2dp_register_ep(app_sbc_source_eps[idx], BT_AVDTP_AUDIO, BT_AVDTP_SOURCE);
	}
#else
	k_work_init_delayable(&app_a2dps[0].initiate_stream_work, initiate_stream_work_handle);
	app_a2dps[0].sbc_sink_ep = &sbc_sink_ep1;

	bt_a2dp_register_ep(&sbc_source_ep1, BT_AVDTP_AUDIO, BT_AVDTP_SOURCE);
#endif

	bt_a2dp_register_cb(&a2dp_cb);

	k_work_queue_init(&audio_play_work_q);
	k_work_queue_start(&audio_play_work_q, audio_play_work_q_thread_stack,
		CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE,
		K_PRIO_COOP(CONFIG_SYSTEM_WORKQUEUE_PRIORITY - 2), NULL);
	k_thread_name_set(&audio_play_work_q.thread, "audio play");
}

#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
enum bt_br_conn_req_rsp bt_br_conn_req_cb(const bt_addr_t *addr, uint32_t cod)
{
	return BT_BR_CONN_REQ_ACCEPT_CENTRAL;
}
#endif

static void bt_ready(int err)
{
	struct net_buf *buf = NULL;
	struct bt_hci_cp_write_class_of_device *cp;

	if (err) {
		PRINTF("Bluetooth init failed (err %d)\n", err);
		return;
	}

#if (defined(CONFIG_BT_SETTINGS) && (CONFIG_BT_SETTINGS > 0))
	settings_load();
#endif /* CONFIG_BT_SETTINGS */

	PRINTF("Bluetooth initialized\n");

	buf = bt_hci_cmd_alloc(K_FOREVER);
	if (buf != NULL) {
		cp = net_buf_add(buf, sizeof(*cp));
		sys_put_le24(APP_CLASS_OF_DEVICE, &cp->class_of_device[0]);
		err = bt_hci_cmd_send_sync(BT_HCI_OP_WRITE_CLASS_OF_DEVICE, buf, NULL);
	} else {
		err = -ENOBUFS;
	}

	if (err != 0) {
		PRINTF("setting class of device failed\n");
	}

	app_connect_init();
	app_discover_init();

#if defined(CONFIG_BT_A2DP_SOURCE_DUAL)
	err = bt_br_set_connectable(true, bt_br_conn_req_cb);
#else
	err = bt_br_set_connectable(true, NULL);
#endif
	if (err != 0) {
		PRINTF("BR/EDR set/rest connectable failed (err %d)\n", err);
		return;
	}
	err = bt_br_set_discoverable(true, false);
	if (err != 0) {
		PRINTF("BR/EDR set discoverable failed (err %d)\n", err);
		return;
	}
	PRINTF("BR/EDR set connectable and discoverable done\n");
	bt_sdp_register_service(&a2dp_source_rec);
	app_edgefast_a2dp_init();
	app_shell_init();
}

void app_a2dp_source_task(void *pvParameters)
{
	int err;

	PRINTF("Bluetooth A2dp Source demo start...\n");

	/* Initializate BT Host stack */
	err = bt_enable(bt_ready);
	if (err) {
		PRINTF("Bluetooth init failed (err %d)\n", err);
		return;
	}

	vTaskDelete(NULL);
}

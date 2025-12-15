/*
 * Copyright 2024-2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include "audio_buf.h"
#include "codec_play.h"
#include "board.h"
#include "fsl_adapter_audio.h"
#include "fsl_codec_common.h"
#include "fsl_codec_adapter.h"
#include "porting.h"

/*If codec is cs42448, it doesn't support DAC CHANNAL, the macro A2DP_CODEC_DAC_VOLUME is meaningless.*/
#define A2DP_CODEC_DAC_VOLUME (100U) /* Range: 0 ~ 100 */
#define A2DP_CODEC_HP_VOLUME  (100U)  /* Range: 0 ~ 100 */

/* audio stream control variables */
static volatile bool audio_start;
static uint32_t audio_sample_rate;
static uint8_t *audio_data_sync_buf[CONFIG_A2DP_BOARD_CODEC_PLAY_COUNT];
static uint32_t audio_data_sync_buf_size[CONFIG_A2DP_BOARD_CODEC_PLAY_COUNT];
static uint8_t audio_data_sync_buf_w;
static uint8_t audio_data_sync_buf_r;
static AT_NONCACHEABLE_SECTION_ALIGN(uint8_t a2dp_silence_data[A2DP_SBC_DATA_PLAY_SIZE_48K], 4);

extern hal_audio_config_t audioTxConfig;
extern codec_config_t boardCodecConfig;
AT_NONCACHEABLE_SECTION_ALIGN(static HAL_AUDIO_HANDLE_DEFINE(audio_tx_handle), 4);
static codec_handle_t codec_handle;

static QueueHandle_t codec_sem;

extern uint32_t BOARD_SwitchAudioFreq(uint32_t sampleRate);

static void codec_tx_callback(hal_audio_handle_t handle, hal_audio_status_t completionStatus, void *callbackParam)
{
	if (0U != __get_IPSR()) {
		portBASE_TYPE task_to_wake = pdFALSE;

		if (pdTRUE == xSemaphoreGiveFromISR(codec_sem, &task_to_wake)) {
			portYIELD_FROM_ISR((task_to_wake));
		}
	} else {
		xSemaphoreGive(codec_sem);
	}
}

void codec_play_configure(uint32_t sample_rate, uint8_t sample_width, uint8_t channels)
{
	audioTxConfig.sampleRate_Hz  = sample_rate;
	audioTxConfig.lineChannels = (hal_audio_channel_t)channels;
	audioTxConfig.srcClock_Hz = BOARD_SwitchAudioFreq(audioTxConfig.sampleRate_Hz);

	PRINTF("a2dp configure sample rate %dHz\r\n", audioTxConfig.sampleRate_Hz);

	HAL_AudioTxInit((hal_audio_handle_t)&audio_tx_handle[0], &audioTxConfig);
	HAL_AudioTxInstallCallback((hal_audio_handle_t)&audio_tx_handle[0], codec_tx_callback, NULL);

	if (CODEC_Init(&codec_handle, &boardCodecConfig) != kStatus_Success) {
	    PRINTF("codec init failed!\r\n");
	}
	CODEC_SetMute(&codec_handle, kCODEC_PlayChannelHeadphoneRight | kCODEC_PlayChannelHeadphoneLeft, true);
	CODEC_SetFormat(&codec_handle, audioTxConfig.srcClock_Hz, audioTxConfig.sampleRate_Hz, audioTxConfig.bitWidth);
	CODEC_SetVolume(&codec_handle, kCODEC_VolumeDAC, A2DP_CODEC_DAC_VOLUME);
	CODEC_SetVolume(&codec_handle, kCODEC_VolumeHeadphoneLeft | kCODEC_VolumeHeadphoneRight, A2DP_CODEC_HP_VOLUME);
	CODEC_SetMute(&codec_handle, kCODEC_PlayChannelHeadphoneRight | kCODEC_PlayChannelHeadphoneLeft, false);
}

void codec_play_deconfigure(void)
{
	CODEC_SetMute(&codec_handle, kCODEC_PlayChannelHeadphoneRight | kCODEC_PlayChannelHeadphoneLeft, true);
	HAL_AudioTxDeinit((hal_audio_handle_t)&audio_tx_handle[0]);
	(void)BOARD_SwitchAudioFreq(0U);
}

static void codec_play_data(uint8_t *data, uint32_t length)
{
	hal_audio_transfer_t xfer;

	audio_data_sync_buf[audio_data_sync_buf_w % CONFIG_A2DP_BOARD_CODEC_PLAY_COUNT] = data;
	audio_data_sync_buf_size[audio_data_sync_buf_w % CONFIG_A2DP_BOARD_CODEC_PLAY_COUNT] =
											length;
	audio_data_sync_buf_w++;

	if (!audio_start) {
		return;
	}

	if ((data != NULL) && (length != 0U)) {
		xfer.dataSize = length;
		xfer.data = data;
	} else {
		xfer.dataSize = audio_sample_rate == 48000 ?
				A2DP_SBC_DATA_PLAY_SIZE_48K : A2DP_SBC_DATA_PLAY_SIZE_44_1K;
		xfer.data = a2dp_silence_data;
	}

	if (kStatus_HAL_AudioSuccess != HAL_AudioTransferSendNonBlocking((hal_audio_handle_t)&audio_tx_handle[0], &xfer)) {
		PRINTF("prime fail\r\n");
	}
}

void codec_play_start(void)
{
	if (audio_start) {
		return;
	}

	audio_start = true;

	for (uint8_t i = 0; i < CONFIG_A2DP_BOARD_CODEC_PLAY_COUNT; i++) {
		codec_play_data(a2dp_silence_data,
				audio_sample_rate == 48000 ?
				A2DP_SBC_DATA_PLAY_SIZE_48K : A2DP_SBC_DATA_PLAY_SIZE_44_1K);
	}
}

void codec_play_stop(void)
{
	if (!audio_start) {
		return;
	}

	audio_start = false;
	HAL_AudioTransferAbortSend((hal_audio_handle_t)&audio_tx_handle[0]);
}

static void codec_play_task(void *handle)
{
	uint8_t *get_data;
	uint32_t length;

	codec_sem = xSemaphoreCreateCounting(0xFF, 0U);
	if (codec_sem == NULL) {
		vTaskDelete(NULL);
	}

	while (1U) {
		if (pdFALSE == xSemaphoreTake(codec_sem, portMAX_DELAY)) {
			continue;
		}

		/* sync the already played media data */
		audio_media_sync(audio_data_sync_buf[audio_data_sync_buf_r %
				CONFIG_A2DP_BOARD_CODEC_PLAY_COUNT],
				audio_data_sync_buf_size[audio_data_sync_buf_r %
				CONFIG_A2DP_BOARD_CODEC_PLAY_COUNT]);

		audio_data_sync_buf_r++;

		if (!audio_start) {
			continue;
		}

		if (audio_sample_rate == 44100) {
			length = A2DP_SBC_DATA_PLAY_SIZE_44_1K;
		} else {
			length = A2DP_SBC_DATA_PLAY_SIZE_48K;
		}
		/* play data */
		audio_get_pcm_data(&get_data, length);
		codec_play_data(get_data, length);
	}
}

int codec_play_init(void)
{
	BaseType_t result = 0;
	result =
		xTaskCreate(codec_play_task, "codec", 2048U, NULL, tskIDLE_PRIORITY + 1, NULL);
	assert(pdPASS == result);
	(void)result;

	return 0;
}

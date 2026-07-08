/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Board-level SCO audio platform extension implementation for frdmimxrt1152.
 *
 * This file implements the sco_audio_pl_ext interface using direct hardware
 * PCM routing from the Bluetooth module to the WM8962 codec (SAI PCM bus),
 * bypassing application-layer audio streaming. It is compiled in place of
 * examples/handsfree_ag/sco_audio_pl_ext.c when
 * CONFIG_HANDSFREE_BT_PCM_TO_CODEC_DIRECT is enabled.
 */

#include "sco_audio_pl_ext.h"
#include "fsl_codec_common.h"
#include "fsl_codec_adapter.h"
#include "fsl_debug_console.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define SCO_CODEC_MCLK_HZ     (24576000U)
#define SCO_CODEC_DAC_VOLUME  (75U) /* Range: 0 ~ 100 */
#define SCO_CODEC_HP_VOLUME   (70U) /* Range: 0 ~ 100 */

/*******************************************************************************
 * External references provided by hardware_init.c
 ******************************************************************************/

extern codec_config_t boardCodecScoConfig;

/*******************************************************************************
 * Private state
 ******************************************************************************/

static codec_handle_t s_codec_handle;
static sco_audio_ep_info s_ep_info;

/*******************************************************************************
 * API
 ******************************************************************************/

void sco_audio_init_pl_ext(void)
{
    /* Nothing to do for direct-PCM path at init time. */
}

void sco_audio_shutdown_pl_ext(void)
{
    /* Nothing to do for direct-PCM path at shutdown time. */
}

int sco_audio_setup_pl_ext(sco_audio_ep_info *ep_info)
{
    if (ep_info == NULL)
    {
        return -1;
    }

    memcpy(&s_ep_info, ep_info, sizeof(sco_audio_ep_info));

    return 0;
}

int sco_audio_start_pl_ext(void)
{
    status_t status;

    status = CODEC_Init(&s_codec_handle, &boardCodecScoConfig);
    if (status != kStatus_Success)
    {
        PRINTF("SCO direct: codec init failed (status %d)\r\n", (int)status);
        return -1;
    }

    /* walkaround for codec api not support modify LRSWAP and BCLKINV */
    wm8962_handle_t *handle = (wm8962_handle_t *)((uintptr_t)s_codec_handle.codecDevHandle);
    /* LRSWAP */
    status = WM8962_ModifyReg(handle, WM8962_IFACE0, WM8962_IFACE1_ALRSWAP_MASK, WM8962_IFACE1_ALRSWAP(WM8962_IFACE1_ADCCH_SWAP));
    if (status != kStatus_Success)
    {
        PRINTF("SCO direct: codec set LRSWAP failed (status %d)\r\n", (int)status);
        return -1;
    }
    /* BCLKINV sample at failing edge */
    status = WM8962_ModifyReg(handle, WM8962_IFACE0, WM8962_IFACE1_BCLKINV_MASK, WM8962_IFACE1_BCLKINV(WM8962_IFACE1_BCLK_INVERT));
    if (status != kStatus_Success)
    {
        PRINTF("SCO direct: codec set BCLKINV failed (status %d)\r\n", (int)status);
        return -1;
    }

    CODEC_SetMute(&s_codec_handle,
                  kCODEC_PlayChannelHeadphoneRight | kCODEC_PlayChannelHeadphoneLeft,
                  true);
    CODEC_SetFormat(&s_codec_handle, SCO_CODEC_MCLK_HZ,
                    s_ep_info.sampl_freq, s_ep_info.sample_len);
    CODEC_SetVolume(&s_codec_handle, kCODEC_VolumeDAC, SCO_CODEC_DAC_VOLUME);
    CODEC_SetVolume(&s_codec_handle,
                    kCODEC_VolumeHeadphoneLeft | kCODEC_VolumeHeadphoneRight,
                    SCO_CODEC_HP_VOLUME);
    CODEC_SetMute(&s_codec_handle,
                  kCODEC_PlayChannelHeadphoneRight | kCODEC_PlayChannelHeadphoneLeft,
                  false);

    return 0;
}

int sco_audio_stop_pl_ext(void)
{
    status_t status = CODEC_Deinit(&s_codec_handle);
    if (status != kStatus_Success)
    {
        PRINTF("SCO direct: codec deinit failed (status %d)\r\n", (int)status);
        return -1;
    }
    return 0;
}

/* Ringtone is not supported on the direct-PCM path. */
void sco_audio_play_ringtone_pl_ext(void)
{
}

void sco_audio_play_ringtone_exit_pl_ext(void)
{
}

int sco_audio_set_speaker_volume(uint8_t volume)
{
    (void)volume;
    return 0;
}

int platform_audio_play_ringtone(void)
{
    return 0;
}

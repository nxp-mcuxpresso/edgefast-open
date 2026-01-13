/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SCO_AUDIO_PL_EXT_H_
#define _SCO_AUDIO_PL_EXT_H_

#include <stdint.h>

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * API
 ******************************************************************************/

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _sco_audio_ep_info
{
    /**
     * Sample Lenght(in bits)
     */
    uint8_t   sample_len;

    /**
     * Sampling Frequency
     */
    uint16_t  sampl_freq;

}sco_audio_ep_info;

/*!
 * @brief Initialize SCO audio platform extension.
 *
 * This function initializes the SCO audio platform extension.
 */
void sco_audio_init_pl_ext(void);

/*!
 * @brief Shutdown SCO audio platform extension.
 *
 * This function shuts down the SCO audio platform extension.
 */
void sco_audio_shutdown_pl_ext(void);

/*!
 * @brief Setup SCO audio with endpoint information.
 *
 * @param ep_info Pointer to SCO audio endpoint information structure.
 * @return 0 on success, negative value on error.
 */
int sco_audio_setup_pl_ext(sco_audio_ep_info *ep_info);

/*!
 * @brief Start SCO audio streaming.
 *
 * @return 0 on success, negative value on error.
 */
int sco_audio_start_pl_ext(void);

/*!
 * @brief Stop SCO audio streaming.
 *
 * @return 0 on success, negative value on error.
 */
int sco_audio_stop_pl_ext(void);

/*!
 * @brief Play ringtone.
 *
 * This function starts playing the ringtone.
 */
void sco_audio_play_ringtone_pl_ext(void);

/*!
 * @brief Exit ringtone playback.
 *
 * This function stops the ringtone playback and restores SCO audio if needed.
 */
void sco_audio_play_ringtone_exit_pl_ext(void);

/*!
 * @brief Set speaker volume.
 *
 * @param volume Volume level (0-15 for HFP).
 * @return 0 on success, -1 on error.
 */
int sco_audio_set_speaker_volume(uint8_t volume);

/*!
 * @brief Play ringtone platform function.
 *
 * @return 0 on success, negative value on error.
 */
int platform_audio_play_ringtone(void);

#if defined(__cplusplus)
}
#endif

#endif /* _SCO_AUDIO_PL_EXT_H_ */

/*
 * Copyright 2025 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/types.h>
#include <stdio.h>
#include <stddef.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>
#include <zephyr/toolchain.h>
#include <porting.h>
#include "fsl_debug_console.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/byteorder.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/audio.h>

#include "umr2bms.h"

#include "le_audio_shell.h"

#include "broadcast_media_sender.h"
#include "unicast_media_receiver.h"

static StackType_t umr_stack[ configMINIMAL_STACK_SIZE * 8 ];
static StaticTask_t umr_task_buffer;

enum {
	UMR_TO_BMS_FLAG_AUDIO_ENABLED,
	UMR_TO_BMS_FLAG_AUDIO_STARTED,
	UMR_TO_BMS_FLAG_AUDIO_STOPED,
	UMR_TO_BMS_FLAG_AUDIO_DISABLED,
	UMR_TO_BMS_FLAG_AUDIO_RELEASED,
};

static atomic_t flags;
static StaticSemaphore_t sem_buff;
static SemaphoreHandle_t sem;

static int change_trigger_pin_mapping(void)
{
    struct net_buf *buf;
    int err;

    buf = bt_hci_cmd_alloc(K_FOREVER);
    if (!buf) {
        PRINTF("Unable to allocate buffer");
        return -ENOBUFS;
    }

    net_buf_add_u8(buf, 0x01);
    net_buf_add_u8(buf, 0x01);

    err = bt_hci_cmd_send_sync(BT_OP(BT_OGF_VS, 0x029A), buf, NULL);
    if (err) {
        PRINTF("Failed to send HCI command: %d", err);
        return err;
    }

    return 0;
}

void umr_to_bms_task(void *param)
{
	int err;

#if (defined(CONFIG_BT_SMP) && (CONFIG_BT_SMP > 0))
	extern void bt_psa_crypto_init(void);
	bt_psa_crypto_init();
#endif /* CONFIG_BT_SMP */

	sem = xSemaphoreCreateCountingStatic(0xff, 0, &sem_buff);

	/* shell init. */
	le_audio_shell_init();

	/* bluetooth init. */
	err = bt_enable(NULL);
	if (err != 0) {
		PRINTF("Bluetooth init failed (err %d)\r\n", err);
		while(1);
	}

	PRINTF("Bluetooth initialized\r\n");

	/* Change trigger pin mapping */
	err = change_trigger_pin_mapping();
	if (err) {
		PRINTF("Fail to change trigger pin mapping (err %d)\r\n", err);
	}

	if (NULL == xTaskCreateStatic(unicast_media_receiver_task, "unicast_media_receiver_task", configMINIMAL_STACK_SIZE * 8, NULL, tskIDLE_PRIORITY + 1, umr_stack, &umr_task_buffer))
	{
		PRINTF("unicast_media_receiver_task creation failed!\r\r\n");
		while (1)
		;
	}

	while (1) {
		xSemaphoreTake(sem, portMAX_DELAY);
		if (atomic_test_and_clear_bit(&flags, UMR_TO_BMS_FLAG_AUDIO_ENABLED)) {
			bms_init();
		}
		if (atomic_test_and_clear_bit(&flags, UMR_TO_BMS_FLAG_AUDIO_STARTED)) {
			bms_start();
		}
		if (atomic_test_and_clear_bit(&flags, UMR_TO_BMS_FLAG_AUDIO_STOPED)) {
			bms_stop();
		}
		if (atomic_test_and_clear_bit(&flags, UMR_TO_BMS_FLAG_AUDIO_DISABLED)) {
			bms_stop();
		}
		if (atomic_test_and_clear_bit(&flags, UMR_TO_BMS_FLAG_AUDIO_RELEASED)) {
			bms_deinit();
		}
	}
}

void umr_to_bms_vol_control(uint8_t volume, uint8_t mute)
{
	if (mute) {
		atomic_set_bit(&flags, UMR_TO_BMS_FLAG_AUDIO_STOPED);
	} else {
		atomic_set_bit(&flags, UMR_TO_BMS_FLAG_AUDIO_STARTED);
	}
	xSemaphoreGive(sem);
}

int umr_to_bms_audio_codec_cfg(const struct bt_audio_codec_cfg *cfg)
{
	return bms_codec_cfg(cfg);
}

int umr_to_bms_audio_codec_qos(const struct bt_bap_qos_cfg *qos)
{
	return bms_qos(qos);
}

int umr_to_bms_audio_enable(void)
{
	return 0;
}

int umr_to_bms_audio_start(void)
{
	atomic_set_bit(&flags, UMR_TO_BMS_FLAG_AUDIO_ENABLED);
	atomic_set_bit(&flags, UMR_TO_BMS_FLAG_AUDIO_STARTED);
	xSemaphoreGive(sem);
	return 0;
}

int umr_to_bms_audio_stop(void)
{
	atomic_set_bit(&flags, UMR_TO_BMS_FLAG_AUDIO_STOPED);
	xSemaphoreGive(sem);
	return 0;
}

int umr_to_bms_audio_disable(void)
{
	atomic_set_bit(&flags, UMR_TO_BMS_FLAG_AUDIO_DISABLED);
	xSemaphoreGive(sem);
	return 0;
}

int umr_to_bms_audio_release(void)
{
	atomic_set_bit(&flags, UMR_TO_BMS_FLAG_AUDIO_RELEASED);
	xSemaphoreGive(sem);
	return 0;
}

int umr_to_bms_stream_recv(enum bt_audio_location chan_allocation, const struct bt_iso_recv_info *info, struct net_buf *buf)
{
	if ((info->flags & BT_ISO_FLAGS_VALID) == 0) {
		buf = NULL;
	}
	return bms_stream_send(chan_allocation, buf);
}
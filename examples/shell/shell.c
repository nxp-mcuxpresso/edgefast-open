/*
 * Copyright 2021 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/shell/shell.h>

#include "shell.h"

#include "fsl_debug_console.h"

extern serial_handle_t g_serialHandle;

static struct shell shell;
struct shell *ctx_shell = NULL;

SDK_ALIGN(static uint8_t shell_handle_buffer[SHELL_HANDLE_SIZE], 4);
static shell_handle_t shell_handle;

void shell_task(void *pvParameters)
{
    PRINTF("Edgefast Bluetooth PAL shell demo start...\n");

    ctx_shell = &shell;

    shell_handle = (shell_handle_t)&shell_handle_buffer[0];
    ctx_shell->sh = shell_handle;

    if (kStatus_SHELL_Success != SHELL_Init(shell_handle, g_serialHandle, CONFIG_SHELL_PROMPT))
    {
        PRINTF("Shell initialization failed!\r\n");
        return;
    }

    while (1)
    {
        vTaskDelay(1000);
    }
}

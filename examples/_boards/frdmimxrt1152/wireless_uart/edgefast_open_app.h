/*
 *  Copyright 2022 NXP
 *  All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "edgefast_open_config.h"

#define FLASH_ADAPTER_SIZE          0x10000
#define OSA_USED                    1
#define PCM_MODE_CONFIG_TX_CLK_SYNC 1

#ifndef BOARD_USER_BUTTON_GPIO
#define BOARD_USER_BUTTON_GPIO (13)
#endif
#ifndef BOARD_USER_BUTTON_GPIO_PIN
#define BOARD_USER_BUTTON_GPIO_PIN (0)
#endif
#define BOARD_USER_BUTTON_IRQ  GPIO13_Combined_0_31_IRQn
#define BOARD_USER_BUTTON_NAME "SW6"
#define BUTTON_TASK_STACK_SIZE (1500U)

#if defined(WIFI_IW612_BOARD_MURATA_2EL_M2)
#include "wifi_config.h"
#else
#error The transceiver module is unsupported
#endif

#if defined(WIFI_IW612_BOARD_MURATA_2EL_M2)
#undef SD_TIMING_MAX
#define SD_TIMING_MAX kSD_TimingDDR50Mode
#endif /*#define WIFI_IW612_BOARD_MURATA_2EL_M2*/

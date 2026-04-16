/*
 * Copyright 2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*${header:start}*/
#include "clock_config.h"
#include "board.h"
#include "pin_mux.h"
#include "fsl_debug_console.h"
#include "fsl_power.h"
#if defined(CONFIG_BT_HOST_CRYPTO) && !defined(CONFIG_BT_CRYPTO_PSA)
#include "ksdk_mbedtls.h"
#endif /* CONFIG_BT_HOST_CRYPTO && !CONFIG_BT_CRYPTO_PSA*/
#if (((defined(CONFIG_BT_SMP)) && (CONFIG_BT_SMP)))
#include "mcux_mbedtls_psa_crypto_config.h"
#endif /* CONFIG_BT_SMP */
/*${header:end}*/

/* On FRDM-RW612 boards, the Xtal32k is present but not connected by default
 * An hardware rework is needed to connect the Xtal32k, so disable this flag with caution */
#ifndef gBoardUseFro32k_d
#define gBoardUseFro32k_d 1
#endif

/*${function:start}*/

void BOARD_InitHardware(void)
{
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
    BOARD_InitSleepPinConfig();
#if defined(CONFIG_BT_HOST_CRYPTO) && !defined(CONFIG_BT_CRYPTO_PSA)
    CRYPTO_InitHardware();
#endif /* CONFIG_BT_HOST_CRYPTO && !CONFIG_BT_CRYPTO_PSA*/
}

/*${function:end}*/

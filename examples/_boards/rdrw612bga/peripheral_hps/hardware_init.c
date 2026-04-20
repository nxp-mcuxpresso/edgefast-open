/*
 * Copyright 2020 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*${header:start}*/
#include "clock_config.h"
#include "board.h"
#include "pin_mux.h"
#include "fsl_debug_console.h"
#include "controller.h"
#include "fsl_power.h"
#if defined(CONFIG_BT_HOST_CRYPTO) && !defined(CONFIG_BT_CRYPTO_PSA)
#include "ksdk_mbedtls.h"
#endif /* CONFIG_BT_HOST_CRYPTO && !CONFIG_BT_CRYPTO_PSA*/
#if (((defined(CONFIG_BT_SMP)) && (CONFIG_BT_SMP)))
#include "mcux_mbedtls_psa_crypto_config.h"
#endif /* CONFIG_BT_SMP */
/*${header:end}*/

/*${function:start}*/

void BOARD_InitHardware(void)
{
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
#if defined(CONFIG_BT_HOST_CRYPTO) && !defined(CONFIG_BT_CRYPTO_PSA)
    CRYPTO_InitHardware();
#endif /* CONFIG_BT_HOST_CRYPTO && !CONFIG_BT_CRYPTO_PSA*/
}

/*${function:end}*/

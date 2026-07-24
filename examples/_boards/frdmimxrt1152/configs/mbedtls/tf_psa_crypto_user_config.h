/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TF_PSA_CRYPTO_USER_CONFIG_H
#define TF_PSA_CRYPTO_USER_CONFIG_H

#include "FreeRTOS.h"

#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_STD_CALLOC pvPortCalloc
#define MBEDTLS_PLATFORM_STD_FREE   vPortFree

#endif /* TF_PSA_CRYPTO_USER_CONFIG_H */

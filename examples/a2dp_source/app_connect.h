/*
 * Copyright 2020 - 2021 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __APP_CONNECT_H__
#define __APP_CONNECT_H__

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * API
 ******************************************************************************/

void app_connect_init(void);

void app_connect(uint8_t *addr);

void app_disconnect(void);

#endif /* __APP_CONNECT_H__ */
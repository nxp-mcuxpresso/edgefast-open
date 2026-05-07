/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __APPL_PERIPHERAL_HFP_AG_MAIN_H__
#define __APPL_PERIPHERAL_HFP_AG_MAIN_H__

#include <zephyr/bluetooth/classic/pbap.h>

struct app_pbap_pce
{
    struct bt_pbap_pce pbap_pce;
    uint32_t conn_id;
    struct net_buf *tx_buf;
    bool goep_v2;
    bool first_recv;
    uint8_t srm_srmp;
    uint8_t set_phone_book_count;
    uint8_t current_state;
};

void pbap_pce_task(void *param);

#endif /* __APPL_PERIPHERAL_HFP_AG_MAIN_H__ */

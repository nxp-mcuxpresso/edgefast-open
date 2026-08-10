/*
 * Copyright 2024-2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SHELL_DTM_NXP_H__
#define __SHELL_DTM_NXP_H__

#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/hci.h>

/* BT_BD_ADDR_SIZE may not be defined in Zephyr hci_types */
#ifndef BT_BD_ADDR_SIZE
#define BT_BD_ADDR_SIZE 6
#endif

/* BR/EDR test OGF (BT Spec Vol 4 Part E) */
#define BT_OGF_TEST                 0x06U

/* HCI Enable Device Under Test Mode (OGF=0x06, OCF=0x03) */
#define BT_HCI_OP_ENABLE_TEST_MODE  BT_OP(BT_OGF_TEST, 0x03U)

/* NXP vendor-specific LE TX power configuration */
#define BT_HCI_OP_LE_SET_TX_POWER   BT_OP(BT_OGF_VS, 0x0087U)

/* NXP vendor-specific BR/EDR TX/RX test */
#define BT_HCI_OP_TX_TEST           BT_OP(BT_OGF_VS, 0x0019U)
#define BT_HCI_OP_RX_TEST           BT_OP(BT_OGF_VS, 0x0018U)

/* LE TX power config payload */
struct bt_hci_le_config {
    uint8_t tx_power;
    uint8_t feloss;
} __packed;

/* NXP vendor-specific BR/EDR transmitter test payload */
struct bt_hci_cp_tx_test {
    uint8_t rx_on_start;
    uint8_t synt_on_start;
    uint8_t tx_on_start;
    uint8_t phd_off_start;
    uint8_t test_scenario;
    uint8_t hopping_mode;
    uint8_t tx_channel;
    uint8_t rx_channel;
    uint8_t tx_test_interval;
    uint8_t pkt_type;
    uint8_t data_length[2];
    uint8_t whitening;
    uint8_t num_pkt[4];
    uint8_t tx_pwr;
} __packed;

/* NXP vendor-specific BR/EDR receiver test payload */
struct bt_hci_cp_rx_test {
    uint8_t test_scenario;
    uint8_t tx_channel;
    uint8_t rx_channel;
    uint8_t pkt_type;
    uint8_t num_pkt[4];
    uint8_t data_length[2];
    uint8_t tx_am_addr;
    uint8_t tx_addr[BT_BD_ADDR_SIZE];
    uint8_t report_err_pkt;
} __packed;

#endif /* __SHELL_DTM_NXP_H__ */

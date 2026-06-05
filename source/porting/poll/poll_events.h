/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __BT_BLE_POLL_EVENTS_H__
#define __BT_BLE_POLL_EVENTS_H__

#define _POLL_EVENT_OBJ_INIT(obj) \
	.poll_events = {NULL},
#define _POLL_EVENT sys_slist_t poll_events

#endif /* __BT_BLE_POLL_EVENTS_H__ */

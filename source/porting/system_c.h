/*
 * Copyright 2024 NXP
 * Copyright (c) 2016, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __EDGEFAST_BT_BLE_SYSTEM_H__
#define __EDGEFAST_BT_BLE_SYSTEM_H__

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <strings.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "event_groups.h"

#include <zephyr/types.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/check.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#if DT_HAS_CHOSEN(zephyr_bt_hci)

extern struct device bt_hci_h4_device;

#define __device_dts_ord_DT_CHOSEN_zephyr_bt_hci_ORD (bt_hci_h4_device)
#define DT_N_INST_0_nxp_bt_hci_uart_PARENT DT_N_INST_0_zephyr_bt_hci_uart
#define DT_N_INST_0_nxp_bt_hci_uart_PARENT_PARENT DT_N_INST_0_hci_uart
#define DT_N_INST_0_zephyr_bt_hci_uart_PARENT DT_N_INST_0_hci_uart
#define __device_dts_ord_DT_N_INST_0_hci_uart_ORD (bt_hci_h4_device)

#if IS_ENABLED(DT_N_INST_0_nxp_bt_hci_uart_P_sdio_reset_gpios_EXISTS)
extern struct device bt_hci_h4_sdio_reset_gpio;
#define __device_dts_ord_DT_N_INST_0_nxp_bt_hci_uart_P_sdio_reset_gpios_IDX_0_PH_ORD (bt_hci_h4_sdio_reset_gpio)
#endif /* IS_ENABLED(DT_N_INST_0_nxp_bt_hci_uart_P_sdio_reset_gpios_EXISTS) */

#if IS_ENABLED(DT_N_INST_0_nxp_bt_hci_uart_P_w_disable_gpios_EXISTS)
extern struct device bt_hci_h4_w_disable_gpio;
#define __device_dts_ord_DT_N_INST_0_nxp_bt_hci_uart_P_w_disable_gpios_IDX_0_PH_ORD (bt_hci_h4_w_disable_gpio)
#endif /* IS_ENABLED(DT_N_INST_0_nxp_bt_hci_uart_P_w_disable_gpios_EXISTS) */

// #define DT_GPIO_CTLR_BY_IDX(node_id, prop, idx) DT_PHANDLE_BY_IDX(node_id, prop, idx)
// #define DT_GPIO_PIN_BY_IDX(node_id, prop, idx) DT_PHA_BY_IDX(node_id, prop, idx, pin)
// #define DT_GPIO_FLAGS_BY_IDX(node_id, prop, idx) DT_PHA_BY_IDX_OR(node_id, prop, idx, flags, 0)

// /** GPIO pin is active (has logical value '1') in low state. */
// #define GPIO_ACTIVE_LOW         (1 << 0)
// /** GPIO pin is active (has logical value '1') in high state. */
// #define GPIO_ACTIVE_HIGH        (0 << 0)

#endif /* DT_CHOSEN_zephyr_bt_hci_EXISTS */

#define SYS_PORT_TRACING_FUNC(type, func, ...) do { } while (false)
#define SYS_PORT_TRACING_FUNC_ENTER(type, func, ...) do { } while (false)
#define SYS_PORT_TRACING_FUNC_BLOCKING(type, func, ...) do { } while (false)
#define SYS_PORT_TRACING_FUNC_EXIT(type, func, ...) do { } while (false)
#define SYS_PORT_TRACING_OBJ_INIT(obj_type, obj, ...) do { } while (false)
#define SYS_PORT_TRACING_OBJ_FUNC(obj_type, func, obj, ...) do { } while (false)
#define SYS_PORT_TRACING_OBJ_FUNC_ENTER(obj_type, func, obj, ...) do { } while (false)
#define SYS_PORT_TRACING_OBJ_FUNC_BLOCKING(obj_type, func, obj, ...) do { } while (false)
#define SYS_PORT_TRACING_OBJ_FUNC_EXIT(obj_type, func, obj, ...) do { } while (false)

#define SYS_PORT_TRACING_TRACKING_FIELD(type)

#define K_OBJ_CORE_INIT(objp, type)   do { } while (false)
#define K_OBJ_CORE_LINK(objp)         do { } while (false)

#define K_PRIO_COOP(x) (((x) >= 0) ? (-x) : (x))

#define _K_PRIO_PREEMPT(x) (((x) < 0) ? 0 : (x))
#define K_PRIO_PREEMPT(x) (((x) >= CONFIG_NUM_PREEMPT_PRIORITIES) ? CONFIG_NUM_PREEMPT_PRIORITIES : _K_PRIO_PREEMPT(x))

#define k_thread_stack_t uint8_t

#define k_ticks_t uint32_t

#ifndef k_timeout_t
#define k_timeout_t k_ticks_t
#endif

#define NSEC_PER_SEC 1000000000L

struct _timeout
{
    uint32_t count;
};

#include "zephyr/spinlock.h"

#include "heap/heap.h"

#include "poll/poll_events.h"

#include "sem/sem.h"

#include "poll/poll.h"

#include "queue/queue.h"

#include "fifo/fifo.h"

#include "lifo/lifo.h"

#include "mutex/mutex.h"

#include "thread/thread.h"

#include "condvar/condvar.h"

#include "work/work.h"

struct sys_memory_stats {
	size_t  free_bytes;
	size_t  allocated_bytes;
	size_t  max_allocated_bytes;
};

#include "slab/slab.h"

#define USEC_PER_MSEC (1000)
/**
 * @addtogroup porting
 * @{
 */

/**
 * @brief Compare timeouts for equality
 *
 * The k_timeout_t object is an opaque struct that should not be
 * inspected by application code.  This macro exists so that users can
 * test timeout objects for equality with known constants
 * (e.g. K_NO_WAIT and K_FOREVER) when implementing their own APIs in
 * terms of Zephyr timeout constants.
 *
 * @return True if the timeout objects are identical
 */
#define K_TIMEOUT_EQ(a, b) ((a) == (b))

/**
 * @brief Generate null timeout delay.
 *
 * This macro generates a timeout delay that instructs a kernel API
 * not to wait if the requested operation cannot be performed immediately.
 *
 * @return Timeout delay value.
 */
#define K_NO_WAIT ((k_timeout_t)0)

/**
 * @brief Generate timeout delay from milliseconds.
 *
 * This macro generates a timeout delay that instructs a kernel API
 * to wait up to @a ms milliseconds to perform the requested operation.
 *
 * @param ms Duration in milliseconds.
 *
 * @return Timeout delay value.
 */
#define K_USEC(ms)     ((ms)/USEC_PER_MSEC)

/**
 * @brief Generate timeout delay from milliseconds.
 *
 * This macro generates a timeout delay that instructs a kernel API
 * to wait up to @a ms milliseconds to perform the requested operation.
 *
 * @param ms Duration in milliseconds.
 *
 * @return Timeout delay value.
 */
#define K_MSEC(ms)     (ms)

/**
 * @brief Generate timeout delay from seconds.
 *
 * This macro generates a timeout delay that instructs a kernel API
 * to wait up to @a s seconds to perform the requested operation.
 *
 * @param s Duration in seconds.
 *
 * @return Timeout delay value.
 */
#define K_SECONDS(s)   K_MSEC((s) * MSEC_PER_SEC)

/**
 * @brief Generate timeout delay from minutes.

 * This macro generates a timeout delay that instructs a kernel API
 * to wait up to @a m minutes to perform the requested operation.
 *
 * @param m Duration in minutes.
 *
 * @return Timeout delay value.
 */
#define K_MINUTES(m)   K_SECONDS((m) * 60)

/**
 * @brief Generate timeout delay from hours.
 *
 * This macro generates a timeout delay that instructs a kernel API
 * to wait up to @a h hours to perform the requested operation.
 *
 * @param h Duration in hours.
 *
 * @return Timeout delay value.
 */
#define K_HOURS(h)     K_MINUTES((h) * 60)

/**
 * @brief Generate infinite timeout delay.
 *
 * This macro generates a timeout delay that instructs a kernel API
 * to wait as long as necessary to perform the requested operation.
 *
 * @return Timeout delay value.
 */
#define K_FOREVER      ((k_ticks_t)-1)

/*
 * When timers are configured out, timepoints can't relate to anything.
 * The best we can do is to preserve whether or not they are derived from
 * K_NO_WAIT. Anything else will translate back to K_FOREVER.
 */
typedef struct { bool wait; } k_timepoint_t;

static inline k_timepoint_t sys_timepoint_calc(k_timeout_t timeout)
{
	k_timepoint_t timepoint;

	timepoint.wait = !K_TIMEOUT_EQ(timeout, K_NO_WAIT);
	return timepoint;
}

static inline k_timeout_t sys_timepoint_timeout(k_timepoint_t timepoint)
{
	return timepoint.wait ? K_FOREVER : K_NO_WAIT;
}

static inline int sys_timepoint_cmp(k_timepoint_t a, k_timepoint_t b)
{
	if (a.wait == b.wait) {
		return 0;
	}
	return b.wait ? -1 : 1;
}

/**
 * @brief Indicates if timepoint is expired
 *
 * @param timepoint Timepoint to evaluate
 * @retval true if the timepoint is in the past, false otherwise
 */
static inline bool sys_timepoint_expired(k_timepoint_t timepoint)
{
	return K_TIMEOUT_EQ(sys_timepoint_timeout(timepoint), K_NO_WAIT);
}

/**
 * @}
 */


static inline uint32_t k_ticks_to_ms_floor32(uint32_t t)
{
	return TICKS_TO_MSEC(t);
}

static inline uint64_t k_ticks_to_us_near64(uint64_t t)
{
	return TICKS_TO_USEC_64(t);
}

static inline uint32_t k_cycle_get_32(void)
{
	return k_uptime_get_32();
}

static inline bool bt_timeout_expired_since(int64_t start, k_timeout_t timeout, int64_t *delta)
{
	int64_t now;
	int64_t deadline;

	if (timeout == K_FOREVER) {
		return false;
	}

	now = k_uptime_get();
	deadline = start + timeout;

	if (delta != NULL) {
		*delta = now - start;
	}

	return now >= deadline;
}

#ifndef CYCLE_TO_NSEC_64
#define CYCLE_TO_NSEC_64(tick) ((uint64_t)((uint64_t)(tick)*1000000000uL / (uint64_t)configTICK_RATE_HZ))
#endif /* CYCLE_TO_NSEC_64 */

static inline uint64_t k_cyc_to_ns_floor64(uint64_t t)
{
	return CYCLE_TO_NSEC_64(t);
}

static ALWAYS_INLINE unsigned int irq_lock()
{
	unsigned int reg;

	reg = DisableGlobalIRQ();

	return reg;
}

static ALWAYS_INLINE void irq_unlock(unsigned int key)
{
	__ISB();
	__DSB();
	EnableGlobalIRQ(key);
}

#endif /* __EDGEFAST_BT_BLE_SYSTEM_H__ */
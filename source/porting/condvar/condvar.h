/*
 * Copyright (c) 2016, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#ifndef __EDGEFAST_BT_BLE_CONDVAR_H__
#define __EDGEFAST_BT_BLE_CONDVAR_H__

#include <system_c.h>

#define K_CONDVAR_MAX_WAITERS 16

struct k_condvar_waiter {
    sys_snode_t node;
    struct k_thread *thread;
};

struct k_condvar {
	sys_slist_t waiter;
	sys_slist_t waiter_pool;
	struct k_mutex mutex;
	struct k_condvar_waiter waiters[K_CONDVAR_MAX_WAITERS];

#ifdef CONFIG_OBJ_CORE_CONDVAR
	struct k_obj_core  obj_core;
#endif
};

#define Z_CONDVAR_INITIALIZER(obj)                                             \
	{                                                                      \
		.waiter = {NULL},                                              \
		.waiter_pool = {NULL},                                         \
		.mutex = Z_MUTEX_INITIALIZER(&obj.mutex),                      \
	}

/**
 * @defgroup condvar_apis Condition Variables APIs
 * @ingroup kernel_apis
 * @{
 */

/**
 * @brief Initialize a condition variable
 *
 * @param condvar pointer to a @p k_condvar structure
 * @retval 0 Condition variable created successfully
 */
int k_condvar_init(struct k_condvar *condvar);

/**
 * @brief Signals one thread that is pending on the condition variable
 *
 * @param condvar pointer to a @p k_condvar structure
 * @retval 0 On success
 */
int k_condvar_signal(struct k_condvar *condvar);

/**
 * @brief Unblock all threads that are pending on the condition
 * variable
 *
 * @param condvar pointer to a @p k_condvar structure
 * @return An integer with number of woken threads on success
 */
int k_condvar_broadcast(struct k_condvar *condvar);

/**
 * @brief Waits on the condition variable releasing the mutex lock
 *
 * Atomically releases the currently owned mutex, blocks the current thread
 * waiting on the condition variable specified by @a condvar,
 * and finally acquires the mutex again.
 *
 * The waiting thread unblocks only after another thread calls
 * k_condvar_signal, or k_condvar_broadcast with the same condition variable.
 *
 * @param condvar pointer to a @p k_condvar structure
 * @param mutex Address of the mutex.
 * @param timeout Waiting period for the condition variable
 *                or one of the special values K_NO_WAIT and K_FOREVER.
 * @retval 0 On success
 * @retval -EAGAIN Waiting period timed out.
 */
int k_condvar_wait(struct k_condvar *condvar, struct k_mutex *mutex, k_timeout_t timeout);

/**
 * @brief Statically define and initialize a condition variable.
 *
 * The condition variable can be accessed outside the module where it is
 * defined using:
 *
 * @code extern struct k_condvar <name>; @endcode
 *
 * @param name Name of the condition variable.
 */
#define K_CONDVAR_DEFINE(name)                                                 \
	struct k_condvar name =                             \
		Z_CONDVAR_INITIALIZER(name)
/**
 * @}
 */

#endif /* __EDGEFAST_BT_BLE_CONDVAR_H__ */
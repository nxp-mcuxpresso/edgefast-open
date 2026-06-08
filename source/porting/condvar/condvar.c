/*
 * Copyright 2025 NXP
 * Copyright (c) 2016, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <porting.h>

/**
 * @brief Initialize a condition variable
 *
 * @param condvar pointer to a @p k_condvar structure
 * @retval 0 Condition variable created successfully
 */
int k_condvar_init(struct k_condvar *condvar)
{
	int err;

	__ASSERT(condvar != NULL, "Invalid parameter!");

	if (condvar == NULL) {
		SYS_PORT_TRACING_OBJ_FUNC(k_condvar, init, condvar, -EINVAL);
		return -EINVAL;
	}

	SYS_PORT_TRACING_OBJ_FUNC(k_condvar, init, condvar, 0);

	err = k_mutex_init(&condvar->mutex);
	if (err) {
		SYS_PORT_TRACING_OBJ_FUNC(k_condvar, init, condvar, err);
		return err;
	}

	err = k_mutex_lock(&condvar->mutex, K_FOREVER);
	if (err) {
		SYS_PORT_TRACING_OBJ_FUNC(k_condvar, init, condvar, err);
		return err;
	}

	sys_slist_init(&condvar->waiter);
	sys_slist_init(&condvar->waiter_pool);
	ARRAY_FOR_EACH(condvar->waiters, i) {
		condvar->waiters[i].thread = NULL;
		sys_slist_append(&condvar->waiter_pool, &condvar->waiters[i].node);
	}

	k_mutex_unlock(&condvar->mutex);

	return 0;
}

/**
 * @brief Signals one thread that is pending on the condition variable
 *
 * @param condvar pointer to a @p k_condvar structure
 * @retval 0 On success
 */
int k_condvar_signal(struct k_condvar *condvar)
{
	int err;
	sys_snode_t *node;

	err = k_mutex_lock(&condvar->mutex, K_FOREVER);
	if (err) {
		return err;
	}

	node = sys_slist_peek_head(&condvar->waiter);
	if (node != NULL) {
		struct k_condvar_waiter *waiter;

		waiter = CONTAINER_OF(node, struct k_condvar_waiter, node);

		if ((waiter->thread != NULL) && (waiter->thread->handle != NULL)) {
			__maybe_unused BaseType_t ret;

			err = -EAGAIN;
			ret = xTaskNotify(waiter->thread->handle, 0, eNoAction);
			__ASSERT(ret == pdPASS, "Failed to wakeup thread %p", waiter->thread);
			if (ret == pdPASS) {
				err = 0;
			}
		}
	}

	if ((err == 0) && (node != NULL)) {
		sys_slist_remove(&condvar->waiter, NULL, node);
		sys_slist_append(&condvar->waiter_pool, node);
	}

	k_mutex_unlock(&condvar->mutex);
	return err;
}

/**
 * @brief Unblock all threads that are pending on the condition
 * variable
 *
 * @param condvar pointer to a @p k_condvar structure
 * @return An integer with number of woken threads on success
 */
int k_condvar_broadcast(struct k_condvar *condvar)
{
	int err;

	err = k_mutex_lock(&condvar->mutex, K_FOREVER);
	if (err) {
		return err;
	}

	while (sys_slist_is_empty(&condvar->waiter) == false) {
		err = k_condvar_signal(condvar);
		if (err != 0) {
			break;
		}
	}

	k_mutex_unlock(&condvar->mutex);
	return err;
}

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
int k_condvar_wait(struct k_condvar *condvar, struct k_mutex *mutex, k_timeout_t timeout)
{
	int err;
	sys_snode_t *node;
	struct k_condvar_waiter *waiter;
	__maybe_unused uint32_t notified_value;
	BaseType_t ret;
	struct k_thread *thread;
	int64_t start = k_uptime_get();

	thread = k_current_get();
	if (thread == NULL) {
		return -EINVAL;
	}

	err = k_mutex_lock(&condvar->mutex, K_FOREVER);
	if (err) {
		return err;
	}

	node = sys_slist_get(&condvar->waiter_pool);
	if (node == NULL) {
		k_mutex_unlock(&condvar->mutex);
		return -EAGAIN;
	}

	waiter = CONTAINER_OF(node, struct k_condvar_waiter, node);
	waiter->thread = thread;
	sys_slist_append(&condvar->waiter, &waiter->node);
	k_mutex_unlock(&condvar->mutex);

	k_mutex_unlock(mutex);
	do {
		atomic_clear_bit(&thread->flags, THREAD_FLAG_SCH_LOCK_CHANGED);

		ret = xTaskNotifyWait(0, ULONG_MAX, &notified_value, timeout);
		if (ret == pdTRUE) {
			break;
		}

		if (bt_timeout_expired_since(start, timeout, NULL)) {
			break;
		}

		if (atomic_test_bit(&thread->flags, THREAD_FLAG_SCH_LOCK_CHANGED)) {
			continue;
		}

		break;
	} while (true);
	err = k_mutex_lock(mutex, K_FOREVER);
	if (err) {
		return err;
	}

	return (ret == pdTRUE) ? 0 : -EAGAIN;
}

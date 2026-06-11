/*
 * Copyright 2025 NXP
 * Copyright (c) 2016, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <porting.h>

/**
 * @brief Initialize a mutex.
 *
 * This routine initializes a mutex object, prior to its first use.
 *
 * Upon completion, the mutex is available and does not have an owner.
 *
 * @param mutex Address of the mutex.
 *
 * @retval 0 Mutex object created
 *
 */
int k_mutex_init(struct k_mutex *mutex)
{
	__ASSERT(mutex != NULL, "Invalid parameter!");

	if (mutex == NULL) {
		SYS_PORT_TRACING_OBJ_FUNC(k_mutex, init, mutex, -EINVAL);
		return -EINVAL;
	}

	SYS_PORT_TRACING_OBJ_FUNC(k_mutex, init, mutex, 0);
	mutex->mutex = xSemaphoreCreateRecursiveMutexStatic(&mutex->mutex_buffer);
	if (mutex->mutex != (QueueHandle_t)&mutex->mutex_buffer) {
		return -EINVAL;
	}

	return 0;
}

/**
 * @brief Lock a mutex.
 *
 * This routine locks @a mutex. If the mutex is locked by another thread,
 * the calling thread waits until the mutex becomes available or until
 * a timeout occurs.
 *
 * A thread is permitted to lock a mutex it has already locked. The operation
 * completes immediately and the lock count is increased by 1.
 *
 * Mutexes may not be locked in ISRs.
 *
 * @param mutex Address of the mutex.
 * @param timeout Waiting period to lock the mutex,
 *                or one of the special values K_NO_WAIT and
 *                K_FOREVER.
 *
 * @retval 0 Mutex locked.
 * @retval -EBUSY Returned without waiting.
 * @retval -EAGAIN Waiting period timed out.
 */
int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout)
{
	struct k_thread *thread;
	int64_t start = k_uptime_ticks();
	int64_t delta;
	int64_t remaining_ticks = timeout.ticks;
	int err = -EINVAL;

	if (NULL == mutex) {
		return err;
	}

	if (mutex->mutex != (QueueHandle_t)&mutex->mutex_buffer) {
		err = k_mutex_init(mutex);
		if (err < 0) {
			return err;
		}
	}

	if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
		return 0;
	}

	thread = k_sched_current_thread_query();
	do {
		if (thread != NULL) {
			atomic_clear_bit(&thread->flags, THREAD_FLAG_SCH_LOCK_CHANGED);
		}

		if (((BaseType_t)pdTRUE) == (BaseType_t)xSemaphoreTakeRecursive(mutex->mutex, (TickType_t)remaining_ticks)) {
			return 0;
		}

		if (bt_timeout_expired_since(start, timeout, &delta)) {
			break;
		}

		if (!K_TIMEOUT_EQ(timeout, K_FOREVER)) {
			remaining_ticks = timeout.ticks - delta;
		}

		if ((thread != NULL) && atomic_test_bit(&thread->flags, THREAD_FLAG_SCH_LOCK_CHANGED)) {
			continue;
		}

		break;
	} while (true);

	return -EAGAIN;
}

/**
 * @brief Unlock a mutex.
 *
 * This routine unlocks @a mutex. The mutex must already be locked by the
 * calling thread.
 *
 * The mutex cannot be claimed by another thread until it has been unlocked by
 * the calling thread as many times as it was previously locked by that
 * thread.
 *
 * Mutexes may not be unlocked in ISRs, as mutexes must only be manipulated
 * in thread context due to ownership and priority inheritance semantics.
 *
 * @param mutex Address of the mutex.
 *
 * @retval 0 Mutex unlocked.
 * @retval -EPERM The current thread does not own the mutex
 * @retval -EINVAL The mutex is not locked
 *
 */
int k_mutex_unlock(struct k_mutex *mutex)
{
	int err = -EINVAL;

	if (NULL == mutex) {
		return err;
	}

	if (mutex->mutex != (QueueHandle_t)&mutex->mutex_buffer) {
		err = k_mutex_init(mutex);
		if (err < 0) {
			return err;
		}
	}

	if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
		return 0;
	}

	if (((BaseType_t)pdTRUE) == (BaseType_t)xSemaphoreGiveRecursive(mutex->mutex)) {
		err = 0;
	} else {
		err = -EAGAIN;
	}

	return err;
}

/*
 * Copyright 2025 NXP
 * Copyright (c) 2016, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __EDGEFAST_BT_BLE_MUTEX_H__
#define __EDGEFAST_BT_BLE_MUTEX_H__

#include <system_c.h>

/**
 * Mutex Structure
 * @ingroup mutex_apis
 */
struct k_mutex {
	SemaphoreHandle_t mutex;
	StaticSemaphore_t mutex_buffer;
	/** Current lock count */
	uint32_t lock_count;

	SYS_PORT_TRACING_TRACKING_FIELD(k_mutex)
};

/**
 * @cond INTERNAL_HIDDEN
 */

#define Z_MUTEX_INITIALIZER(obj) \
	{ \
	.mutex = (SemaphoreHandle_t)NULL, \
	.mutex_buffer = {{0}}, \
	}

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
 * @defgroup mutex_apis Mutex APIs
 * @ingroup kernel_apis
 * @{
 */

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
int k_mutex_init(struct k_mutex *mutex);

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
int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout);

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
int k_mutex_unlock(struct k_mutex *mutex);

/**
 * @internal
 */

/**
 * @brief Statically define and initialize a mutex.
 *
 * The mutex can be accessed outside the module where it is defined using:
 *
 * @code extern struct k_mutex <name>; @endcode
 *
 * @param name Name of the mutex.
 */
#define K_MUTEX_DEFINE(name) \
	STRUCT_SECTION_ITERABLE(k_mutex, name) = \
		Z_MUTEX_INITIALIZER(name)

/** @} */

#endif /* __EDGEFAST_BT_BLE_MUTEX_H__ */

/*
 * Copyright (c) 2016, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "porting.h"

/**
 * @brief Initialize a k_heap
 *
 * This constructs a synchronized k_heap object over a memory region
 * specified by the user.  Note that while any alignment and size can
 * be passed as valid parameters, internal alignment restrictions
 * inside the inner sys_heap mean that not all bytes may be usable as
 * allocated memory.
 *
 * @param h Heap struct to initialize
 * @param mem Pointer to memory.
 * @param bytes Size of memory region, in bytes
 */
void k_heap_init(struct k_heap *h, void *mem, size_t bytes)
{
}

/**
 * @brief Allocate aligned memory from a k_heap
 *
 * Behaves in all ways like k_heap_alloc(), except that the returned
 * memory (if available) will have a starting address in memory which
 * is a multiple of the specified power-of-two alignment value in
 * bytes.  The resulting memory can be returned to the heap using
 * k_heap_free().
 *
 * @note @a timeout must be set to K_NO_WAIT if called from ISR.
 * @note When CONFIG_MULTITHREADING=n any @a timeout is treated as K_NO_WAIT.
 *
 * @funcprops \isr_ok
 *
 * @param h Heap from which to allocate
 * @param align Alignment in bytes, must be a power of two
 * @param bytes Number of bytes requested
 * @param timeout How long to wait, or K_NO_WAIT
 * @return Pointer to memory the caller can now use
 */
void *k_heap_aligned_alloc(struct k_heap *h, size_t align, size_t bytes, k_timeout_t timeout)
{
	return NULL;
}

/**
 * @brief Allocate memory from a k_heap
 *
 * Allocates and returns a memory buffer from the memory region owned
 * by the heap.  If no memory is available immediately, the call will
 * block for the specified timeout (constructed via the standard
 * timeout API, or K_NO_WAIT or K_FOREVER) waiting for memory to be
 * freed.  If the allocation cannot be performed by the expiration of
 * the timeout, NULL will be returned.
 * Allocated memory is aligned on a multiple of pointer sizes.
 *
 * @note @a timeout must be set to K_NO_WAIT if called from ISR.
 * @note When CONFIG_MULTITHREADING=n any @a timeout is treated as K_NO_WAIT.
 *
 * @funcprops \isr_ok
 *
 * @param h Heap from which to allocate
 * @param bytes Desired size of block to allocate
 * @param timeout How long to wait, or K_NO_WAIT
 * @return A pointer to valid heap memory, or NULL
 */
void *k_heap_alloc(struct k_heap *h, size_t bytes, k_timeout_t timeout)
{
	return NULL;
}

/**
 * @brief Allocate and initialize memory for an array of objects from a k_heap
 *
 * Allocates memory for an array of num objects of size and initializes all
 * bytes in the allocated storage to zero.  If no memory is available
 * immediately, the call will block for the specified timeout (constructed
 * via the standard timeout API, or K_NO_WAIT or K_FOREVER) waiting for memory
 * to be freed.  If the allocation cannot be performed by the expiration of
 * the timeout, NULL will be returned.
 * Allocated memory is aligned on a multiple of pointer sizes.
 *
 * @note @a timeout must be set to K_NO_WAIT if called from ISR.
 * @note When CONFIG_MULTITHREADING=n any @a timeout is treated as K_NO_WAIT.
 *
 * @funcprops \isr_ok
 *
 * @param h Heap from which to allocate
 * @param num Number of objects to allocate
 * @param size Desired size of each object to allocate
 * @param timeout How long to wait, or K_NO_WAIT
 * @return A pointer to valid heap memory, or NULL
 */
void *k_heap_calloc(struct k_heap *h, size_t num, size_t size, k_timeout_t timeout)
{
	return NULL;
}

/**
 * @brief Reallocate memory from a k_heap
 *
 * Reallocates and returns a memory buffer from the memory region owned
 * by the heap.  If no memory is available immediately, the call will
 * block for the specified timeout (constructed via the standard
 * timeout API, or K_NO_WAIT or K_FOREVER) waiting for memory to be
 * freed.  If the allocation cannot be performed by the expiration of
 * the timeout, NULL will be returned.
 * Reallocated memory is aligned on a multiple of pointer sizes.
 *
 * @note @a timeout must be set to K_NO_WAIT if called from ISR.
 * @note When CONFIG_MULTITHREADING=n any @a timeout is treated as K_NO_WAIT.
 *
 * @funcprops \isr_ok
 *
 * @param h Heap from which to allocate
 * @param ptr Original pointer returned from a previous allocation
 * @param bytes Desired size of block to allocate
 * @param timeout How long to wait, or K_NO_WAIT
 *
 * @return Pointer to memory the caller can now use, or NULL
 */
void *k_heap_realloc(struct k_heap *h, void *ptr, size_t bytes, k_timeout_t timeout)
{
	return NULL;
}

/**
 * @brief Free memory allocated by k_heap_alloc()
 *
 * Returns the specified memory block, which must have been returned
 * from k_heap_alloc(), to the heap for use by other callers.  Passing
 * a NULL block is legal, and has no effect.
 *
 * @param h Heap to which to return the memory
 * @param mem A valid memory block, or NULL
 */
void k_heap_free(struct k_heap *h, void *mem)
{
}

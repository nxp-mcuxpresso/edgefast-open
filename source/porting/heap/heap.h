/*
 * Copyright (c) 2016, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __EDGEFAST_BT_BLE_HEAP_H__
#define __EDGEFAST_BT_BLE_HEAP_H__

/**
 * @addtogroup heap_apis
 * @{
 */

/* kernel synchronized heap struct */
struct k_heap {
	struct k_spinlock lock;
};

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
void k_heap_init(struct k_heap *h, void *mem,
		 size_t bytes) __attribute_nonnull(1);

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
void *k_heap_aligned_alloc(struct k_heap *h, size_t align, size_t bytes,
			   k_timeout_t timeout) __attribute_nonnull(1);

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
void *k_heap_alloc(struct k_heap *h, size_t bytes,
		   k_timeout_t timeout) __attribute_nonnull(1);

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
	__attribute_nonnull(1);

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
	__attribute_nonnull(1);

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
void k_heap_free(struct k_heap *h, void *mem) __attribute_nonnull(1);

/* Hand-calculated minimum heap sizes needed to return a successful
 * 1-byte allocation.  See details in lib/os/heap.[ch]
 */
#define Z_HEAP_MIN_SIZE ((sizeof(void *) > 4) ? 56 : 44)

/**
 * @brief Define a static k_heap in the specified linker section
 *
 * This macro defines and initializes a static memory region and
 * k_heap of the requested size in the specified linker section.
 * After kernel start, &name can be used as if k_heap_init() had
 * been called.
 *
 * Note that this macro enforces a minimum size on the memory region
 * to accommodate metadata requirements.  Very small heaps will be
 * padded to fit.
 *
 * @param name Symbol name for the struct k_heap object
 * @param bytes Size of memory region, in bytes
 * @param in_section __attribute__((section(name))
 */
#define Z_HEAP_DEFINE_IN_SECT(name, bytes, in_section)		\
	STRUCT_SECTION_ITERABLE(k_heap, name) = {		\
	}

/**
 * @brief Define a static k_heap
 *
 * This macro defines and initializes a static memory region and
 * k_heap of the requested size.  After kernel start, &name can be
 * used as if k_heap_init() had been called.
 *
 * Note that this macro enforces a minimum size on the memory region
 * to accommodate metadata requirements.  Very small heaps will be
 * padded to fit.
 *
 * @param name Symbol name for the struct k_heap object
 * @param bytes Size of memory region, in bytes
 */
#define K_HEAP_DEFINE(name, bytes)				\
	Z_HEAP_DEFINE_IN_SECT(name, bytes,			\
			      __noinit_named(kheap_buf_##name))

/**
 * @brief Define a static k_heap in uncached memory
 *
 * This macro defines and initializes a static memory region and
 * k_heap of the requested size in uncached memory.  After kernel
 * start, &name can be used as if k_heap_init() had been called.
 *
 * Note that this macro enforces a minimum size on the memory region
 * to accommodate metadata requirements.  Very small heaps will be
 * padded to fit.
 *
 * @param name Symbol name for the struct k_heap object
 * @param bytes Size of memory region, in bytes
 */
#define K_HEAP_DEFINE_NOCACHE(name, bytes)			\
	Z_HEAP_DEFINE_IN_SECT(name, bytes, __nocache)

/**
 * @}
 */

#endif /* __EDGEFAST_BT_BLE_HEAP_H__ */
# EdgeFast Open Bluetooth stack integration

This section provides guidance for integrating EdgeFast Open Bluetooth stack examples in an RTOS environment, with a focus on keeping the OS abstraction consistent.

## Zephyr API vs FreeRTOS API usage in EdgeFast Open examples

When integrating with FreeRTOS, example code should **prefer FreeRTOS APIs** whenever an equivalent capability exists. This keeps the OS abstraction consistent and avoids mixing different kernel/IPC models.

### Zephyr APIs that can be used directly (no direct FreeRTOS equivalent)

Some subsystems used by current EdgeFast Open examples rely on Zephyr-style utility APIs that do not have a clear 1:1 FreeRTOS replacement in this context. In these cases, it is acceptable to **use the Zephyr interfaces directly**.

#### Allowed Zephyr API families

- **FIFO**: `k_fifo_*`
- **Work item** (work items only; this does *not* include work queue creation): `k_work_*`
- **Network buffer**: `net_buf_*`
- **Memory slab allocation**: `k_mem_*`

#### Allowed Zephyr APIs (list)

The following list enumerates the concrete Zephyr-style APIs that are expected to be used by EdgeFast Open examples when integrating with FreeRTOS. This section is intended as a quick review aid.

> Notes:
> - The list below focuses on the utility interfaces used by the stack/examples and is not meant to be a complete Zephyr RTOS surface.
> - APIs that create or manage Zephyr kernel threads/queues (for example `k_thread_*`, `k_work_queue_*`) are intentionally **not** listed here.

**FIFO (`k_fifo_*`)**

- `k_fifo_init()`
- `k_fifo_cancel_wait()`
- `k_fifo_put()`
- `k_fifo_alloc_put()`
- `k_fifo_put_list()`
- `k_fifo_put_slist()`
- `k_fifo_get()`
- `k_fifo_is_empty()`
- `k_fifo_peek_head()`
- `k_fifo_peek_tail()`

**Work item (`k_work_*`)**

- `k_work_init()`
- `k_work_busy_get()`
- `k_work_is_pending()`
- `k_work_submit_to_queue()`
- `k_work_submit()`
- `k_work_flush()`
- `k_work_cancel()`
- `k_work_cancel_sync()`
- `k_work_init_delayable()`
- `k_work_delayable_from_work()`
- `k_work_delayable_busy_get()`
- `k_work_delayable_is_pending()`
- `k_work_delayable_expires_get()`
- `k_work_delayable_remaining_get()`
- `k_work_schedule_for_queue()`
- `k_work_schedule()`
- `k_work_reschedule_for_queue()`
- `k_work_reschedule()`
- `k_work_flush_delayable()`
- `k_work_cancel_delayable()`
- `k_work_cancel_delayable_sync()`

**Network buffer (`net_buf_*`)**

- `net_buf_ref()`
- `net_buf_unref()`
- `net_buf_reserve()`
- `net_buf_pull()`
- `net_buf_push()`
- `net_buf_pull_le16()`
- `net_buf_pull_le32()`
- `net_buf_push_le16()`
- `net_buf_push_le32()`
- `net_buf_add()`
- `net_buf_add_le16()`
- `net_buf_add_le32()`
- `net_buf_add_mem()`
- `net_buf_insert()`
- `net_buf_remove_mem()`
- `net_buf_tailroom()`
- `net_buf_headroom()`
- `net_buf_frags_len()`
- `net_buf_frag_insert()`
- `net_buf_frag_add()`
- `net_buf_frag_del()`
- `net_buf_frag_last()`
- `net_buf_simple_init()`
- `net_buf_simple_reset()`
- `net_buf_simple_add()`
- `net_buf_simple_add_mem()`
- `net_buf_simple_add_u8()`
- `net_buf_simple_add_le16()`
- `net_buf_simple_add_le32()`
- `net_buf_simple_add_be16()`
- `net_buf_simple_add_be32()`
- `net_buf_simple_push()`
- `net_buf_simple_push_le16()`
- `net_buf_simple_push_le32()`
- `net_buf_simple_push_be16()`
- `net_buf_simple_push_be32()`
- `net_buf_simple_pull()`
- `net_buf_simple_pull_u8()`
- `net_buf_simple_pull_le16()`
- `net_buf_simple_pull_le32()`
- `net_buf_simple_pull_be16()`
- `net_buf_simple_pull_be32()`
- `net_buf_simple_tailroom()`
- `net_buf_simple_headroom()`
- `net_buf_simple_restore()`
- `net_buf_simple_save()`

**Memory slab allocation (`k_mem_slab_*`)**

- `k_mem_slab_init()`
- `k_mem_slab_alloc()`
- `k_mem_slab_free()`
- `k_mem_slab_num_used_get()`
- `k_mem_slab_max_used_get()`
- `k_mem_slab_num_free_get()`
- `k_mem_slab_runtime_stats_get()`
- `k_mem_slab_runtime_stats_reset_max()`

### All other cases: use FreeRTOS APIs

For thread creation, synchronization primitives (mutex/semaphore/queue/event-group), timers, and other OS services that have FreeRTOS counterparts, **use the FreeRTOS interfaces** and avoid calling Zephyr kernel APIs directly.

## Scheduler lock behavior (`k_sched_lock()` / `k_sched_unlock()`)

Some EdgeFast Open components rely on a Zephyr-style scheduler lock via `k_sched_lock()` / `k_sched_unlock()`.

### Baseline semantics (Zephyr)

In Zephyr, `k_sched_lock()` prevents the *current thread* from being preempted (it becomes non-preemptible), but it does **not** "shut down" the whole kernel scheduler. As a result:

- the calling thread continues to run,
- the thread is not preempted by other ready threads,
- kernel services (for example, synchronization primitives) can still be used as usual.

### FreeRTOS legacy behavior (scheduler suspend/resume)

FreeRTOS does not provide a direct equivalent of Zephyr's scheduler lock API. A common pattern to approximate "no context switch" is suspending/resuming the scheduler using `vTaskSuspendAll()` / `xTaskResumeAll()`.

However, this is *stronger* than Zephyr's "non-preemptible current thread" semantics, because a suspended scheduler changes what the application is allowed to do:

- while the scheduler is suspended, some blocking/IPC operations are not permitted,
- calling certain synchronization APIs (for example, `xSemaphoreTake()` in some configurations/ports) may trigger an assertion if they could block or otherwise require a context switch.

This means code that is safe under Zephyr's `k_sched_lock()` can be unsafe under a FreeRTOS implementation that uses scheduler suspend/resume.

### FreeRTOS selectable implementation (priority-based alternative)

To avoid the above mismatch, the FreeRTOS port supports two implementation strategies for `k_sched_lock()` / `k_sched_unlock()`, selected at build time:

- `BT_THREAD_SCHEDULER_LOCK_VIA_SCHEDULER`
  - Implements `k_sched_lock()` using `vTaskSuspendAll()`.
  - Implements `k_sched_unlock()` using `xTaskResumeAll()`.
  - **Limitation:** while locked, the application must not call APIs that may block or rely on scheduler activity (for example, taking a mutex/semaphore with a non-zero timeout).

- `BT_THREAD_SCHEDULER_LOCK_VIA_HIGHEST_PRIO`
  - Implements `k_sched_lock()` by temporarily raising the current task priority to a reserved “second-highest” priority level.
  - Implements `k_sched_unlock()` by restoring the original priority.
  - This matches Zephyr’s intent more closely (the current task becomes effectively non-preemptible without globally suspending the scheduler), and it avoids the “scheduler suspended” restrictions for synchronization APIs.
  - **Requirement:** one priority level must be reserved (the highest priority remains available for truly critical/system use, and the second-highest is used for the lock).

  Example (priority reservation):

  - If `CONFIG_NUM_PREEMPT_PRIORITIES = 12`, the FreeRTOS configuration should typically set `configMAX_PRIORITIES = 14`.
  - FreeRTOS highest priority (`configMAX_PRIORITIES - 1` = 13) is commonly reserved for the system timer/daemon task.
  - The second-highest priority (12) should be reserved for `k_sched_lock()` / `k_sched_unlock()` when using `BT_THREAD_SCHEDULER_LOCK_VIA_HIGHEST_PRIO`.
  - Therefore, the maximum priority that application tasks should use is 11.

### Application guidance

- Prefer `BT_THREAD_SCHEDULER_LOCK_VIA_HIGHEST_PRIO` when the locked region may call FreeRTOS synchronization primitives.
- Use `BT_THREAD_SCHEDULER_LOCK_VIA_SCHEDULER` only when the locked region is strictly bounded and does not call APIs that may block or require scheduling.
- Keep scheduler-locked regions short and deterministic regardless of the implementation.

## Integration checklist

Use the following checklist when reviewing or writing an example:

- Use FreeRTOS tasks for thread creation and scheduling.
- Use FreeRTOS synchronization primitives for inter-task coordination.
- Use FreeRTOS timers for time-based scheduling.
- Avoid mixing Zephyr kernel primitives (for example, `k_thread_*`, `k_mutex_*`, `k_sem_*`, `k_timer_*`) with FreeRTOS primitives in the same module.
- Restrict direct Zephyr API usage to the four categories listed above, unless there is a documented reason.
- When integrating on FreeRTOS, be aware that `k_sched_lock()` semantics depend on the selected implementation strategy.

## Rationale

Mixing multiple OS abstractions in the same integration layer typically increases maintenance cost and makes issues harder to debug (for example, different scheduling assumptions, different priority semantics, and different IPC lifetimes).

Standardizing on FreeRTOS APIs where possible:

- improves readability for FreeRTOS-based users,
- reduces the risk of subtle concurrency bugs,
- simplifies portability and long-term maintenance.

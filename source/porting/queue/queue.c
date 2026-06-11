
/*
 * Copyright 2023 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "porting.h"

#define LOG_ENABLE IS_ENABLED(CONFIG_BT_DEBUG_FIFO)
#define LOG_MODULE_NAME bt_fifo
#include "fsl_component_log.h"
LOG_MODULE_DEFINE(LOG_MODULE_NAME, kLOG_LevelTrace);

static struct k_spinlock lock;

void bt_fifo_init(bt_fifo_t *fifo)
{
	int err;

	LOG_DBG("FIFO init", fifo);

	k_spinlock_key_t key = k_spin_lock(&lock);

	err = k_sem_init(&fifo->sem, 0, UINT32_MAX);
	if (err < 0) {
		LOG_ERR("failed");
	}

	sys_slist_init(&fifo->list);

	k_spin_unlock(&lock, key);
}

#if (defined(CONFIG_BT_DEBUG_FIFO) && (CONFIG_BT_DEBUG_FIFO > 0U))
void bt_fifo_append_debug(bt_fifo_t *fifo, void *data, const char *func, int line)
#else
void bt_fifo_append(bt_fifo_t *fifo, void *data)
#endif /* CONFIG_BT_DEBUG_FIFO */
{
	k_spinlock_key_t key;

	key = k_spin_lock(&lock);
	if (true == sys_slist_find(&fifo->list, (sys_snode_t*)data, NULL))
	{
#if (defined(CONFIG_BT_DEBUG_FIFO) && (CONFIG_BT_DEBUG_FIFO > 0U))
		LOG_ERR("%s():%d:node %p has been inserted to fifo %p", func, line, data, fifo);
#else
		LOG_ERR("node %p has been inserted to fifo %p", data, fifo);
#endif /* CONFIG_BT_DEBUG_FIFO */
		k_spin_unlock(&lock, key);
		return;
	}
	LOG_INF("node %p, fifo %p", data, fifo);
	sys_slist_append(&fifo->list, (sys_snode_t*)data);
	k_spin_unlock(&lock, key);
	LOG_INF("next %p", ((sys_snode_t*)data)->next);
	k_sem_give(&fifo->sem);
}

#if (defined(CONFIG_BT_DEBUG_FIFO) && (CONFIG_BT_DEBUG_FIFO > 0U))
void bt_fifo_prepend_debug(bt_fifo_t *fifo, void *data, const char *func, int line)
#else
void bt_fifo_prepend(bt_fifo_t *fifo, void *data)
#endif /* CONFIG_BT_DEBUG_FIFO */
{
	k_spinlock_key_t key;

	key = k_spin_lock(&lock);
	if (true == sys_slist_find(&fifo->list, (sys_snode_t*)data, NULL))
	{
#if (defined(CONFIG_BT_DEBUG_FIFO) && (CONFIG_BT_DEBUG_FIFO > 0U))
		LOG_ERR("%s():%d:node %p has been inserted to fifo %p", func, line, data, fifo);
#else
		LOG_ERR("node %p has been inserted to fifo %p", data, fifo);
#endif /* CONFIG_BT_DEBUG_FIFO */
		k_spin_unlock(&lock, key);
		return;
	}
	sys_slist_prepend(&fifo->list, (sys_snode_t*)data);
	k_spin_unlock(&lock, key);
	k_sem_give(&fifo->sem);
}

#if (defined(CONFIG_BT_DEBUG_FIFO) && (CONFIG_BT_DEBUG_FIFO > 0U))
void bt_fifo_insert_debug(bt_fifo_t *fifo, void *prev, void *data, const char *func, int line)
#else
void bt_fifo_insert(bt_fifo_t *fifo, void *prev, void *data)
#endif /* CONFIG_BT_DEBUG_FIFO */
{
	k_spinlock_key_t key;

	key = k_spin_lock(&lock);
	if (true == sys_slist_find(&fifo->list, (sys_snode_t*)data, NULL))
	{
#if (defined(CONFIG_BT_DEBUG_FIFO) && (CONFIG_BT_DEBUG_FIFO > 0U))
		LOG_ERR("%s():%d:node %p has been inserted to fifo %p", func, line, data, fifo);
#else
		LOG_ERR("node %p has been inserted to fifo %p", data, fifo);
#endif /* CONFIG_BT_DEBUG_FIFO */
		k_spin_unlock(&lock, key);
		return;
	}
	sys_slist_insert(&fifo->list, (sys_snode_t*)prev, (sys_snode_t*)data);
	k_spin_unlock(&lock, key);
	k_sem_give(&fifo->sem);
}

int bt_fifo_append_list(bt_fifo_t *fifo, void *head, void *tail)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&lock);
	sys_slist_append_list(&fifo->list, (sys_snode_t*)head, (sys_snode_t*)tail);
	k_spin_unlock(&lock, key);

	k_sem_give(&fifo->sem);

	return 0;
}

int bt_fifo_merge_slist(bt_fifo_t *fifo, sys_slist_t *head)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&lock);
	sys_slist_append_list(&fifo->list, (sys_snode_t*)head->head, (sys_snode_t*)NULL);
	head->head = NULL;
	k_spin_unlock(&lock, key);
	k_sem_give(&fifo->sem);

	return 0;
}

void *bt_fifo_get(bt_fifo_t *fifo, k_timeout_t timeout)
{
	sys_snode_t *head = NULL;
	int64_t start;
	int64_t delta;
	k_timeout_t remaining_ticks = timeout;
	int err;

	if (k_is_in_isr()) {
		k_spinlock_key_t key = k_spin_lock(&lock);
		head = sys_slist_get(&fifo->list);
		k_spin_unlock(&lock, key);
	} else {
		start = k_uptime_ticks();
		do {
			k_spinlock_key_t key = k_spin_lock(&lock);
			head = sys_slist_get(&fifo->list);
			k_spin_unlock(&lock, key);
			if (head != NULL) {
				break;
			}

			if (bt_timeout_expired_since(start, timeout, &delta)) {
				break;
			}

			if (!K_TIMEOUT_EQ(timeout, K_FOREVER)) {
				remaining_ticks.ticks = timeout.ticks - delta;
			}

			err = k_sem_take(&fifo->sem, remaining_ticks);
			if (err < 0) {
				LOG_ERR("Failed to wait for queue semaphore: %d", err);
				break;
			}
		} while (head == NULL);
	}

	LOG_INF("node %p, fifo %p", head, fifo);

	if (head != NULL) {
		head->next = NULL;
	}

	return head;
}

bool bt_fifo_remove(bt_fifo_t *fifo, void *data)
{
	bool ret;
	k_spinlock_key_t key;

	key = k_spin_lock(&lock);
	ret = sys_slist_find_and_remove(&fifo->list, (sys_snode_t*)data);
	k_spin_unlock(&lock, key);

	return ret;
}

bool bt_fifo_unique_append(bt_fifo_t *fifo, void *data)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&lock);
	if (true == sys_slist_find(&fifo->list, (sys_snode_t*)data, NULL))
	{
		k_spin_unlock(&lock, key);
		LOG_ERR("node %p has been inserted to fifo %p", data, fifo);
		return false;
	}
	sys_slist_append(&fifo->list, (sys_snode_t*)data);
	k_spin_unlock(&lock, key);
	k_sem_give(&fifo->sem);

	return true;
}

int bt_fifo_is_empty(bt_fifo_t *fifo)
{
	return sys_slist_is_empty(&fifo->list);
}

void *bt_fifo_peek_head(bt_fifo_t *fifo)
{
	return sys_slist_peek_head(&fifo->list);
}

void *bt_fifo_peek_tail(bt_fifo_t *fifo)
{
	return sys_slist_peek_tail(&fifo->list);
}

int bt_fifo_alloc_prepend(bt_fifo_t *fifo, void *data)
{
	return -ENOMEM;
}

int bt_fifo_alloc_append(bt_fifo_t *fifo, void *data)
{
	return -ENOMEM;
}

void bt_fifo_cancel_wait(bt_fifo_t *fifo)
{
}

/* IMPLEMENTATION for k_queue */
static void k_queue_poll_event_update(struct k_queue *queue);
static void k_queue_poll_event_update(struct k_queue *queue)
{
	struct k_poll_event *poll_event, *next;

	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&queue->poll_events, poll_event, next, _node) {
		if ((NULL == poll_event->poller) || (NULL == poll_event->poller->event_update))
		{
			continue;
		}

		poll_event->poller->event_update(poll_event);
	}
}

void k_queue_init(struct k_queue *queue)
{
	bt_fifo_init(&queue->fifo);
}

void k_queue_cancel_wait(struct k_queue *queue)
{
	bt_fifo_cancel_wait(&queue->fifo);
}

void k_queue_append(struct k_queue *queue, void *data)
{
	bt_fifo_append(&queue->fifo, data);

	k_queue_poll_event_update(queue);
}

int32_t k_queue_alloc_append(struct k_queue *queue, void *data)
{
	int ret = bt_fifo_alloc_append(&queue->fifo, data);

	if (0 == ret)
	{
		k_queue_poll_event_update(queue);
	}
	return (int32_t)ret;
}

void k_queue_prepend(struct k_queue *queue, void *data)
{
	bt_fifo_prepend(&queue->fifo, data);
	k_queue_poll_event_update(queue);
}

int32_t k_queue_alloc_prepend(struct k_queue *queue, void *data)
{
	int ret = bt_fifo_alloc_prepend(&queue->fifo, data);

	if (0 == ret)
	{
		k_queue_poll_event_update(queue);
	}
	return (int32_t)ret;
}

void k_queue_insert(struct k_queue *queue, void *prev, void *data)
{
	bt_fifo_insert(&queue->fifo, prev, data);
	k_queue_poll_event_update(queue);
}

int k_queue_append_list(struct k_queue *queue, void *head, void *tail)
{
	int ret;

	ret = bt_fifo_append_list(&queue->fifo, head, tail);
	k_queue_poll_event_update(queue);

	return ret;
}

int k_queue_merge_slist(struct k_queue *queue, sys_slist_t *list)
{
	int ret;

	ret = bt_fifo_merge_slist(&queue->fifo, list);
	k_queue_poll_event_update(queue);

	return ret;
}

void *k_queue_get(struct k_queue *queue, k_timeout_t timeout)
{
	return bt_fifo_get(&queue->fifo, timeout);
}

bool k_queue_remove(struct k_queue *queue, void *data)
{
	return bt_fifo_remove(&queue->fifo, data);
}

bool k_queue_unique_append(struct k_queue *queue, void *data)
{
	bool ret = bt_fifo_unique_append(&queue->fifo, data);

	k_queue_poll_event_update(queue);

	return ret;
}

int k_queue_is_empty(struct k_queue *queue)
{
	return bt_fifo_is_empty(&queue->fifo);
}

void *k_queue_peek_head(struct k_queue *queue)
{
	return bt_fifo_peek_head(&queue->fifo);
}

void *k_queue_peek_tail(struct k_queue *queue)
{
	return bt_fifo_peek_tail(&queue->fifo);
}
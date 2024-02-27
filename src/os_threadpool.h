/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef __OS_THREADPOOL_H__
#define __OS_THREADPOOL_H__	1

#include <pthread.h>
#include "os_list.h"

typedef struct {
	void *argument;
	void (*action)(void *arg);
	void (*destroy_arg)(void *arg);
	os_list_node_t list;
} os_task_t;

typedef struct os_threadpool {
	unsigned int num_threads;
	pthread_t *threads;
	/* should_continue => threadpool-ul ar trebui sa se opreasca din
	 * asteptarea task-urilor sau nu
	 */
	unsigned int num_threadsLocked;
	/*
	 * Head of queue used to store tasks.
	 * First item is head.next, if head.next != head (i.e. if queue
	 * is not empty).
	 * Last item is head.prev, if head.prev != head (i.e. if queue
	 * is not empty).
	 */
	os_list_node_t head;

	/* TODO: Define threapool / queue synchronization data. */

	/* mutex - asigura ca atunci cand se modifica coada de task-uri
	 * nu o sa existe pierderi de date (evita scenariul in care am doua
	 * thread-uri, unul imi scrie la o adresa de memorie si altul la aceeasi,
	 * facand suprascriere, deci pierderea datelor)
	 */
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int should_continue;
} os_threadpool_t;

os_task_t *create_task(void (*f)(void *), void *arg, void (*destroy_arg)(void *));
void destroy_task(os_task_t *t);

os_threadpool_t *create_threadpool(unsigned int num_threads);
void destroy_threadpool(os_threadpool_t *tp);

void enqueue_task(os_threadpool_t *q, os_task_t *t);
os_task_t *dequeue_task(os_threadpool_t *tp);
void wait_for_completion(os_threadpool_t *tp);

#endif

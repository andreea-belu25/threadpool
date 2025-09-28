// SPDX-License-Identifier: BSD-3-Clause

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>

#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"

/* Create a task that would be executed by a thread. */
os_task_t *create_task(void (*action)(void *), void *arg, void (*destroy_arg)(void *))
{
	os_task_t *t;

	t = malloc(sizeof(*t));
	DIE(t == NULL, "malloc");

	t->action = action;		// the function
	t->argument = arg;		// arguments for the function
	t->destroy_arg = destroy_arg;	// destroy argument function

	return t;
}

/* Destroy task. */
void destroy_task(os_task_t *t)
{
	if (t->destroy_arg != NULL)
		t->destroy_arg(t->argument);
	free(t);
}

/* Put a new task to threadpool task queue. */
void enqueue_task(os_threadpool_t *tp, os_task_t *t)
{
	assert(tp != NULL);  // program exits if the threadpool is NULL
	assert(t != NULL);

	/* Enqueue task to the shared task queue. Use synchronization. */

	/* mutex = controls access to a shared memory region
	 * the steps below are repeated until the execution of all
	 * threads is finished (other threads wait for the completion of the
	 * task from the current thread that is working)
	 */
	pthread_mutex_lock(&tp->mutex);  // lock access to shared memory region
	list_add(&tp->head, &t->list);  // current thread adds to the list
	pthread_mutex_unlock(&tp->mutex);

	pthread_cond_broadcast(&tp->cond);
}

/*
 * Check if queue is empty.
 * This function should be called in a synchronized manner.
 */
static int queue_is_empty(os_threadpool_t *tp)
{
	return list_empty(&tp->head);
}

/*
 * Get a task from threadpool task queue.
 * Block if no task is available.
 * Return NULL if work is complete, i.e. no task will become available,
 * i.e. all threads are going to block.
 */
os_task_t *dequeue_task(os_threadpool_t *tp)
{
	// lock in a function => unlock before exiting the function
	os_task_t *t;

	/* Dequeue task from the shared task queue. Use synchronization. */

	pthread_mutex_lock(&tp->mutex);

	// if there are no more tasks to process => exit function
	if (tp->should_continue == 0) {
		pthread_mutex_unlock(&tp->mutex);
		return NULL;
	}
	int was_queue_empty = 0;

	if (queue_is_empty(tp)) { // empty list (contains only sentinel)
		/* if the queue is empty => the thread that gets here is blocked
		 * and will wait until a new task appears. Other threads
		 * may still have tasks to execute in parallel.
		 */
		// blocked thread - thread stops execution and then resumes where it left off
		tp->num_threadsLocked += 1;
		was_queue_empty = 1;

		// empty queue + threadpool that can still continue => wait for a new task
		while (queue_is_empty(tp) && tp->should_continue)
		    // unlocks mutex implicitly
		    // waits until a signal is received, then resumes execution
			pthread_cond_wait(&tp->cond, &tp->mutex);

		// empty queue + threadpool that can no longer continue execution => exit function
		// nothing left to remove from the queue
		if (tp->should_continue == 0)
			return NULL;
	}

	if (was_queue_empty) {
		// lock and unlock - control access to shared memory
		pthread_mutex_lock(&tp->mutex);
		// current thread has been unblocked
		tp->num_threadsLocked -= 1;
	}

	// if the queue is not empty => take the last task and remove it
	// pointer arithmetic
	t = (os_task_t *)((char *)tp->head.prev - 24); // = t->list;
	list_del(tp->head.prev);
	pthread_mutex_unlock(&tp->mutex);
	return t;
}

/* Loop function for threads */
static void *thread_loop_function(void *arg)
{
	os_threadpool_t *tp = (os_threadpool_t *) arg;

	while (1) {
		os_task_t *t;

		t = dequeue_task(tp);
		if (t == NULL)
			break;  // stop when there are no more tasks to take from the task queue
		t->action(t->argument);  // run task
		destroy_task(t);  // free memory
	}

	return NULL;
}

/* Wait completion of all threads. This is to be called by the main thread. */
void wait_for_completion(os_threadpool_t *tp)
{
	/* Wait for all worker threads. Use synchronization. */
	while (!(tp->num_threadsLocked == tp->num_threads && queue_is_empty(tp)))
		continue;

	/*
	 * If all threads are paused and waiting for a task to become available,
	 * that means no thread will be able to create new tasks.
	 * If the task queue is also empty, that means no more tasks can be created,
	 * i.e. the threadpool must stop.
	 */
	tp->should_continue = 0;
	pthread_cond_broadcast(&tp->cond);  // unblock all blocked threads

	// when all threads are blocked and the queue is empty => STOP (no more tasks can be added)
	/* Join all worker threads. */
	for (unsigned int i = 0; i < tp->num_threads; i++)
		pthread_join(tp->threads[i], NULL);
}

/* Create a new threadpool. */
os_threadpool_t *create_threadpool(unsigned int num_threads)
{
	os_threadpool_t *tp = NULL;
	int rc;

	tp = malloc(sizeof(*tp));
	DIE(tp == NULL, "malloc");

	list_init(&tp->head);

	/* Initialize synchronization data. */

	tp->should_continue = 1;
	pthread_mutex_init(&tp->mutex, NULL);
	pthread_cond_init(&tp->cond, NULL);
	
	tp->num_threadsLocked = 0;
	tp->num_threads = num_threads;
	tp->threads = malloc(num_threads * sizeof(*tp->threads));
	DIE(tp->threads == NULL, "malloc");
	for (unsigned int i = 0; i < num_threads; ++i) {
		rc = pthread_create(&tp->threads[i], NULL, &thread_loop_function, (void *) tp);
		DIE(rc < 0, "pthread_create");
	}
	return tp;
}

/* Destroy a threadpool. Assume all threads have been joined. */
void destroy_threadpool(os_threadpool_t *tp)
{
	os_list_node_t *n, *p;

	/* Cleanup synchronization data. */

	pthread_mutex_destroy(&tp->mutex);
	pthread_cond_destroy(&tp->cond);
	list_for_each_safe(n, p, &tp->head) {
		list_del(n);
		destroy_task(list_entry(n, os_task_t, list));
	}

	free(tp->threads);
	free(tp);
}

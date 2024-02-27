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
	assert(tp != NULL);  // iese din program daca threadpool-ul este NULL
	assert(t != NULL);

	/* TODO: Enqueue task to the shared task queue. Use synchronization. */

	/* mutex = controleaza accesul la o zona de memorie comuna
	 * pasii de mai jos se repeta pana s-a terminat
	 * executia tuturor thread-uilor (celalalte thread-uri
	 * asteapta finalizarea task-ului de pe thread-ul curent care lucreaza)
	 */
	pthread_mutex_lock(&tp->mutex);  // blocare acces zona de memorie comuna
	list_add(&tp->head, &t->list);  //  thread-ul curent adauga in lista
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
	//  lock intr-o fct => unlock pana la iesirea din fct
	os_task_t *t;

	/* TODO: Dequeue task from the shared task queue. Use synchronization. */

	pthread_mutex_lock(&tp->mutex);

	//  daca nu mai am de prelucrat task-uri => ies din fct
	if (tp->should_continue == 0) {
		pthread_mutex_unlock(&tp->mutex);
		return NULL;
	}
	int was_queue_empty = 0;

	if (queue_is_empty(tp)) { // lista goala (contine doar santinela)
		/* daca coada e goala => thread-ul care ajunge aici e blocat
		 * si va astepta pana apare un task nou. Celalalte thread-uri
		 * pot avea task-uri pe care sa le execute in paralel.
		 */
		//  thread blocat - thread se opreste din executie si apoi reia de unde s-a oprit
		tp->num_threadsLocked += 1;
		was_queue_empty = 1;

		//  coada goala + threadpool care mai poate continua => se asteapta un task nou
		while (queue_is_empty(tp) && tp->should_continue)
		    //  se da implicit unlock la mutex
		    //  se asteapta pana primeste un semnal si apoi se continua rularea 
			pthread_cond_wait(&tp->cond, &tp->mutex);

		//  coada goala + threadpool care nu mai poate continua executia => se iese din fct
		//  nu mai avem ce elimina din coada
		if (tp->should_continue == 0)
			return NULL;
	}

	if (was_queue_empty) {
		//  lock si unlock- blocare/ deblocare acces la zona de memorie
		pthread_mutex_lock(&tp->mutex);
		//  thread-ul curent a fost deblocat
		tp->num_threadsLocked -= 1;
	}

	//  daca coada nu e goala => iau ultimul task si-l sterg
	//  aritmetica pe pointeri
	t = (os_task_t *)((char *)tp->head.prev - 24); //= t->list;
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
			break;  //  oprire cand nu mai am ce scoate din coada de task-uri
		t->action(t->argument);  //  rulare task
		destroy_task(t);  //  dezalocare memorie
	}

	return NULL;
}

/* Wait completion of all threads. This is to be called by the main thread. */
void wait_for_completion(os_threadpool_t *tp)
{
	/* TODO: Wait for all worker threads. Use synchronization. */
	while (!(tp->num_threadsLocked == tp->num_threads && queue_is_empty(tp)))
		continue;

	/*
	 * Daca toate thread-urile sunt pe pauza si asteapta ca un task sa devina disponibil,
	 * asta inseamna ca nici un thread nu va putea crea task-uri.
	 * Daca si coada de task-uri este goala, asta inseamna ca nu va mai putea fi creat
	 * nici un task, adica threadpool-ul trebuie oprit.
	 */
	tp->should_continue = 0;
	pthread_cond_broadcast(&tp->cond);  // deblocheaza toate thread-urile blocate

	// cand toate thread-urile sunt blocate si coada e goala => STOP (nu mai pot fi adaugate task-uri)
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

	/* TODO: Initialize synchronization data. */

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

	/* TODO: Cleanup synchronization data. */

	pthread_mutex_destroy(&tp->mutex);
	pthread_cond_destroy(&tp->cond);
	list_for_each_safe(n, p, &tp->head) {
		list_del(n);
		destroy_task(list_entry(n, os_task_t, list));
	}

	free(tp->threads);
	free(tp);
}

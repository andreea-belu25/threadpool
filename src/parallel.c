// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>

#include "os_graph.h"
#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"

#define NUM_THREADS		4

static int sum;
static os_graph_t *graph;
static os_threadpool_t *tp;

/* TODO: Define graph synchronization mechanisms. */

pthread_mutex_t graph_mutex;  // mutex-ul asociat grafului
/* (daca incearca cineva sa acceseze graficul simultan, nu va functiona);
 * threadpool, graf si suma -> zone de memorie comune
 */

/* TODO: Define graph task argument. */

void prelucrate_currentNode(void *Node)
{
	//  parcurgerea si prelucrarea nodurilor din graf in paralel
	os_node_t *node = (os_node_t *)Node;

	if (graph->visited[node->id] == DONE)
		return;

	pthread_mutex_lock(&graph_mutex);
	sum += node->info;
	graph->visited[node->id] = DONE;
	pthread_mutex_unlock(&graph_mutex);

	for (unsigned int i = 0; i < node->num_neighbours; i++) {
		pthread_mutex_lock(&graph_mutex);
		if (graph->visited[node->neighbours[i]] == NOT_VISITED) {
			os_task_t *t = create_task(prelucrate_currentNode, graph->nodes[node->neighbours[i]], NULL);

			//  daca am nodurile 2 si 3 care au nodul 4 ca vecin
			//  si ruleaza in paralel thread-ul pentru 2 si thread-ul pentru 3 => daca nu pun processing
			//  o sa prelucreze nodul 4 de 2 ori
			graph->visited[node->neighbours[i]] = PROCESSING;
			pthread_mutex_unlock(&graph_mutex);
			enqueue_task(tp, t);  // task-urile nu sunt executate automat
		}
	}
}

static void process_node(unsigned int idx)
{
	/* TODO: Implement thread-pool based processing of graph. */

	os_task_t *t = create_task(prelucrate_currentNode, graph->nodes[idx], NULL);
	enqueue_task(tp, t);
}


int main(int argc, char *argv[])
{
	FILE *input_file;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s input_file\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	input_file = fopen(argv[1], "r");
	DIE(input_file == NULL, "fopen");

	graph = create_graph_from_file(input_file);

	/* TODO: Initialize graph synchronization mechanisms. */

	pthread_mutex_init(&graph_mutex, NULL);
	tp = create_threadpool(NUM_THREADS);

	/*  se incepe cu procesarea primului nod
	 * => creare task pentru nod 0 ce va fi adaugat in coada
	 * in main e thread-ul principal => nodul 0 ruleaza pe thread-ul
	 * principal, urmand ca restul nodurilor sa ruleze pe thread-uri paralele
	 */
	process_node(0);

	wait_for_completion(tp);
	destroy_threadpool(tp);

	printf("%d", sum);
	pthread_mutex_destroy(&graph_mutex);
	return 0;
}

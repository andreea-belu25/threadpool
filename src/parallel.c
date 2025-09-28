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

/* Define graph synchronization mechanisms. */

pthread_mutex_t graph_mutex;  // mutex associated with the graph
/* (if someone tries to access the graph simultaneously, it won’t work);
 * threadpool, graph, and sum -> shared memory regions
 */

/* Define graph task argument. */

void prelucrate_currentNode(void *Node)
{
	// traversal and processing of graph nodes in parallel
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

			// if nodes 2 and 3 both have node 4 as a neighbor
			// and threads for nodes 2 and 3 run in parallel => without marking as PROCESSING,
			// node 4 would be processed twice
			graph->visited[node->neighbours[i]] = PROCESSING;
			pthread_mutex_unlock(&graph_mutex);
			enqueue_task(tp, t);  // tasks are not executed automatically
		}
	}
}

static void process_node(unsigned int idx)
{
	/* Implement thread-pool based processing of graph. */

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

	/* Initialize graph synchronization mechanisms. */

	pthread_mutex_init(&graph_mutex, NULL);
	tp = create_threadpool(NUM_THREADS);

	/* start with processing the first node
	 * => create a task for node 0 that will be added to the queue
	 * in main, the main thread executes => node 0 runs on the main thread,
	 * while the remaining nodes will run on parallel threads
	 */
	process_node(0);

	wait_for_completion(tp);
	destroy_threadpool(tp);

	printf("%d", sum);
	pthread_mutex_destroy(&graph_mutex);
	return 0;
}

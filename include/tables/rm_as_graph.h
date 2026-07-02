#ifndef __RM_AS_GRAPH__
#define __RM_AS_GRAPH__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vendor/utils.h"

struct graph_as;

typedef struct graph_px
{
    uint16_t px;
    struct graph_as *apt;
} graph_px_t;

typedef struct graph_as
{
	uint8_t aid;
    tree_t *px_data;
} graph_as_t;

typedef struct graph
{
    graph_as_t *as_data[256];
} graph_t;

graph_t *graph_create(uint8_t myaid);

void graph_show(graph_t *apt);

int graph_trace(graph_t *tar, void *pid_start, int pid_count, uint8_t as_ans[]);

int graph_trace_px(graph_t *tar, void *pid_start, int pid_count, uint8_t as_ans[]);

int graph_add(graph_t *tar, uint8_t aid1, uint8_t aid2, uint16_t npx);

void graph_clear(graph_t *tar);

#endif
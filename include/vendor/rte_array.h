#ifndef _RTE_ARRAY_H_
#define _RTE_ARRAY_H_

#include <stdlib.h>
#include <string.h>

#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_malloc.h>
#include <rte_mempool.h>

typedef struct rte_array
{
    int max_count;
    int count;
    int element_size;
    void *data;
} rte_array_t;

rte_array_t *rte_array_create(int max_count, int element_size);

int rte_array_add(rte_array_t *ans, void *element);
void *rte_array_get(rte_array_t *ans, int index);
int rte_array_update(rte_array_t *ans, int index, void *element);
int rte_array_delete(rte_array_t *ans, int index);

int rte_array_loop_function(rte_array_t *ans, int func(void *));

int rte_array_destroy(rte_array_t *ans);

#endif
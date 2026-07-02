#ifndef __RM_BR_TABLE_H__
#define __RM_BR_TABLE_H__
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_malloc.h>
#include <rte_mempool.h>

#include "vendor/cuckoo_hash.h"
#include "vendor/jsw_rbtree.h"
#include "vendor/rte_array.h"
#include "vendor/utils.h"



// BR info table
typedef struct br_info
{
    uint8_t nid[16];
    uint8_t port;
    uint32_t ip_addr;
} br_info_t;

typedef struct br_info_store
{
    uint16_t br_count;
    tree_t *store_data;
} br_info_store_t;

int
br_node_cmp(const void *p1, const void *p2);
void *
br_node_dup(void *p);

int br_store_insert(br_info_t *target);
br_info_t *br_store_search(uint8_t *nid);
int br_store_erase(uint8_t *nid);
int br_store_ret_node_list(br_info_t *br__list[]);

#endif
#ifndef __RM_PROXY_TABLE_H__
#define __RM_PROXY_TABLE_H__

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
// #include "tables/rm_as_graph.h"
// #include "tables/rm_status.h"




// PROXY info table
typedef struct proxy_info
{
    uint8_t nid[16];
    uint8_t port;
    uint32_t ip_addr;
} proxy_info_t;

typedef struct proxy_info_store
{
    uint16_t proxy_count;
    tree_t *store_data;
} proxy_info_store_t;

int
proxy_node_cmp(const void *p1, const void *p2);

void *
proxy_node_dup(void *p);
// functions of proxy data
/** 将代理注册信息 \b target 插入表中。 */
int proxy_store_insert(proxy_info_t *target);
proxy_info_t *proxy_store_search(uint8_t *nid);
int proxy_store_erase(uint8_t *nid);
int proxy_store_ret_node_list(proxy_info_t *proxy_list[]);

#endif
#ifndef __RM_INFO_H__
#define __RM_INFO_H__

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
#include "tables/rm_as_graph.h"
#include "tables/rm_status.h"
#include "tables/rm_sid_table.h"


#define AS_TABLE_SIZE 64
#define AS_BR_SIZE 64
#define AS_BR_PX_SIZE 32

#define N_HASH_POEWER 1
#define L_HASH_POEWER 2
#define T_HASH_POEWER 3



typedef struct my_basic_info
{
    int aid;
    uint8_t nid[16];
    int security_level;
    uint32_t sk;
    uint32_t port_ip[32];
    
    // info for strategy
    uint64_t quick_mark; // 64 FOR NOW
    rte_array_t *global_strategy_store;
    graph_t *as_graph;

    // status info
    rm_status_t *my_status;

} my_basic_info_t;



void basic_info_show();
void show_data(uint8_t *data, int count, bool is_mac);

// TODO: GRAPH



// functions
// initialize data structure of RM
int rm_data_initialize();

// basic info for RM
int global_set_as_info(uint8_t *nid, int aid, int sl, uint32_t sk);
int global_add_strategy(uint8_t t, uint8_t l, uint8_t *v);
void global_quick_set_strategy(uint8_t t, bool set);
bool global_quick_check_strategy(uint8_t t);
strategy_t *global_search_strategy(uint8_t t);
void clear_syn_rm_config();

void
node_rel(void *p);

#endif
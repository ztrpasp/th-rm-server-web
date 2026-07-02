#ifndef __RM_SID_TABLE_H__
#define __RM_SID_TABLE_H__

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

#define SID_LEN 36
#define MAX_SID_NUM 1024

typedef struct {
    uint8_t tsid[SID_LEN];
} tsid_entry_t;

typedef struct {
    tsid_entry_t sid_list[MAX_SID_NUM];
    uint32_t count;  // 当前SID数量
} tsid_table_t;


typedef struct strategy
{
    uint8_t tag;
    uint8_t len;
    uint8_t *value;
} strategy_t;

typedef struct sid_info
{
    uint16_t px; // if px == 0 then inner-AS provider
    uint8_t as_path_len; // todo:
    void *node_pt; // may be BR or PROXY node structure
    rte_array_t *strategy_array; // array to store strategy
    struct sid_info *next;
} sid_info_t;

typedef struct sid_table_node
{
    uint8_t key[20]; //may be N or L
    sid_info_t *sid_info_list; // the sid list
} sid_table_node_t;

typedef struct sid_table
{
    cuckoo_hash_t *N_hash_table;
    cuckoo_hash_t *L_hash_table;
    //cuckoo_hash_t *T_hash_table;
} sid_store_t;


// SID info
sid_info_t *sid_create(uint16_t px, void *node_pt, uint8_t as_path_len);
strategy_t *strategy_create(uint8_t t, uint8_t l, uint8_t *v);
int sid_add_strategy(sid_info_t *ans, strategy_t *tar);
int sid_info_destroy(sid_info_t *st);

int sid_store_insert(uint8_t *N_sid, uint8_t *L_sid, sid_info_t *info_node,
                    int cmp(sid_info_t *x, sid_info_t *y));
sid_table_node_t *sid_store_search(uint8_t *n_sid, uint8_t *l_sid);

int tsid_table_init();
int tsid_table_add(const uint8_t *new_sid);


#endif
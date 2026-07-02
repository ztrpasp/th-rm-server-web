#ifndef __RM_AS_TABLE_H__
#define __RM_AS_TABLE_H__

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
#include "tables/rm_br_table.h"




// AS info table
typedef struct connect_info
{
    br_info_t *br_pt; // the connecting BR
    rte_array_t *px_array; // px
} connect_info_t;

typedef struct as_info
{
    uint16_t aid;
    uint8_t security_level;
    rte_array_t *connect_array; // connect_info_t
} as_info_t;

typedef struct as_info_store
{
    rte_array_t *as_array; // as_info_t
} as_info_store_t;

typedef struct search_px_result
{
    br_info_t *target_br;
    as_info_t *target_as;
} search_px_result_t;

// AS info
connect_info_t *connect_info_create(br_info_t *br_pt);
int connect_info_insert(connect_info_t *ans, uint16_t px);
as_info_t *as_info_create(uint16_t aid, uint8_t security_level);
int as_info_insert(as_info_t *target, connect_info_t *info);

int as_store_insert(as_info_t *target);
as_info_t *as_store_search_aid(uint16_t key);
int as_store_search_px(uint16_t px, search_px_result_t *res);
void as_store_show(void);
int as_store_ret_as_px_ip_list(void *buf);

int matchAID(void *buf1, void *buf2);


int matchPX(void *buf1, void *buf2);

int as_info_destroy(as_info_t *ans);


#endif
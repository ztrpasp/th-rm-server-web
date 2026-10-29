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

// 名称、描述展示副本的最大长度：策略单元的 len 只有 1 字节
#define TSID_TEXT_MAX 255

/*
 * TSID 表的一条。数据面（worker lcore）写，Web 线程和 CLI 读，没有锁：
 * 读方一律用 tsid_table_snapshot 复制一份再用。名称和描述是展示用副本，
 * 已经过 display_text_copy 清洗（没有控制字符、是合法 UTF-8），以 NUL 结尾。
 */
typedef struct {
    uint8_t tsid[SID_LEN];
    uint8_t name_len;       // 0 表示通告里没有名称
    uint8_t desc_len;       // 0 表示通告里没有描述
    uint8_t name_verified;  // 1：SHA1(名称) == L_SID
    char name[TSID_TEXT_MAX + 1];
    char desc[TSID_TEXT_MAX + 1];
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

// sid_store_insert 的返回值：同一来源、策略没变（例如终端周期刷新），什么都没做，不是错误
#define SID_STORE_UNCHANGED (-2)

int sid_store_insert(uint8_t *N_sid, uint8_t *L_sid, sid_info_t *info_node,
                    int cmp(sid_info_t *x, sid_info_t *y));
sid_table_node_t *sid_store_search(uint8_t *n_sid, uint8_t *l_sid);

int tsid_table_init();
int tsid_table_add(const uint8_t *new_sid);
int tsid_table_snapshot(uint32_t index, tsid_entry_t *out);


#endif
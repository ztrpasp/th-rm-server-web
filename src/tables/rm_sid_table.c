#include <stdbool.h>

#include "tables/rm_sid_table.h"
#include "tables/rm_tables.h"
#include "packets/rte_CoLoR.h"
#include "telemetry/rm_log.h"
#include "vendor/display_text.h"
#include "vendor/sha1.h"

static void tsid_table_record(const uint8_t *N_sid, const uint8_t *L_sid,
    const sid_info_t *info, bool add);

// functions of SID 
sid_info_t *
sid_create(uint16_t px, void *node_pt, uint8_t as_path_len)
{
    if(!node_pt)
        return NULL;
    sid_info_t *ans;
    if((ans = (sid_info_t *)rte_malloc(NULL, sizeof(sid_info_t), 0)) == NULL)
        return NULL;
    ans->px = px;
    ans->node_pt = node_pt;
    ans->strategy_array = rte_array_create(4, sizeof(strategy_t));
    if (ans->strategy_array == NULL) {
        rte_free(ans);
        return NULL;
    }
    ans->next = NULL;
    ans->as_path_len = as_path_len;
    return ans;
}

strategy_t *
strategy_create(uint8_t t, uint8_t l, uint8_t *v)
{
    strategy_t *ans = (strategy_t *)rte_malloc(NULL, sizeof(strategy_t), 0);
    if (ans == NULL)
        return NULL;
    ans->tag = t;
    ans->len = l;
    ans->value = v;
    return ans;
}

int
sid_add_strategy(sid_info_t *ans, strategy_t *tar)
{
    if(!ans || !tar)
        return -1;
    if (rte_array_add(ans->strategy_array, tar) != 1) {
        rte_free(tar->value);
        rte_free(tar);
        return -1;
    }
    rte_free(tar); // the array stores a copy of the strategy descriptor
    return 1;
}

int
sid_free_single_strategy(void *st)
{
    if(!st)
        return -1;
    strategy_t *sst = (strategy_t *)st;
    rte_free(sst->value);
    return 1;
}

int
sid_info_destroy(sid_info_t *st)
{
    if(!st)
        return -1;
    rte_array_loop_function(st->strategy_array, sid_free_single_strategy);
    rte_array_destroy(st->strategy_array);
    rte_free(st);
    return 1;
}

bool
check_same_sid_node(sid_info_t *a, sid_info_t *b)
{
    if(a == NULL || b == NULL)
        return false;
    else if(a->px == b->px && a->node_pt == b->node_pt)
        return true;
    else
        return false;
}

// 两个节点的策略个数、顺序和每个策略的 tag/len/value 都相同才算相同
bool
check_same_strategy(sid_info_t *a, sid_info_t *b)
{
    if(a == NULL || b == NULL || a->strategy_array == NULL || b->strategy_array == NULL)
        return false;
    rte_array_t *xa = a->strategy_array, *xb = b->strategy_array;
    if(xa->count != xb->count)
        return false;
    strategy_t *x1, *x2;
    for(int i = 0; i < xa->count; i++)
    {
        x1 = (strategy_t *)((uint8_t *)xa->data + i * xa->element_size);
        x2 = (strategy_t *)((uint8_t *)xb->data + i * xb->element_size);
        if(x1->tag != x2->tag || x1->len != x2->len)
            return false;
        if(x1->len > 0 && memcmp(x1->value, x2->value, x1->len) != 0)
            return false;
    }
    return true;
}

/*
 * 返回 1：已插入或已替换策略，需要转发；0：已插入，不转发；
 * SID_STORE_UNCHANGED（-2）：同一来源、策略相同，什么都没做；-1：失败。
 * 返回值 >= 0 时 info_node 归表所有（挂进链表，或策略数组被取走后释放），调用方不能再用；
 * 返回负数时 info_node 仍归调用方，由调用方释放。
 */
int
sid_store_insert(uint8_t *N_sid, uint8_t *L_sid, sid_info_t *info_node,
                    int cmp(sid_info_t *x, sid_info_t *y))
{
    (void)cmp;
    if(!info_node){
        RM_LOG_ERROR(RM_LOG_MODULE_TABLE, RM_EVENT_PACKET_MALFORMED,
            RM_LOG_PORT_NONE, "SID insert rejected: info node is null");
        return -1;
    }

    struct cuckoo_hash_item *check_pt;
    sid_table_node_t *head = (sid_table_node_t *)rte_malloc(NULL, sizeof(sid_table_node_t), 0);
    if (head == NULL) {
        RM_LOG_ERROR(RM_LOG_MODULE_TABLE, RM_EVENT_RESOURCE_EXHAUSTED,
            RM_LOG_PORT_NONE, "SID insert failed: unable to allocate table node");
        return -1;
    }
    head->sid_info_list = NULL;

    uint8_t len = 0;
    void *key = NULL;
    cuckoo_hash_t *hash_pt = NULL;

    // 如果两个都有则同时插入
    // L first
    if(L_sid)
    {
        key = L_sid;
        len = 20;
        hash_pt = sid_store.L_hash_table;
    }
    // N second
    else if(N_sid)
    {
        key = N_sid;
        len = 16;
        hash_pt = sid_store.N_hash_table;
    }
    else {
        RM_LOG_ERROR(RM_LOG_MODULE_TABLE, RM_EVENT_PACKET_MALFORMED,
            RM_LOG_PORT_NONE, "SID insert rejected: neither N-SID nor L-SID supplied");
        rte_free(head);
        return -1;
    }
    
    rte_memcpy(head->key, key, len);
    check_pt = cuckoo_hash_insert(hash_pt, head->key, len, head);
    // new registered
    if(check_pt == NULL)
    {
        // N、L 都有的 SID 加进 TSID 总表（给 Web/CLI/tag 40 用），并记下名称和描述
        tsid_table_record(N_sid, L_sid, info_node, true);
        head->sid_info_list = info_node;
        return 1;
    }
    // head already register
    else if(check_pt != CUCKOO_HASH_FAILED)
    {
        sid_info_t *p_npt = NULL, *npt = NULL;
        sid_info_t *stored = info_node; // 这次存进表、要据此更新 TSID 名称和描述的节点
        rte_free(head);
        int fwd_flag = 0;
        for(npt = ((sid_table_node_t *)(check_pt->value))->sid_info_list;;)
        {
            /*
            // same inner-as node, update strategy info
            if(check_same_sid_node(info_node, npt))
            {
                if(check_same_strategy(info_node, npt)){
                    cli_print("same strategy\n");
                    return -2;
                }
                rte_array_destroy(npt->strategy_array);
                npt->strategy_array = info_node->strategy_array;
                break;
            }
            // to the tail
            else if(npt->next == NULL)
            {
                npt->next = info_node;
                break;
            }
            // compare
            else if(cmp && cmp(info_node, npt) > 0)
            {
                // insert as head node
                if(p_npt == NULL)
                {
                    info_node->next = npt;
                    ((sid_table_node_t *)(check_pt->value))->sid_info_list = info_node;
                    break;
                }
                else
                {
                    p_npt->next = info_node;
                    info_node->next = npt;
                    break;
                }
            }
            else
            {
                p_npt = npt;
                npt = npt->next;
            }*/

           // same inner-as node, update strategy info
           if(check_same_sid_node(info_node, npt))
           {
               if(check_same_strategy(info_node, npt)){
                   return SID_STORE_UNCHANGED;
               }
               // 换上新的策略数组：旧数组连同每个 value 一起释放；
               // info_node 只用来带新策略，不进链表，取走数组后释放
               rte_array_loop_function(npt->strategy_array, sid_free_single_strategy);
               rte_array_destroy(npt->strategy_array);
               npt->strategy_array = info_node->strategy_array;
               rte_free(info_node);
               stored = npt;
               fwd_flag = 1;
               break;
           }
           // to the tail
           else if(npt->next == NULL)
           {
               npt->next = info_node;
               fwd_flag = 0;
               break;
           }
           else if(info_node->as_path_len < npt->as_path_len)
           {
                // insert as head node
                // 只有这种情况下需要转发，也就是说新来的是path最短的
                if(p_npt == NULL)
                {
                    info_node->next = npt;
                    ((sid_table_node_t *)(check_pt->value))->sid_info_list = info_node;
                    fwd_flag = 1;
                    break;
                }
                else
                {
                    p_npt->next = info_node;
                    info_node->next = npt;
                    fwd_flag = 0;
                    break;
                }
           }
           else
           {
               p_npt = npt;
               npt = npt->next;
           }

        }
        // 名称、描述以最近一次存进表的通告为准
        tsid_table_record(N_sid, L_sid, stored, false);
        //return 2;
        return fwd_flag;
    }
    else
    {
        RM_LOG_ERROR(RM_LOG_MODULE_TABLE, RM_EVENT_RESOURCE_EXHAUSTED,
            RM_LOG_PORT_NONE, "SID insert failed: hash table expansion failed");
        rte_free(head);
        return -1;
    }

    return -1;
}

sid_table_node_t *
sid_store_search(uint8_t *n_sid, uint8_t *l_sid)
{
    struct cuckoo_hash_item *pt;
    
    // search N_hash_table first
    if(n_sid)
    {
        if((pt = cuckoo_hash_lookup(sid_store.N_hash_table, n_sid, 16)) != NULL)
            return (sid_table_node_t *) (pt->value);
    }
    if(l_sid)
    {
        if((pt = cuckoo_hash_lookup(sid_store.L_hash_table, l_sid, 20)) != NULL)
            return (sid_table_node_t *) (pt->value);
    }
    return NULL;
}

static int sid_equal(const uint8_t *sid1, const uint8_t *sid2) {
    return memcmp(sid1, sid2, SID_LEN) == 0;
}

int tsid_table_init()
{
    g_tsid_table.count = 0;
    return 0;
}


/**
 * 返回值为0：该SID已存在，不添加
 * 返回值为1：成功添加新的SID
 * 返回值为-1：SID表已满，不添加
 */
int tsid_table_add(const uint8_t *new_sid)
{
    for(uint32_t i = 0; i < g_tsid_table.count; i++)
    {
        if(sid_equal(g_tsid_table.sid_list[i].tsid, new_sid))
        {
            return 0;//如果sid已存在，我们不会把他加到sid总表中
        }
    }

    //检查容量
    if(g_tsid_table.count >= MAX_SID_NUM)
    {
        RM_LOG_ERROR(RM_LOG_MODULE_TABLE, RM_EVENT_RESOURCE_EXHAUSTED,
            RM_LOG_PORT_NONE, "TSID table is full capacity=%u", MAX_SID_NUM);
        return -1;
    }

    //添加：先把整条写好，再让 count 加 1，Web/CLI 看到新的 count 时这条已经完整
    tsid_entry_t *entry = &g_tsid_table.sid_list[g_tsid_table.count];
    memcpy(entry->tsid, new_sid, SID_LEN);
    entry->name_len = 0;
    entry->desc_len = 0;
    entry->name_verified = 0;
    entry->name[0] = '\0';
    entry->desc[0] = '\0';
    __atomic_store_n(&g_tsid_table.count, g_tsid_table.count + 1, __ATOMIC_RELEASE);
    return 1;
}

static int
tsid_table_find(const uint8_t *sid)
{
    for(uint32_t i = 0; i < g_tsid_table.count; i++)
    {
        if(sid_equal(g_tsid_table.sid_list[i].tsid, sid))
            return (int)i;
    }
    return -1;
}

// 写一段展示文本：先写内容，最后写长度（读方按长度取，见 tsid_table_snapshot）
static void
tsid_text_store(char *dst, uint8_t *len_field, const strategy_t *st)
{
    size_t n = display_text_copy(dst, TSID_TEXT_MAX + 1,
        st != NULL ? st->value : NULL, st != NULL ? st->len : 0);
    __atomic_store_n(len_field, (uint8_t)n, __ATOMIC_RELEASE);
}

/*
 * 从 info 的策略里取 SID 名称（tag 13）和描述（tag 14），更新 TSID 表里这个 SID 的展示副本。
 * - 名称要满足 SHA1(名称) == L_SID 才算已校验；不满足只记 warn，照样展示，标成未校验；
 * - 同一种 tag 出现多次时取第一个，记 warn；
 * - 通告里没有的字段清空：以最近一次存进表的通告为准。
 * 只处理 N、L 都有的 SID（TSID 表只收这种）。add 为 true 时先把 SID 加进 TSID 表。
 */
static void
tsid_table_record(const uint8_t *N_sid, const uint8_t *L_sid,
    const sid_info_t *info, bool add)
{
    uint8_t sid[SID_LEN];
    const strategy_t *name = NULL, *desc = NULL;
    bool duplicated = false;
    uint8_t verified = 0;
    int index;

    if(N_sid == NULL || L_sid == NULL || info == NULL || info->strategy_array == NULL)
        return;
    memcpy(sid, N_sid, COLOR_N_SID_LEN);
    memcpy(sid + COLOR_N_SID_LEN, L_sid, COLOR_L_SID_LEN);
    if(add)
        (void)tsid_table_add(sid);
    if((index = tsid_table_find(sid)) < 0)
        return; // TSID 表满了，没加进去

    rte_array_t *arr = info->strategy_array;
    for(int i = 0; i < arr->count; i++)
    {
        const strategy_t *st = (const strategy_t *)((uint8_t *)arr->data + i * arr->element_size);
        const strategy_t **slot = st->tag == STRATEGY_SID_NAME ? &name :
            (st->tag == STRATEGY_SID_DESC ? &desc : NULL);
        if(slot == NULL)
            continue;
        if(*slot != NULL)
            duplicated = true;
        else
            *slot = st;
    }
    if(duplicated)
        RM_LOG_WARN(RM_LOG_MODULE_ANN, RM_EVENT_PACKET_MALFORMED, RM_LOG_PORT_NONE,
            "ANN unit carries SID name or description more than once; using the first");

    if(name != NULL && name->len > 0)
    {
        uint8_t digest[RM_SHA1_DIGEST_LEN];
        rm_sha1(name->value, name->len, digest);
        verified = memcmp(digest, L_sid, COLOR_L_SID_LEN) == 0;
        if(!verified)
            RM_LOG_WARN(RM_LOG_MODULE_ANN, RM_EVENT_ANN_NAME_UNVERIFIED, RM_LOG_PORT_NONE,
                "SID name does not hash to L-SID %02x%02x%02x%02x...; shown as unverified",
                L_sid[0], L_sid[1], L_sid[2], L_sid[3]);
    }

    tsid_entry_t *entry = &g_tsid_table.sid_list[index];
    tsid_text_store(entry->name, &entry->name_len, name);
    tsid_text_store(entry->desc, &entry->desc_len, desc);
    __atomic_store_n(&entry->name_verified, verified, __ATOMIC_RELEASE);
}

static void
tsid_text_load(char *dst, uint8_t *dst_len, const char *src, const uint8_t *src_len)
{
    size_t n = __atomic_load_n(src_len, __ATOMIC_ACQUIRE); // uint8_t，不会超过 TSID_TEXT_MAX
    memcpy(dst, src, n);
    dst[n] = '\0';
    *dst_len = (uint8_t)strnlen(dst, n);
}

/*
 * 把 TSID 表第 index 条复制到 out，给 Web 线程和 CLI 用。成功返回 0，下标越界返回 -1。
 * 数据面可能正在改这一条：复制出来的名称、描述最坏是新旧内容混在一起，
 * 但一定以 NUL 结尾、不会越界。这些字段只用于展示，不参与任何判定。
 */
int
tsid_table_snapshot(uint32_t index, tsid_entry_t *out)
{
    if(out == NULL)
        return -1;
    uint32_t count = __atomic_load_n(&g_tsid_table.count, __ATOMIC_ACQUIRE);
    if(index >= count || index >= MAX_SID_NUM)
        return -1;

    const tsid_entry_t *entry = &g_tsid_table.sid_list[index];
    memcpy(out->tsid, entry->tsid, SID_LEN);
    tsid_text_load(out->name, &out->name_len, entry->name, &entry->name_len);
    tsid_text_load(out->desc, &out->desc_len, entry->desc, &entry->desc_len);
    out->name_verified = out->name_len > 0 &&
        __atomic_load_n(&entry->name_verified, __ATOMIC_ACQUIRE);
    return 0;
}

#include "tables/rm_sid_table.h"
#include "tables/rm_tables.h"
#include "telemetry/rm_log.h"


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

bool
check_same_strategy(sid_info_t *a, sid_info_t *b)
{
    if(a == NULL || b == NULL)
        return false;
    void *sa = a->strategy_array->data, *sb = b->strategy_array->data;
    int es = a->strategy_array->element_size, c = a->strategy_array->count, i;
    int j;
    strategy_t *x1, *x2;
    for(i = 0; i < c; i++)
    {
        x1 = (strategy_t *)(sa + i * es);
        x2 = (strategy_t *)(sb + i * es);
        if(x1->tag != x2->tag || x1->len != x2->len)
            return false;
        for(j = 0; j < x1->len; j++)
            if((x1->value)[j] != (x2->value)[j])
                return false;
    }
    return true;
}

int
sid_store_insert(uint8_t *N_sid, uint8_t *L_sid, sid_info_t *info_node,
                    int cmp(sid_info_t *x, sid_info_t *y))
{
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
        if(N_sid && L_sid)//如果两个都有，我们就要把它添加到TSID总表之中，这里的N_sid和L_sid都是指向数据包缓冲区的指针，按照解析顺序先N，所以从N指针开始往后读36个字节就可以把N和L都拷贝进来
        {
            (void)tsid_table_add(N_sid);
        }
        head->sid_info_list = info_node;
        return 1;
    }
    // head already register
    else if(check_pt != CUCKOO_HASH_FAILED)
    {
        sid_info_t *p_npt = NULL, *npt = NULL;
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
                   return -2;
               }
               rte_array_destroy(npt->strategy_array);
               npt->strategy_array = info_node->strategy_array;
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

    //添加
    memcpy(g_tsid_table.sid_list[g_tsid_table.count].tsid, new_sid, SID_LEN);
    g_tsid_table.count++;
    return 1;
}

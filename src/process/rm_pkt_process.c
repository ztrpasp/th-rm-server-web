#include <stdlib.h>
#include <string.h>

#include <rte_per_lcore.h>

#include "process/rm_pkt_process.h"
#include "tables/rm_tables.h"
#include "packets/pkt.h"
#include "packets/rte_CoLoR.h"
#include "commandline/cli.h"
#include "telemetry/rm_log.h"
#include "telemetry/rm_metrics.h"

int
insert_cmp(sid_info_t *wait, sid_info_t *now)
{
    if(!wait || !now)
        return 0;

    if(wait->px < now->px)
        return 1;
    else
        return -1;
}

void
show_mark_t_array(mark_t *ans)
{
    if(!ans)
        return;
    int i, j;
    ANN_unit_front_t *head;
    for(i = 0; ; i++)
    {
        if(ans[i].unit_head == NULL)
            break;
        head = (ANN_unit_front_t *)(ans[i].unit_head);
        cli_print("Pass: %s\n", ans[i].filtered ? "FILTERED" : "PASSED");
        cli_print("Flag: %x\n", head->flag);
        cli_print("N_SID: ");show_data(ans[i].N_pt, 16, false);cli_print("\n");
        cli_print("L_SID: ");show_data(ans[i].L_pt, 20, false);cli_print("\n");
        cli_print("NID: ");show_data(ans[i].nid_pt, 16, false);cli_print("\n");
        cli_print("StrategyN: %d\n", head->strategy_num);
        for(j = 0; j < head->strategy_num; j++)
        {
            cli_print("S_tag: %x S_len: %d\n", ans[i].strategy_pt[j].tag, ans[i].strategy_pt[j].len);
        }
    }
}

void
show_px_unit_array(px_unit_t *ans)
{
    if(!ans)
        return;
    int i, j;
    for(i = 0; ans[i].br_pt != NULL; i++)
    {
        cli_print("Forward to PX %x UNIT ", ans[i].px);
        for(j = 0; (ans[i].unit_to_forward)[j] != 255; j++)
        {
            cli_print("%d ", (ans[i].unit_to_forward)[j]);
        }
        cli_print("\n");
    }
}

uint16_t
rm_ANN_get_last_px(void *pkt)
{
    if(!pkt)
        return RM_ERR_NULL_PT_INPUT;
    ANN_front_t *pt = (ANN_front_t *)pkt;
    return *(uint16_t *)(pkt + pt->pkg_len - 2);
}

uint16_t *
rm_ANN_get_px_pt(void *pkt)
{
    if(!pkt)
        return NULL;
    ANN_front_t *pt = (ANN_front_t *)pkt;
    return (uint16_t *)(pkt + pt->pkg_len - GET_ANN_PX_NUM(pt->unit_px_num) * 2);
}

int
rm_ANN_check_filter(ANN_front_t *head, mark_t *info_mark)
{
    if(!head || !info_mark)
        return RM_ERR_NULL_PT_INPUT;

    int check, px_num = GET_ANN_PX_NUM(head->unit_px_num);
    uint8_t as_trace[32];
    
    // check N/L/nid legally
    if(px_num == 0 && info_mark->nid_pt == NULL)
    {
        RM_LOG_WARN(RM_LOG_MODULE_ANN, RM_EVENT_PACKET_MALFORMED,
            RM_LOG_PORT_NONE, "inner ANN has no NID");
        return 1;
    }
    if(info_mark->N_pt == NULL && info_mark->L_pt == NULL)
    {
        RM_LOG_WARN(RM_LOG_MODULE_ANN, RM_EVENT_PACKET_MALFORMED,
            RM_LOG_PORT_NONE, "ANN unit has neither N-SID nor L-SID");
        return 1;
    }
    if(global_quick_check_strategy(STRATEGY_TRACE_EVERY_PKG) && px_num != 0)
    {
        // *(uint16_t *)(pkt + pt->pkg_len - 2);
        check = graph_trace_px(my_as_info->as_graph, (void *)head + head->pkg_len - px_num * 2, px_num, as_trace);
        if(check == 0)
        {
            RM_LOG_WARN(RM_LOG_MODULE_ANN, RM_EVENT_ANN_POLICY_DENIED,
                RM_LOG_PORT_NONE, "ANN path validation failed");
            return 1;
        }
        rm_metrics_record_trace(RM_PACKET_ANN, as_trace[check - 1]);
    }
    return 0;
}

// parse ANN and store , mark ann_unit, ret number of ANN_unit
int
rm_ANN_parse_store(void *pkt, mark_t *mark_array, search_px_result_t *src_px_res)
{
    if(!pkt || !mark_array || !src_px_res)
        return RM_ERR_NULL_PT_INPUT;
    
    void *pt, *N_sid, *L_sid, *nid, *value_pt;
    sid_info_t *sid_mid;
//    br_info_t *br_mid;
    proxy_info_t *proxy_mid;
    strategy_t *st;
    uint16_t last_px = 0;
    ANN_front_t *ann_front = (ANN_front_t *)pkt;
    ANN_unit_front_t *unit_front;
    uint8_t i, j, strategy_count;
    uint8_t unit_count = GET_ANN_UNIT_NUM(ann_front->unit_px_num);

    // check unit_count limit
    if(unit_count > 32)
    {
        RM_LOG_WARN(RM_LOG_MODULE_ANN, RM_EVENT_PACKET_MALFORMED,
            RM_LOG_PORT_NONE, "ANN unit count %u exceeds 32", unit_count);
        return RM_PROCESS_ERR;
    }

    // check the checksum
    if(ANN_check_checksum(pkt) == 0)
    {
        RM_LOG_WARN(RM_LOG_MODULE_ANN, RM_EVENT_PACKET_BAD_CHECKSUM,
            RM_LOG_PORT_NONE, "ANN checksum validation failed");
        return RM_ERR_CHECKSUM_UNPASSED;
    }

    // get the last px
    if(GET_ANN_PX_NUM(ann_front->unit_px_num) != 0)
    {
        last_px = rm_ANN_get_last_px(pkt);
        // check and use px to get as and br
        if(0 >= as_store_search_px(last_px, src_px_res))
        {
            RM_LOG_WARN(RM_LOG_MODULE_ANN, RM_EVENT_ANN_NO_SOURCE_PATH,
                RM_LOG_PORT_NONE, "no border-router mapping for PX 0x%04x", last_px);
            return RM_PROCESS_ERR;
        }
        rm_metrics_add(RM_METRIC_ANN_FROM_OUTSIDE, 1);
    }
    else
    {
        rm_metrics_add(RM_METRIC_ANN_FROM_INSIDE, 1);
        src_px_res->target_as = NULL;
        src_px_res->target_br = NULL;
    }

    // for every unit announced in ANN
    for(i = 0, pt = pkt + sizeof(ANN_front_t); i < unit_count; i++)
    {
        unit_front = (ANN_unit_front_t *)pt;
        pt += sizeof(ANN_unit_front_t);

        // process N, L and nid
        if(CHECK_ANN_UNIT_FLAG_N(unit_front->flag))
        {
            N_sid = pt;
            pt += 16;
        }
        else
            N_sid = NULL;
        if(CHECK_ANN_UNIT_FLAG_L(unit_front->flag))
        {
            L_sid = pt;
            pt += 20;
        }
        else
            L_sid = NULL;
        if(CHECK_ANN_UNIT_FLAG_I(unit_front->flag))
        {
            nid = pt;
            pt += 16;
        }
        else
            nid = NULL;

        mark_array[i].unit_head = unit_front;
        mark_array[i].N_pt = N_sid;
        mark_array[i].L_pt = L_sid;
        mark_array[i].nid_pt = nid;

        // parse ann strategy part and mark
        strategy_count = unit_front->strategy_num;
        if (strategy_count > 32) {
            RM_LOG_WARN(RM_LOG_MODULE_ANN, RM_EVENT_PACKET_MALFORMED,
                RM_LOG_PORT_NONE,
                "ANN strategy count %u exceeds 32", strategy_count);
            return RM_PROCESS_ERR;
        }
        for(j = 0; j < strategy_count; j++)
        {
            // mark the strategy pt
            mark_array[i].strategy_pt[j].tag = *(uint8_t *)pt;
            mark_array[i].strategy_pt[j].len = *(uint8_t *)(pt + 1);
            mark_array[i].strategy_pt[j].value = (uint8_t *)pt + 2;
            pt += 2 + mark_array[i].strategy_pt[j].len;
        }

        // ### check and filter ANN_unit
        if(1 == rm_ANN_check_filter(ann_front, mark_array + i))
        {
            mark_array[i].filtered = true;
        }
        else
        {
            mark_array[i].filtered = false;
            uint8_t as_path_len = GET_ANN_PX_NUM(ann_front->unit_px_num);
            // find the BR or PROXY node to initialize sid_node
            if(GET_ANN_PX_NUM(ann_front->unit_px_num) == 0)
            {
                // use nid to find PROXY
                proxy_mid = proxy_store_search(nid);
                if(NULL == proxy_mid)
                {
                    RM_LOG_WARN(RM_LOG_MODULE_ANN, RM_EVENT_ANN_PROXY_NOT_FOUND,
                        RM_LOG_PORT_NONE, "no proxy mapping for ANN NID");
                    return RM_PROCESS_ERR;
                }
                if(NULL == (sid_mid = sid_create(0, proxy_mid, 0)))
                {
                    RM_LOG_ERROR(RM_LOG_MODULE_ANN, RM_EVENT_RESOURCE_EXHAUSTED,
                        RM_LOG_PORT_NONE, "unable to allocate local SID entry");
                    return RM_ERR_NO_MEMORY;
                }
            }
            else
            {
                if(NULL == (sid_mid = sid_create(last_px, src_px_res->target_br, as_path_len)))
                {
                    RM_LOG_ERROR(RM_LOG_MODULE_ANN, RM_EVENT_RESOURCE_EXHAUSTED,
                        RM_LOG_PORT_NONE, "unable to allocate remote SID entry");
                    return RM_ERR_NO_MEMORY;
                }
            }

            // insert strategy
            for(j = 0; j < strategy_count; j++)
            {
                value_pt = (void *)rte_malloc(NULL, mark_array[i].strategy_pt[j].len, 0);
                if (value_pt == NULL) {
                    sid_info_destroy(sid_mid);
                    RM_LOG_ERROR(RM_LOG_MODULE_ANN, RM_EVENT_RESOURCE_EXHAUSTED,
                        RM_LOG_PORT_NONE, "unable to copy ANN strategy value");
                    return RM_ERR_NO_MEMORY;
                }
                rte_memcpy(value_pt, mark_array[i].strategy_pt[j].value, mark_array[i].strategy_pt[j].len);
                st = strategy_create(mark_array[i].strategy_pt[j].tag, mark_array[i].strategy_pt[j].len, value_pt);
                if (st == NULL) {
                    rte_free(value_pt);
                    sid_info_destroy(sid_mid);
                    RM_LOG_ERROR(RM_LOG_MODULE_ANN, RM_EVENT_RESOURCE_EXHAUSTED,
                        RM_LOG_PORT_NONE, "unable to allocate ANN strategy descriptor");
                    return RM_ERR_NO_MEMORY;
                }
                if (sid_add_strategy(sid_mid, st) != 1) {
                    sid_info_destroy(sid_mid);
                    RM_LOG_ERROR(RM_LOG_MODULE_ANN, RM_EVENT_RESOURCE_EXHAUSTED,
                        RM_LOG_PORT_NONE, "unable to append ANN strategy");
                    return RM_ERR_NO_MEMORY;
                }
            }
         
            // insert into sid_table
            // ### decide the priority of forwarding
            int sid_store_insert_ret = sid_store_insert(N_sid, L_sid, sid_mid, insert_cmp);
            if(0 > sid_store_insert_ret)
            {
                RM_LOG_ERROR(RM_LOG_MODULE_TABLE, RM_EVENT_RESOURCE_EXHAUSTED,
                    RM_LOG_PORT_NONE, "unable to insert SID entry");
                sid_info_destroy(sid_mid);
                // mark the sid
                mark_array[i].filtered = true;
            }
            else if(0 == sid_store_insert_ret)
            {
                mark_array[i].filtered = true;
            }
        }
    }
    // mark the end
    if(i < 32)
        mark_array[i].unit_head = NULL;

    return RM_PROCESS_SUCCESS;
}

// input pkg and mark_array, return numbers of px and the units needed forwarded
int
rm_ANN_forward_px_units(void *pkt, mark_t *mark_array, search_px_result_t *src_px_res, px_unit_t *px_unit_array)
{
    if(!pkt || !mark_array || !src_px_res || !px_unit_array)
        return RM_ERR_NULL_PT_INPUT;

    ANN_front_t *front = (ANN_front_t *)pkt;
    as_info_t *as_pt;
    uint16_t px_mid = 0x0;
    connect_info_t *con_pt;
    void *as_pt_start = as_store.as_array->data;
    void *connect_pt_start;
    int i, j, k, px_n, un_n, s_i,
        unit_count = GET_ANN_UNIT_NUM(front->unit_px_num),
        as_c = as_store.as_array->count, as_s = sizeof(as_info_t),
        con_c, con_s = sizeof(connect_info_t);
    
    // check for NULL pointer
    if(as_pt_start == NULL || as_c == 0)
        return 0;
    
    // for every AS to decide what units should be forwarded to which px
    for(i = 0, px_n = 0; i < as_c; i++)
    {
        as_pt = (as_info_t *)(as_pt_start + as_s * i);
        
        // ### decide if send ANN to this as
        if(src_px_res->target_as && src_px_res->target_as->aid == as_pt->aid)
            continue;
        if(global_quick_check_strategy(STRATEGY_AS_SECURITY_CHECK))
        {
            if(my_as_info->security_level > as_pt->security_level) {
                continue;
            }
        }

        // ### decide target px, use the first px by default
        con_c = as_pt->connect_array->count;
        if(con_c == 0)
            continue;
        connect_pt_start = as_pt->connect_array->data;
        px_mid = 0;
        for(k = 0; k < con_c; k++)
        {
            con_pt = (connect_info_t *)(connect_pt_start + k * con_s);
            if(con_pt->px_array->count > 0 && con_pt->px_array->data != NULL)
            {
                px_mid = *(uint16_t *)(con_pt->px_array->data); // just use the first px
                if(px_mid > 0)
                    break;
            }
        }
        if(con_pt->br_pt == NULL || px_mid == 0)
            continue;
        px_unit_array[px_n].br_pt = con_pt->br_pt;
        px_unit_array[px_n].px = px_mid;

        // ### LOOP to decide which units to send
        for(j = 0, un_n = 0; j < unit_count; j++)
        {
            bool unit_filter = mark_array[j].filtered;
            
            // # decide if forward the unit
            for(s_i = 0; unit_filter == false && s_i < 32; s_i++)
            {
                switch(mark_array[j].strategy_pt[s_i].tag)
                {
                    // end of Strategy
                    case 0:
                    {
                        s_i = 32;
                        break;
                    }
                    // S1 : Security level
                    case STRATEGY_SECURITY_LEVEL:
                    {
                        if(!global_quick_check_strategy(STRATEGY_SECURITY_LEVEL_CHECK))
                            break;
                        uint8_t sl = *(uint8_t *)(mark_array[j].strategy_pt[s_i].value);
                        if(sl > as_pt->security_level)
                        {
                            unit_filter = true;
                        }
                        break;
                    }
                    // S2 : the ROAD / white list
                    case STRATEGY_ANNOUNCE_PATH:
                    {
                        if(!global_quick_check_strategy(STRATEGY_ANNOUNCE_PATH))
                            break;
                        uint8_t road_i, road_limi = mark_array[j].strategy_pt[s_i].len,
                                *road_data = mark_array[j].strategy_pt[s_i].value;
                        // loop to check the list
                        for(road_i = 0; road_i < road_limi; road_i++)
                        {
                            if(road_data[road_i] == as_pt->aid)
                                break;
                        }
                        if(road_limi == road_i)
                        {
                            unit_filter = true;
                        }
                        break;
                    }
                    default:break;
                }
            }

            // add this unit to px
            if(unit_filter == false)
                px_unit_array[px_n].unit_to_forward[un_n++] = j;
            
        }
        // mark the end of unit array
        if(un_n != 0)
            px_unit_array[px_n++].unit_to_forward[un_n] = 255;
    }
    // mark the end of px_units array
    px_unit_array[px_n].br_pt = NULL;

    return px_n;
}

// construct ANNs
int
rm_ANN_construct_ann(void *pkt, mark_t *mark_array, px_unit_t *px_units, void *ann_buf[32])
{
    if(!pkt || !mark_array || !px_units)
        return RM_ERR_NULL_PT_INPUT;
    
    int i, j, k, index, num;
    uint8_t ubuf[128];
    void *apt;
    mark_t *mpt;
    ANN_front_t *ans_head, *old_head = (ANN_front_t *)pkt;
    for(i = 0; ; i++)
    {
        if(px_units[i].br_pt == NULL)
            break;

        apt = ann_buf[i];
        ANN_init(apt);

        for(j = 0; ; j++)
        {
            index = px_units[i].unit_to_forward[j];
            if(index == 255)
                break;
            mpt = mark_array + index;
            
            // ### modify unit
            if(global_quick_check_strategy(STRATEGY_N_SID_FIELD_OMIT))
                mpt->N_pt = NULL;
            if(global_quick_check_strategy(STRATEGY_L_SID_FIELD_OMIT))
                mpt->L_pt = NULL;
            if(mpt->N_pt == NULL && mpt->L_pt == NULL)
                continue;
            if(global_quick_check_strategy(STRATEGY_NID_FIELD_OMIT))
                mpt->nid_pt = NULL;
            ANN_unit_construct(ubuf, mpt->N_pt, mpt->L_pt, mpt->nid_pt);
            
            // ### choose and modify strategy
            num = ((ANN_unit_front_t *)mpt->unit_head)->strategy_num;
            for(k = 0; k < num; k++)
            {
                ANN_unit_append_strategy(ubuf, mpt->strategy_pt[k].tag, 
                                            mpt->strategy_pt[k].len, mpt->strategy_pt[k].value);
            }
            ANN_append_unit(apt, ubuf);
        }

        // append px
        num = GET_ANN_PX_NUM(old_head->unit_px_num);
        uint16_t *ut = rm_ANN_get_px_pt(pkt);
        for(j = 0; j < num; j++)
            ANN_append_px(apt, ut[j]);
        ANN_append_px(apt, px_units[i].px);

        // update head info
        ans_head = (ANN_front_t *)apt;
        ans_head->ttl = old_head->ttl - 1;

        ANN_update_checksum(apt);
    }

    return i;
}

sid_info_t *
rm_GET_search(void *pkt)
{
    if(!pkt)
        return NULL;

    GET_front_t *get_head = (GET_front_t *)pkt;
    sid_table_node_t *sid_node;

    // search sid table
    if((sid_node = sid_store_search(get_head->N_sid, get_head->L_sid)) != NULL)
    {
        return sid_node->sid_info_list;
    }
    else
        return NULL;
}

static void *
rm_GET_pid_start(void *pkt)
{
    GET_front_t *get_pt = (GET_front_t *)pkt;
    uint8_t *pid_start = (uint8_t *)pkt + get_pt->pkg_len - get_pt->PID_num * 4;
    if (CHECK_GET_FLAG_R(get_pt->flag))
        pid_start -= 4;
    return pid_start;
}

static int
rm_GET_get_source_asid(void *pkt, uint32_t *source_asid)
{
    if(!pkt || !source_asid)
        return RM_ERR_NULL_PT_INPUT;

    GET_front_t *get_pt = (GET_front_t *)pkt;
    uint8_t as_trace[32];
    int trace_count;

    if(get_pt->PID_num == 0)
    {
        *source_asid = (uint32_t)my_as_info->aid;
        return RM_PROCESS_SUCCESS;
    }

    trace_count = graph_trace(my_as_info->as_graph, rm_GET_pid_start(pkt), get_pt->PID_num, as_trace);
    if(trace_count <= 0)
        return RM_PROCESS_ERR;

    *source_asid = (uint32_t)as_trace[trace_count - 1];
    return RM_PROCESS_SUCCESS;
}

int
rm_GET_check_filter(void *pkt, sid_info_t *search_res)
{
    if(!pkt || !search_res)
        return RM_ERR_NULL_PT_INPUT;
    
    int i, count, size, j, k, p_check = 0;
    void *s_data;
    strategy_t *gt;
    uint8_t as_trace[32];
    uint16_t px;
    GET_front_t *get_pt = (GET_front_t *)pkt;
    search_px_result_t res_br;
    
    if(get_pt->PID_num != 0)
    {
        px = (uint16_t)((*(uint32_t *)(pkt + (get_pt->pkg_len - 4))) >> 16);
        if(1 != as_store_search_px(px, &res_br))
            return 1;

        if(global_quick_check_strategy(STRATEGY_TRACE_EVERY_PKG))
        {
            p_check = graph_trace(my_as_info->as_graph, rm_GET_pid_start(pkt), get_pt->PID_num, as_trace);
            if(p_check == 0)
            {
                return 1;
            }
            rm_metrics_record_trace(RM_PACKET_GET, as_trace[p_check - 1]);
        }
    }
    else
        px = 0;

    // parse SID's strategies
    if(search_res->strategy_array == NULL || search_res->strategy_array->data == NULL)
        return 0;
    s_data = search_res->strategy_array->data;
    count = search_res->strategy_array->count;
    size = search_res->strategy_array->element_size;
    if(count <= 0)
        return 0;
    for(i = 0; i < count; i++)
    {
        gt = (strategy_t *)(s_data + i * size);
        switch(gt->tag)
        {
            case STRATEGY_SECURITY_LEVEL:
            {
                if(px != 0 && global_quick_check_strategy(STRATEGY_SECURITY_LEVEL_CHECK))
                {
                    uint8_t sid_secu = *(uint8_t *)(gt->value);
                    if(res_br.target_as->security_level < sid_secu)
                        return 1;
                }
                break;
            }
            case STRATEGY_ANNOUNCE_PATH:
            {
                if(px != 0 && global_quick_check_strategy(STRATEGY_ANNOUNCE_PATH_CHECK))
                {
                    if(get_pt->PID_num == 0)
                        break;
                    // get the list of AS being passed
                    if(p_check == 0)
                        p_check = graph_trace(my_as_info->as_graph, rm_GET_pid_start(pkt), get_pt->PID_num, as_trace);
                    if(p_check == 0)
                    {
                        return 1;
                    }
                    else
                    {
                        for(j = 0; j < p_check; j++)
                        {
                            // check the white list
                            for(k = 0; k < gt->len; k++)
                            {
                                if(as_trace[j] == *(uint8_t *)(gt->value + k))
                                    break;
                            }
                            if(k == gt->len)
                            {
                                return 1;
                            }
                        }
                    }
                }
                break;
            }
            default:;
        }
    }

    // check global strategies
    if(px != 0 && global_quick_check_strategy(STRATEGY_AS_SECURITY_CHECK))
    {
        if(res_br.target_as->security_level < my_as_info->security_level)
            return 1;
    }
    
    // not filtered
    return 0;
}

/*
 * 演示开关："攻击者 AS"——查不到 SID 的 GET 不丢弃，转给第一个邻居 AS 的第一个 BR，
 * 用来演示下游能否识别非法 GET。只有设置了 RM_DEMO_FORWARD_UNKNOWN_SID=1 的 RM 才开启，
 * 默认关闭。
 *
 * 原先写死为"aid == 1 的 RM 一律开启"：在 AS1 是正式域的部署里，查不到 SID 的 GET
 * 会被转出本域（计数器还记成转发成功），并且每个这样的 GET 都 rte_malloc 一个
 * sid_info_t、从不释放。
 */
static bool
demo_forward_unknown_sid_enabled(void)
{
    // 只读一次环境变量；多个 lcore 同时首次读取，得到的也是同一个值
    static int cached = -1;

    if (cached < 0) {
        const char *value = getenv("RM_DEMO_FORWARD_UNKNOWN_SID");
        cached = (value != NULL && strcmp(value, "1") == 0) ? 1 : 0;
    }
    return cached == 1;
}

// 演示模式下伪造的转发目标：每个 lcore 一份，只在处理当前这个 GET 期间使用，不需要释放
static RTE_DEFINE_PER_LCORE(sid_info_t, demo_unknown_sid_node);

static sid_info_t *
demo_unknown_sid_target(void)
{
    if (as_store.as_array == NULL || as_store.as_array->count == 0)
        return NULL;
    as_info_t *apt = (as_info_t *)(as_store.as_array->data);
    if (apt->connect_array == NULL || apt->connect_array->count == 0)
        return NULL;
    connect_info_t *cpt = (connect_info_t *)(apt->connect_array->data);
    if (cpt->br_pt == NULL || cpt->px_array == NULL ||
        cpt->px_array->count == 0 || cpt->px_array->data == NULL)
        return NULL;

    sid_info_t *node = &RTE_PER_LCORE(demo_unknown_sid_node);
    node->node_pt = cpt->br_pt;
    node->px = *(uint16_t *)cpt->px_array->data;
    node->as_path_len = 0;
    node->strategy_array = NULL;
    node->next = NULL;
    return node;
}

int
rm_GET_process(void *pkt, sid_info_t **node_pt, struct rte_ipv4_hdr *ip_layer_pt, struct lcore_conf *qconf)
{
    if(!pkt || !node_pt || !ip_layer_pt || !qconf)
        return RM_ERR_NULL_PT_INPUT;

    GET_front_t *get_pt = (GET_front_t *)pkt;
    sid_info_t *search_node;
    uint32_t acl_match_id = 0;
    uint32_t source_asid = 0;

    // checksum check
    if(GET_check_checksum(pkt) == 0)
    {
        RM_LOG_WARN(RM_LOG_MODULE_GET, RM_EVENT_PACKET_BAD_CHECKSUM,
            RM_LOG_PORT_NONE, "GET checksum validation failed");
        return RM_ERR_CHECKSUM_UNPASSED;
    }

    // search SID table
    search_node = rm_GET_search(pkt);

    // unable to find the info
    if(!search_node)
    {
        GET_front_t *get_head = (GET_front_t *)pkt;
        int zero_flag = 1;
        for(int u = 0; u < 16; u++)
        {
            if(get_head->N_sid[u] != 0)
            {
                zero_flag = 0;
                break;
            }
        }
        for(int u = 0; u < 20; u++)
        {
            if(get_head->L_sid[u] != 0)
            {
                zero_flag = 0;
                break;
            }
        }
        if(zero_flag)//全0sid说明proxy正在向RM请求SID列表
        {
            RM_LOG_INFO(RM_LOG_MODULE_GET, RM_EVENT_GET_SID_LIST_RETURNED,
                0, "returning %u SID entries to proxy", g_tsid_table.count);
            struct rte_ether_addr port_mac_addr;
            uint8_t dst_port = 0;//目前RM只有port0
            uint32_t src_ip = my_as_info->port_ip[dst_port];
            uint32_t dst_ip = rte_cpu_to_be_32(ip_layer_pt->src_addr);
            rte_eth_macaddr_get(dst_port, &port_mac_addr);

            int total = g_tsid_table.count;
            // check if there are any SIDs
            if(total == 0)
                return RM_RETURN_SID_LIST;
                
            for(int i = 0; i < total; i = i + 10)//todo:定义一个宏取代10
            {
                void *pkt_buf = rte_malloc(NULL, 512, 0);
                if(!pkt_buf)
                {
                    RM_LOG_ERROR(RM_LOG_MODULE_GET, RM_EVENT_RESOURCE_EXHAUSTED,
                        dst_port, "unable to allocate SID-list packet buffer");
                    return RM_ERR_NO_MEMORY;
                }
                
                int num_to_copy = 10;
                if(i + 10 > total)
                {
                    num_to_copy = total - i;
                }
                
                uint8_t *con_buf = rte_malloc(NULL, num_to_copy * SID_LEN, 0);
                if(!con_buf)
                {
                    rte_free(pkt_buf);
                    RM_LOG_ERROR(RM_LOG_MODULE_GET, RM_EVENT_RESOURCE_EXHAUSTED,
                        dst_port, "unable to allocate SID-list payload buffer");
                    return RM_ERR_NO_MEMORY;
                }

                for(int j = 0; j < num_to_copy; j++)
                {
                    memcpy(con_buf + j * SID_LEN, g_tsid_table.sid_list[i + j].tsid, SID_LEN);
                }

                CONTROL_init(pkt_buf, CONTROL_SID_LIST, num_to_copy * SID_LEN, con_buf);
                CONTROL_update_checksum(pkt_buf);
                rm_send_new_ip_pkt(qconf, pkt_buf, CoLoR_get_pkg_length(pkt_buf),
					src_ip, dst_ip, port_mac_addr.addr_bytes, dst_port);

                rte_free(pkt_buf);
                rte_free(con_buf);
            }
            return RM_RETURN_SID_LIST;

        }

        // 查不到 SID 一律丢弃；只有显式打开演示开关时才转给第一个 BR（见 demo_forward_unknown_sid_enabled）
        if(!demo_forward_unknown_sid_enabled() ||
            (search_node = demo_unknown_sid_target()) == NULL)
        {
            RM_LOG_WARN(RM_LOG_MODULE_GET, RM_EVENT_GET_SID_NOT_FOUND,
                RM_LOG_PORT_NONE, "GET SID was not found");
            return RM_GET_SID_UNFOUND;
        }
        RM_LOG_WARN(RM_LOG_MODULE_GET, RM_EVENT_GET_SID_NOT_FOUND,
            RM_LOG_PORT_NONE,
            "GET SID was not found, forwarded to first BR (RM_DEMO_FORWARD_UNKNOWN_SID=1)");
    }

    uint8_t acl_sid[RM_ACL_SID_LEN];
    rte_memcpy(acl_sid, get_pt->N_sid, COLOR_N_SID_LEN);
    rte_memcpy(acl_sid + COLOR_N_SID_LEN, get_pt->L_sid, COLOR_L_SID_LEN);
    if (rm_GET_get_source_asid(pkt, &source_asid) != RM_PROCESS_SUCCESS)
    {
        RM_LOG_WARN(RM_LOG_MODULE_SECURITY, RM_EVENT_GET_ACL_DENIED,
            RM_LOG_PORT_NONE, "GET denied: unable to identify source AS");
        return RM_GET_ACL_DENIED;
    }
    if (rm_acl_check_get(acl_sid, source_asid, get_pt->nid, &acl_match_id) == 0)
    {
        RM_LOG_WARN(RM_LOG_MODULE_SECURITY, RM_EVENT_GET_ACL_DENIED,
            RM_LOG_PORT_NONE,
            "GET denied by ACL rule %u, source_asid=0x%08x",
            acl_match_id, source_asid);
        return RM_GET_ACL_DENIED;
    }
    
    // ### check and filter GET
    if(rm_GET_check_filter(pkt, search_node) == 1)
    {
        RM_LOG_WARN(RM_LOG_MODULE_GET, RM_EVENT_GET_POLICY_DENIED,
            RM_LOG_PORT_NONE, "GET denied by forwarding policy");
        return RM_GET_POLICY_DENIED;
    }


    // update pkg info
    // 提供者在本域内，不计算PID
    if(search_node->px == 0)
    {
        get_pt->ttl -= 1;
        GET_update_checksum(pkt);
    }
    // 提供者在其他域，计算PID
    else
    {
        get_pt->ttl -= 1;
        // calculate PID and append
        GET_append_pid(pkt, GET_calculate_PID(pkt, my_as_info->sk, search_node->px));
        GET_update_checksum(pkt);
    }
    *node_pt = search_node;
    return RM_PROCESS_SUCCESS;

}

int
rm_CONTROL_process(void *pkt, int from_port)
{
    if(!pkt)
        return RM_ERR_NULL_PT_INPUT;
    else if(CONTROL_check_checksum(pkt) == 0)
    {
        RM_LOG_WARN(RM_LOG_MODULE_CONTROL, RM_EVENT_PACKET_BAD_CHECKSUM,
            from_port, "CONTROL checksum validation failed");
        return RM_ERR_CHECKSUM_UNPASSED;
    }

    int as_num, br_num, px_num, i, j, br_buf_count;
    as_info_t *as_mid;
    connect_info_t *con_mid;
    CONTROL_front_t *con_pt = (CONTROL_front_t *)pkt;
    br_info_t br_mid;
    
    void *pt = pkt + con_pt->header_len;
    void *ept = pt + con_pt->len;
    parse_br_buf_t br_buf[128];
    switch(con_pt->tag) // use flag
    {
        case CONTROL_CONFIG_RM:
        {
            clear_syn_rm_config();
            
            // base as-info
//            uint8_t *nid = pt;
            uint32_t asid = *(uint32_t *)(pt + 16);
            uint8_t security_level = *(uint8_t *)(pt + 20);
            uint32_t sk = *(uint32_t *)(pt + 21);
            global_set_as_info(pt, asid, security_level, sk); // sk

            // br_list
            br_num = *(uint8_t *)(pt + 25); // br count
            pt += 26;
            for(br_buf_count = 0; br_buf_count < br_num; br_buf_count++)
            {
                rte_memcpy(br_mid.nid, pt + 1, 16); // br: nid
                br_mid.ip_addr = *(uint32_t *)(pt + 17); // br: ip
                br_mid.port = from_port;
                br_store_insert(&br_mid);
                br_buf[br_buf_count].br_n = *(uint8_t *)pt; // br: id
                br_buf[br_buf_count].br_info_pt = br_store_search(br_mid.nid);
                pt += 21;
            }

            as_num = *(uint8_t *)pt++;
            for(i = 0; i < as_num; i++) {
                uint32_t asid = *(uint32_t *)pt;
                uint8_t security_level = *(uint8_t *)(pt + 4);
                as_mid = as_info_create(asid, security_level);
                as_store_insert(as_mid);
                pt += 5;
            }

            px_num = *(uint8_t *)pt++;
            for (i = 0; i < px_num; i++) {
                uint32_t asid = *(uint32_t *)pt;
                uint8_t br_n = *(uint8_t *)(pt + 4);
//                uint8_t prefix_length = *(uint8_t *)(pt + 5); // TODO: currently PX is 16-bit 
                uint32_t px = *(uint32_t *)(pt + 6);
                as_mid = as_store_search_aid(asid);
                for (j = 0; j < br_buf_count; j++) { // find br
                    if (br_buf[j].br_n == br_n) {
                        break;
                    }
                }
                if(j == br_buf_count) {
                    RM_LOG_WARN(RM_LOG_MODULE_CONTROL, RM_EVENT_PACKET_MALFORMED,
                        from_port, "RM configuration references unknown BR %u", br_n);
                    return RM_PARSE_ERR_FORMAT;
                }
                con_mid = connect_info_create(br_buf[j].br_info_pt);
                connect_info_insert(con_mid, (uint16_t)(px >> 16));  // TODO: currently PX is 16-bit 
                as_info_insert(as_mid, con_mid);
                pt += 10;
            }

            return CONTROL_CONFIG_RM;
        }
        case CONTROL_REG_PROXY:
        {
            proxy_info_t ans;
            ans.port = from_port;
            ans.ip_addr = *(uint32_t *)pt;
            rte_memcpy(ans.nid, pt + 4, 16);
            int insert_result = proxy_store_insert(&ans);
            if (insert_result == 1) {
                rm_metrics_add(RM_METRIC_PROXY_REGISTERED, 1);
                RM_LOG_INFO(RM_LOG_MODULE_CONTROL, RM_EVENT_PROXY_REGISTERED,
                    ans.port, "proxy registered, ip=0x%08x", ans.ip_addr);
            }
            return CONTROL_REG_PROXY;
        }
        case CONTROL_GLOBAL_TOPOLOGY_INFO:
        {
            uint8_t aid1, aid2;
            uint16_t px;
            graph_clear(my_as_info->as_graph);
            my_as_info->as_graph = graph_create(my_as_info->aid);
            for(; pt != ept; )
            {
                aid1 = *(uint8_t *)(pt);
                aid2 = *(uint8_t *)(pt + 1);
                px = *(uint16_t *)(pt + 2);
                graph_add(my_as_info->as_graph, aid1, aid2, px);
                pt += 4;
            }
            return CONTROL_GLOBAL_TOPOLOGY_INFO;
        }
        case CONTROL_STRATEGY_GLOBAL: // t l (st sl sv)
        {
            uint8_t s_tag;
            s_tag = *(uint8_t *)pt;
            switch(s_tag)
            {
                case STRATEGY_SECURITY_LEVEL_CHECK:
                case STRATEGY_ANNOUNCE_PATH_CHECK:
                case STRATEGY_DATA_LEAKAGE_CHECK:
                case STRATEGY_AS_SECURITY_CHECK:
                case STRATEGY_N_SID_FIELD_OMIT:
                case STRATEGY_L_SID_FIELD_OMIT:
                case STRATEGY_NID_FIELD_OMIT:
                case STRATEGY_TRACE_EVERY_PKG:
                {
                    // set quick mark
                    if(1 == *(uint8_t *)(pt + 3))
                    {
                        global_quick_set_strategy(s_tag, true);
                    }
                    else
                    {
                        global_quick_set_strategy(s_tag, false);
                    }
                    // add the strategy
                    global_add_strategy(s_tag, *(uint16_t *)(pt + 1), (pt + 3));
                    break;
                }
                default:;
            }
            return CONTROL_STRATEGY_GLOBAL;
        }
        case STAT_ANN_INFO:
            return STAT_ANN_INFO;
        case STAT_GET_INFO:
            return STAT_GET_INFO;
        case STAT_PROXY_INFO:
            return STAT_PROXY_INFO;
        default:
            return CONTROL_UNKNOWN_TAG;
    }
}

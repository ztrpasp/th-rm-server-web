#include "tables/rm_as_table.h"
#include "tables/rm_tables.h"


// functions of AS data
int
matchAID(void *buf1, void *buf2)
{
    if(!buf1 || !buf2)
        return -1;
    uint16_t aid = *((uint16_t *)buf2);
    if(((as_info_t *)buf1)->aid == aid)
        return 1;
    else
        return 0;
}

int
matchPX(void *buf1, void *buf2)
{
    if(!buf1 || !buf2)
        return -1;
    if((*(uint16_t *)buf1) == (*(uint16_t *)buf2))
        return 1;
    else
        return 0;
}

connect_info_t *
connect_info_create(br_info_t *br_pt)
{
    if(!br_pt)
        return NULL;
    connect_info_t *ans = (connect_info_t *)rte_malloc(NULL, sizeof(connect_info_t), 0);
    ans->px_array = rte_array_create(4, sizeof(uint16_t));
    ans->br_pt = br_pt;
    return ans;
}

int
connect_info_insert(connect_info_t *ans, uint16_t px)
{
    if(!ans)
        return -1;
    return rte_array_add(ans->px_array, &px);
}

as_info_t *
as_info_create(uint16_t aid, uint8_t secu)
{
    as_info_t *ans = (as_info_t *)rte_malloc(NULL, sizeof(as_info_t), 0);
    ans->connect_array = rte_array_create(8, sizeof(connect_info_t));
    ans->aid = aid;
    ans->security_level = secu;
    return ans;
}

int
as_info_insert(as_info_t *target, connect_info_t *info)
{
    if(!target || !info)
        return -1;
    if(1 == rte_array_add(target->connect_array, info))
    {
        rte_free(info); // to prevent mem leak
        return 1;
    }
    else
        return -1;
}

static int //todo
little_destroy(void *ans)
{
    connect_info_t *x = (connect_info_t *)ans;
    rte_array_destroy(x->px_array);
    return 1;
}

int
as_info_destroy(as_info_t *ans)
{
    if(!ans)
        return -1;
    // loop and destroy every connect info
    rte_array_loop_function(ans->connect_array, little_destroy);
    rte_array_destroy(ans->connect_array);
    return 1;
}

int
as_store_insert(as_info_t *target)
{
    if(!target)
        return -1;
    if(1 == rte_array_add(as_store.as_array, target))
    {
        rte_free(target); // to prevent mem leak
        return 1;
    }
    else
        return -1;
}

as_info_t *
as_store_search_aid(uint16_t key)
{
    int i, c = as_store.as_array->count, s = sizeof(as_info_t);
    as_info_t *npt;
    void *start = as_store.as_array->data;
    for(i = 0; i < c; i++)
    {
        npt = (as_info_t *)(start + i * s);
        if(npt->aid == key)
            return npt;
    }
    return NULL;
}

int
as_store_search_px(uint16_t px, search_px_result_t *res)
{
    as_info_t *as_pt;
    connect_info_t *con_pt;
    void *as_pt_start = as_store.as_array->data, *con_pt_start, *px_start;
    int i, j, k,
        as_n = as_store.as_array->count, as_s = sizeof(as_info_t),
        con_n, con_s = sizeof(connect_info_t),
        px_n;

    if(!res)
        return -1;

    for(i = 0; i < as_n; i++)
    {
        as_pt = (as_info_t *)(as_pt_start + as_s * i);
        con_pt_start = as_pt->connect_array->data;
        con_n = as_pt->connect_array->count;
        for(j = 0; j < con_n; j++)
        {
            con_pt = (connect_info_t *)(con_pt_start + con_s * j);
            px_start = con_pt->px_array->data;
            px_n = con_pt->px_array->count;
            for(k = 0; k < px_n; k++)
            {
                if(*(uint16_t *)(px_start + k * 2) == px)
                {
                    res->target_as = as_pt;
                    res->target_br = con_pt->br_pt;
                    return 1;
                }
            }
        }
    }
    res->target_as = NULL;
    res->target_br = NULL;
    return 0;
}

/** 将所有AS-PX-IP关系拷贝到 \b buf 中
 * 
 * @param buf 指向返回AS-PX-IP的空间
 * @returns AS-PX-IP数量
 */
int
as_store_ret_as_px_ip_list(void *buf) //modified
{
    if(!buf)
        return -1;
    const int as_px_ip_len = 13;
    uint8_t *pt = buf;
    as_info_t *as_pt;
    connect_info_t *con_pt;
    uint8_t *as_pt_start = as_store.as_array->data, *con_pt_start, *px_start;
    int i, j, k,
        as_n = as_store.as_array->count, as_s = sizeof(as_info_t),
        con_n, con_s = sizeof(connect_info_t),
        px_n;

    for(i = 0; i < as_n; i++)
    {
        as_pt = (as_info_t *)(as_pt_start + as_s * i);
        con_pt_start = as_pt->connect_array->data;
        con_n = as_pt->connect_array->count;
        for(j = 0; j < con_n; j++)
        {
            con_pt = (connect_info_t *)(con_pt_start + con_s * j);
            px_start = con_pt->px_array->data;
            px_n = con_pt->px_array->count;
            for(k = 0; k < px_n; k++)
            {
                *(uint32_t *)pt = as_pt->aid;
                *(uint8_t *)(pt + 4) = 16; // TODO: currently prefix length is 16-bit 
                *(uint32_t *)(pt + 5) = (uint32_t)(*(uint16_t *)(px_start + k * 2)) << 16;
                *(uint32_t *)(pt + 9) = con_pt->br_pt->ip_addr;
                pt += as_px_ip_len;
            }
        }
    }
    return (pt - (uint8_t *)buf)/as_px_ip_len;
}

int
show_single_px(void *px)
{
    if(!px)
        return 0;
    printf("%x ", *(uint16_t *)px);
    return 1;
}

int
show_single_con(void *con)
{
    if(!con)
        return 0;
    connect_info_t *ct = (connect_info_t *)con;
    printf("    br :");
    show_data(ct->br_pt->nid, 16, false);
    printf("\n        Port: %d\n       IP: %x\n       PX: ", ct->br_pt->port, ct->br_pt->ip_addr);
    rte_array_loop_function(ct->px_array, show_single_px);
    printf("\n");
    return 1;
}

int
show_single_as(void *as)
{
    if(!as)
        return 0;
    as_info_t *at = (as_info_t *)as;
    printf("AID: %d\n", at->aid);
    rte_array_loop_function(at->connect_array, show_single_con);
    return 1;
}

void
as_store_show(void)
{
    rte_array_loop_function(as_store.as_array, show_single_as);
}
#include "tables/rm_br_table.h"
#include "tables/rm_tables.h"

int
br_node_cmp(const void *p1, const void *p2)
{
    uint8_t *nid1 = ((br_info_t *)p1)->nid, *nid2 = ((br_info_t *)p2)->nid, i;
    
    for(i = 0; i < 16; i++)
    {
        if(nid1[i] > nid2[i])
            return 1;
        else if(nid1[i] < nid2[i])
            return -1;
    }
    return 0;
}

void *
br_node_dup(void *p)
{
    void *ans;
    ans = (void *)rte_malloc(NULL, sizeof(br_info_t), 0);
    rte_memcpy(ans, p, sizeof(br_info_t));
    return ans;
}

int
br_store_insert(br_info_t *target)
{
    if(target == NULL)
        return -1;
    if(0 == jsw_rbinsert(br_store.store_data, (void *)target))
        return -1;
    else
        return 1;
}

br_info_t *
br_store_search(uint8_t *nid)
{
    if(!nid)
        return NULL;
    br_info_t mid;
    rte_memcpy(mid.nid, nid, 16);
    return (br_info_t *)jsw_rbfind(br_store.store_data, &mid);
}

int
br_store_erase(uint8_t *nid)
{
    if(!nid)
        return -1;
    br_info_t mid, *res;
    rte_memcpy(mid.nid, nid, 16);
    if((res = jsw_rbfind(br_store.store_data, &mid)) != NULL)
    {
        if(0 == jsw_rberase(br_store.store_data, &mid))
            return 1;
        else
            return -1;
    }
    else
        return -1;
}

int
br_store_ret_node_list(br_info_t *br_list[])
{
    int i;
    br_info_t *br_pt;
    jsw_rbtrav_t *travel = jsw_rbtnew();
    br_pt = (br_info_t *)jsw_rbtfirst(travel, br_store.store_data);
    for(i = 0;;)
    {
        if(br_pt == NULL)
            break;
        br_list[i++] = br_pt;
        br_pt = (br_info_t *)jsw_rbtnext(travel);
    }
    jsw_rbtdelete(travel);
    return i;
}
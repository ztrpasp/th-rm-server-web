#include "tables/rm_proxy_table.h"
#include "tables/rm_tables.h"



int
proxy_node_cmp(const void *p1, const void *p2)
{
    uint8_t *nid1 = ((proxy_info_t *)p1)->nid, *nid2 = ((proxy_info_t *)p2)->nid, i;
    
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
proxy_node_dup(void *p)
{
    void *ans;
    ans = (void *)rte_malloc(NULL, sizeof(proxy_info_t), 0);
    rte_memcpy(ans, p, sizeof(proxy_info_t));
    return ans;
}
// functions of proxy data
int
proxy_store_insert(proxy_info_t *target)
{
    if(target == NULL)
        return -1;
    if(0 == jsw_rbinsert(proxy_store.store_data, (void *)target))
        return -1;
    else
        return 1;
}

proxy_info_t *
proxy_store_search(uint8_t *nid)
{
    if(!nid)
        return NULL;
    proxy_info_t mid;
    rte_memcpy(mid.nid, nid, 16);
    return (proxy_info_t *)jsw_rbfind(proxy_store.store_data, &mid);
}

int
proxy_store_erase(uint8_t *nid)
{
    if(!nid)
        return -1;
    proxy_info_t mid, *res;
    rte_memcpy(mid.nid, nid, 16);
    if((res = jsw_rbfind(proxy_store.store_data, &mid)) != NULL)
    {
        if(0 == jsw_rberase(proxy_store.store_data, &mid))
            return 1;
        else
            return -1;
    }
    else
        return -1;
}

/** 获取所有的代理节点指针。
 * 
 * @param proxy_list 存储代理节点指针的数组，调用者保证容量足够
 * @return 代理节点的数量
 */
int
proxy_store_ret_node_list(proxy_info_t *proxy_list[])
{
    int i;
    proxy_info_t *proxy_pt;
    jsw_rbtrav_t *travel = jsw_rbtnew();
    proxy_pt = (proxy_info_t *)jsw_rbtfirst(travel, proxy_store.store_data);
    for(i = 0;;)
    {
        if(proxy_pt == NULL)
            break;
        proxy_list[i++] = proxy_pt;
        proxy_pt = (proxy_info_t *)jsw_rbtnext(travel);//这有个bug，原来是把proxy_info_t写成了br_info_t
    }
    jsw_rbtdelete(travel);
    return i;
}
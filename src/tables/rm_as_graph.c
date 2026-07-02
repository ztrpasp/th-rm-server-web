#include "commandline/cli.h"
#include "tables/rm_tables.h"

int
graph_node_cmp(const void *p1, const void *p2)
{
    return ((graph_px_t *)p1)->px - ((graph_px_t *)p2)->px;
}

void *
graph_node_dup(void *p)
{
    void *ans;
    ans = (void *)malloc(sizeof(graph_px_t));
    memcpy(ans, p, sizeof(graph_px_t));
    return ans;
}

void
graph_node_rel(void *p)
{
    free(p);
}

graph_t *
graph_create(uint8_t myaid)
{
    graph_t *ans = (graph_t *)malloc(sizeof(graph_t));
    (ans->as_data)[0] = (graph_as_t *)malloc(sizeof(graph_as_t));
    (ans->as_data)[0]->aid = myaid;
    (ans->as_data)[0]->px_data = jsw_rbnew(graph_node_cmp, graph_node_dup, graph_node_rel);
    (ans->as_data)[1] = NULL;
    return ans;
}

void
graph_show(graph_t *tar)
{
    if(tar == NULL){
        cli_print("[#] AS Graph is NULL?????\n");
        return;
    }

    int i;
    graph_as_t *apt;
    graph_px_t *gpt;
    jsw_rbtrav_t *travel;

    for(i = 0; i < 256; i++)
    {
        apt = (tar->as_data)[i];
        if(apt == NULL)
            break;
        travel = jsw_rbtnew();
        gpt = (graph_px_t *)jsw_rbtfirst(travel, apt->px_data);
        for(;;)
        {
            if(gpt == NULL)
                break;
            cli_print("PX 0x%x AID1 %d AID2 %d\n", gpt->px, apt->aid, gpt->apt->aid);
            gpt = (graph_px_t *)jsw_rbtnext(travel);
        }
        jsw_rbtdelete(travel);
    }
}

int
graph_trace(graph_t *tar, void *pid_start, int pid_count, uint8_t as_ans[])
{
    if(!tar || !pid_start || pid_count <= 0 || !as_ans)
        return -1;
    
    int i, ans_count;
    void *ppt;
    graph_as_t *apt;
    graph_px_t *check, buf;
    uint8_t now_aid;
    uint16_t npx;

    for(ppt = pid_start + ((pid_count - 1) * 4), apt = (tar->as_data)[0], ans_count = 0; ; )
    {
        npx = (*(uint32_t *)ppt) >> 16;
        // search the px_tree
        now_aid = apt->aid;
        buf.px = npx;
        if((check = (graph_px_t *)jsw_rbfind(apt->px_data, &buf)) != NULL)
        {
            apt = check->apt;
            as_ans[ans_count++] = apt->aid;
            if(ppt == pid_start)
                break;
            else
                ppt -= 4;
            
        }
        else
            return 0;
    }

    return ans_count;
}

int
graph_trace_px(graph_t *tar, void *px_start, int px_count, uint8_t as_ans[])
{
    if(!tar || !px_start || px_count <= 0 || !as_ans)
        return -1;
    
    int i, ans_count;
    void *ppt;
    graph_as_t *apt;
    graph_px_t *check, buf;
    uint8_t now_aid;
    uint16_t npx;

    for(ppt = px_start + ((px_count - 1) * 2), apt = (tar->as_data)[0], ans_count = 0; ; )
    {
        npx = *(uint16_t *)ppt;
        cli_print("trace PX %x\n", npx);
        // search the px_tree
        now_aid = apt->aid;
        buf.px = npx;
        if((check = (graph_px_t *)jsw_rbfind(apt->px_data, &buf)) != NULL)
        {
            apt = check->apt;
            as_ans[ans_count++] = apt->aid;
            if(ppt == px_start)
                break;
            else
                ppt -= 2;
            
        }
        else
            return 0;
    }

    return ans_count;
}

int
graph_add(graph_t *tar, uint8_t aid1, uint8_t aid2, uint16_t npx)
{
    if(!tar || aid1 == aid2)
        return -1;
    
    uint16_t i;
    graph_as_t *mid, *at1, *at2;
    graph_px_t buf;

    for(i = 0, at1 = NULL, at2 = NULL; i < 256; i++)
    {
        mid = (tar->as_data)[i];
        if(mid == NULL)
        {
            if(at1 == NULL)
            {
                at1 = (graph_as_t *)malloc(sizeof(graph_as_t));
                at1->aid = aid1;
                at1->px_data = jsw_rbnew(graph_node_cmp, graph_node_dup, graph_node_rel);
                (tar->as_data)[i++] = at1;
                (tar->as_data)[i] = NULL;
            }
            if(at2 == NULL)
            {
                at2 = (graph_as_t *)malloc(sizeof(graph_as_t));
                at2->aid = aid2;
                at2->px_data = jsw_rbnew(graph_node_cmp, graph_node_dup, graph_node_rel);
                (tar->as_data)[i++] = at2;
                (tar->as_data)[i] = NULL;
            }
            break;
        }
        else if(mid->aid == aid1)
            at1 = mid;
        else if(mid->aid == aid2)
            at2 = mid;
    }

    buf.px = npx;
    if(jsw_rbfind(at1->px_data, &buf) == NULL)
    {
        buf.apt = at2;
        if(0 == jsw_rbinsert(at1->px_data, (void *)&buf))
            return -1;
        buf.apt = at1;
        if(0 == jsw_rbinsert(at2->px_data, (void *)&buf))
            return -1;
        else
            return 1;
    }
    else
        return 0;
}

void
graph_clear(graph_t *tar)
{
    int i;
    graph_as_t *as_node;
    graph_px_t *ptt;
    jsw_rbtrav_t *travel;
    
    for(i = 0; ; i++)
    {
        as_node = tar->as_data[i];
        if(as_node == NULL)
            break;
        travel = jsw_rbtnew();
        ptt = (graph_px_t *)jsw_rbtfirst(travel, as_node->px_data);
        for(;;)
        {
            if(ptt == NULL)
                break;
            // delete from opposite as
            jsw_rberase(ptt->apt->px_data, ptt);
            ptt = (graph_px_t *)jsw_rbtnext(travel);
        }
        jsw_rbtdelete(travel);
        jsw_rbdelete(as_node->px_data);
        free(as_node);
    }
    free(tar);
}

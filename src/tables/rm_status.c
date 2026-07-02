#include "commandline/cli.h"
#include "tables/rm_status.h"

rm_status_t *status_create()
{
    int i;
    rm_status_t *ans = (rm_status_t *)rte_malloc(NULL, sizeof(rm_status_t), 0);
    
    ans->ANN_stat.from_inside_count = 15;
    ans->ANN_stat.from_outside_count = 6;
    ans->ANN_stat.outbound_count = 10;
    ans->ANN_stat.filtered_count = 5;
    for(i = 0; i < 256; i++)
        ans->ANN_stat.trace_res[i] = 0;
        
    ans->GET_stat.from_inside_count = 10;
    ans->GET_stat.from_outside_count = 4;
    ans->GET_stat.to_inside_count = 12;
    ans->GET_stat.to_outside_count = 2;
    ans->GET_stat.filtered_count = 0;
    for(i = 0; i < 256; i++)
        ans->GET_stat.trace_res[i] = 0;

    ans->GET_stat.trace_res[i] = 1;
    ans->GET_stat.trace_res[i] = 2;
    ans->GET_stat.trace_res[i] = 1;

    ans->proxy_stat.register_count = 2;

    return ans;
}

void status_show(rm_status_t *st)
{
    int i;
    cli_print("Status : ANN\nfrom_inside: %d\nfrom_outside: %d\noutbound: %d\nfiltered: %d\nTrace result:\n",
        st->ANN_stat.from_inside_count,
        st->ANN_stat.from_outside_count,
        st->ANN_stat.outbound_count,
        st->ANN_stat.filtered_count);
    for(i = 0; i < 256; i++)
    {
        if(st->ANN_stat.trace_res[i] != 0)
        {
            cli_print("AS:\t%d\t--%d\n", i, st->ANN_stat.trace_res[i]);
        }
    }
    
    cli_print("Status : GET\nfrom_inside: %d\nfrom_outside: %d\nto_inside: %d\nto_outside: %d\nfiltered: %d\nTrace result:\n",
        st->GET_stat.from_inside_count,
        st->GET_stat.from_outside_count,
        st->GET_stat.to_inside_count,
        st->GET_stat.to_outside_count,
        st->GET_stat.filtered_count);
    for(i = 0; i < 256; i++)
    {
        if(st->GET_stat.trace_res[i] != 0)
        {
            cli_print("AS:\t%d\t--%d\n", i, st->GET_stat.trace_res[i]);
        }
    }

    cli_print("Status : Proxy\nreg_count: %d\n",
        st->proxy_stat.register_count);
}

int status_construct(rm_status_t *st, void *buf, uint8_t type)
{
    int size = 0;
    switch(type)
    {
        case ANN_STAT_TYPE:
        {
            *(uint32_t *)buf = st->ANN_stat.from_inside_count;
            *(uint32_t *)(buf + 4) = st->ANN_stat.from_outside_count;
            *(uint32_t *)(buf + 8) = st->ANN_stat.outbound_count;
            *(uint32_t *)(buf + 12) = st->ANN_stat.filtered_count;
            size = 16;
            break;
        }
        case GET_STAT_TYPE:
        {
            *(uint32_t *)(buf) = st->GET_stat.from_inside_count;
            *(uint32_t *)(buf + 4) = st->GET_stat.from_outside_count;
            *(uint32_t *)(buf + 8) = st->GET_stat.to_inside_count;
            *(uint32_t *)(buf + 12) = st->GET_stat.to_outside_count;
            *(uint32_t *)(buf + 16) = st->GET_stat.filtered_count;
            size = 20;
            break;
        }
        case PROXY_STAT_TYPE:
        {
            *(uint32_t *)(buf) = st->proxy_stat.register_count;
            size = 4;
            break;
        }
        default:
            size = -1;
    }
    return size;
}
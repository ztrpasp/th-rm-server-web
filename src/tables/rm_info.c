#include "tables/rm_info.h"
#include "tables/rm_tables.h"
#include "vendor/print.h"
#include "commandline/cli.h"

void
node_rel(void *p)
{
    rte_free(p);
}

void
show_data(uint8_t *data, int count, bool is_mac)
{
    int i;
    if(!data)
        return;
    
    if(is_mac) {
        // MAC地址格式: 02:00:00:00:00:01
        for(i = 0; i < count; i++) {
            cli_print("%02x%s", data[i], (i < count-1) ? ":" : "");
        }
    } else if(count == 4) {
        // IPv4地址格式: 192.168.1.1
        cli_print("%d.%d.%d.%d", data[0], data[1], data[2], data[3]);
    } else {
        // 其他数据保持原样
        bool flag = false;
        for(i = 0; i < count; i++) {
            if(flag) {
                cli_print("%02x", data[i]);
            } else if(data[i] != 0) {
                cli_print("%x", data[i]);
                flag = true;
            }
        }
    }
}





// the function to initialize data structure
int
rm_data_initialize()
{
    int i;
    cli_print(BLUE "[>] Start initializing...\n");
    cli_print(RESET "");

    // initialize table of basic 
    br_store.br_count = 0;
    br_store.store_data = jsw_rbnew(br_node_cmp, br_node_dup, node_rel);
    if(br_store.store_data != NULL){cli_print(GREEN "[+] br_store initialized successfully\n");
        cli_print(RESET "");
    }
    else{cli_print(RED "[!] br_store initialized unsuccessfully\n");
        cli_print(RESET "");
    }

    proxy_store.proxy_count = 0;
    proxy_store.store_data = jsw_rbnew(proxy_node_cmp, proxy_node_dup, node_rel);
    if(proxy_store.store_data!= NULL){cli_print(GREEN "[+] proxy_store initialized successfully\n");
        cli_print(RESET "");
    }
    else{cli_print(RED "[!] proxy_store initialized unsuccessfully\n");
        cli_print(RESET "");
    }
    
    // initialize basic info
    my_as_info = (my_basic_info_t *)rte_malloc(NULL, sizeof(my_basic_info_t), 0);
    my_as_info->quick_mark = 0;
    my_as_info->global_strategy_store = rte_array_create(4, sizeof(strategy_t));
    for(i = 0; i < 32; i++)
    {
        my_as_info->port_ip[i] = 0; // 0 for not open
    }
    my_as_info->as_graph = NULL;
    my_as_info->my_status = status_create();
    
    // initialize table of as info
    as_store.as_array = rte_array_create(16, sizeof(as_info_t));

    // initialize table of sid info
    sid_store.N_hash_table = (cuckoo_hash_t *)rte_malloc(NULL, sizeof(cuckoo_hash_t), 0);
    sid_store.L_hash_table = (cuckoo_hash_t *)rte_malloc(NULL, sizeof(cuckoo_hash_t), 0);
    //sid_store.T_hash_table = (cuckoo_hash_t *)rte_malloc(NULL, sizeof(cuckoo_hash_t), 0);
    cuckoo_hash_init(sid_store.N_hash_table, N_HASH_POEWER);
    cuckoo_hash_init(sid_store.L_hash_table, L_HASH_POEWER);
    //cuckoo_hash_init(sid_store.T_hash_table, T_HASH_POEWER);

    if (0 == rm_acl_table_init()) {
        cli_print(GREEN "[+] acl_store initialized successfully\n");
        cli_print(RESET "");
    } else {
        cli_print(RED "[!] acl_store initialized unsuccessfully\n");
        cli_print(RESET "");
    }

    cli_print(GREEN "[+] Initialize successfully! ^o^ ^o^ ^o^\n");
    cli_print(RESET "");
}

// global info setting
int
global_set_as_info(uint8_t *nid, int aid, int sl, uint32_t sk)
{
    rte_memcpy(my_as_info->nid, nid, 16);
    my_as_info->aid = aid;
    my_as_info->security_level = sl;
    my_as_info->sk = sk;
    if(my_as_info->as_graph != NULL)
        graph_clear(my_as_info->as_graph);
    my_as_info->as_graph = graph_create(aid);
}

int
global_add_strategy(uint8_t t, uint8_t l, uint8_t *v)
{
    strategy_t *ans;

    ans = global_search_strategy(t);
    if(ans != NULL)
    {
        rte_memcpy(ans->value, v, l);
        return 0;
    }

    ans = (strategy_t *)rte_malloc(NULL, sizeof(strategy_t), 0);
    ans->tag = t;
    ans->len = l;
    ans->value = rte_malloc(NULL, l, 0);
    rte_memcpy(ans->value, v, l);

    rte_array_add(my_as_info->global_strategy_store, ans);
    return 0;
}

strategy_t *
global_search_strategy(uint8_t t)
{
    strategy_t *spt;
    void *start = my_as_info->global_strategy_store->data;
    int i, limi = my_as_info->global_strategy_store->count, 
            s = my_as_info->global_strategy_store->element_size;
    for(i = 0; i < limi; i++)
    {
        spt = (strategy_t *)(start + (i * s));
        if(spt->tag == t)
            return spt;
    }
    return NULL;
}

void
global_quick_set_strategy(uint8_t t, bool set)
{
    if(set)
        my_as_info->quick_mark = ((my_as_info->quick_mark) | (0x1 << t));
    else
        my_as_info->quick_mark = ((my_as_info->quick_mark) & (~(0x1 << t)));
}

bool
global_quick_check_strategy(uint8_t t)
{
    return (((my_as_info->quick_mark) >> t) & 0x1) == 0x1;
}

void
clear_syn_rm_config()
{
    // clear br info
    br_store.br_count = 0;
    jsw_rbdelete (br_store.store_data);
    br_store.store_data = jsw_rbnew(br_node_cmp, br_node_dup, node_rel);

    // clear as-br-px info
    int i, c = as_store.as_array->count, s = sizeof(as_info_t);
    as_info_t *apt;
    void *start = as_store.as_array->data;
    for(i = 0; i < c; i++)
    {
        apt = (as_info_t *)(start + i * s);
        as_info_destroy(apt);
    }
    rte_array_destroy(as_store.as_array);
    as_store.as_array = rte_array_create(16, sizeof(as_info_t));
}

/* 输出本域AS ID，Security Level和策略标记 */
void basic_info_show()
{
    cli_print("AID : %d\nSecurity : %d\n", my_as_info->aid, my_as_info->security_level);
    cli_print("Mark: %x\n", my_as_info->quick_mark);
}

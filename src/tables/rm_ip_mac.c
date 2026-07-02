#include "commandline/cli.h"
#include "tables/rm_ip_mac.h"
#include "tables/rm_tables.h"
#include "vendor/print.h"

int
ip_mac_store_initialize()
{
    ip_mac_store = (cuckoo_hash_t *)rte_malloc(NULL, sizeof(cuckoo_hash_t), 0);
    if(cuckoo_hash_init(ip_mac_store, 1)){
        cli_print(GREEN "[+] ip_mac_store initialized successfully\n");
        cli_print(RESET "");
        return 0;
    }
    else{
        cli_print("[!] ip_mac_store initialized unsuccessfully\n");
        cli_print(RESET "");
        return -1; 
    }
}

int
ip_mac_store_insert(uint32_t ip, uint8_t *mac_addr_bytes)
{
    if(!mac_addr_bytes)
        return -1;

    int i;
    struct cuckoo_hash_item *check_pt;
    ip_mac_info_t *new_info;

    uint32_t *key  = rte_malloc(NULL, sizeof(*key), 0); //ip
    if(!key){
        cli_print(RED "[!] 分配ip键空间失败\n");
        cli_print(RESET "");
        return -1;
    }
    *key = ip;
    

    new_info = (ip_mac_info_t *)rte_malloc(NULL, sizeof(ip_mac_info_t), 0);
    for(i = 0; i < 6; i++)
        new_info->addr_bytes[i] = mac_addr_bytes[i];
    
    check_pt = cuckoo_hash_insert(ip_mac_store, key, 4, new_info);
    if(check_pt == NULL)
    {
        return 1;
    }
    else
    {
        rte_free(check_pt->value);
        check_pt->value = new_info;
        return 2;
    }
    
}

uint8_t *
ip_mac_store_search(uint32_t ip)
{
    struct cuckoo_hash_item *pt;
    
    if(NULL != (pt = cuckoo_hash_lookup(ip_mac_store, &ip, 4)))
    {
        return ((ip_mac_info_t *)(pt->value))->addr_bytes;
    }
    return NULL;
}
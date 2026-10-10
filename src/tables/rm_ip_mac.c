#include "tables/rm_ip_mac.h"
#include "tables/rm_tables.h"
#include "telemetry/rm_log.h"

int
ip_mac_store_initialize()
{
    ip_mac_store = (cuckoo_hash_t *)rte_malloc(NULL, sizeof(cuckoo_hash_t), 0);
    if (ip_mac_store == NULL) {
        RM_LOG_ERROR(RM_LOG_MODULE_TABLE, RM_EVENT_RESOURCE_EXHAUSTED,
            RM_LOG_PORT_NONE, "ARP cache initialization failed: no memory");
        return -1;
    }
    if(cuckoo_hash_init(ip_mac_store, 1)){
        RM_LOG_INFO(RM_LOG_MODULE_TABLE, RM_EVENT_TABLE_INITIALIZED,
            RM_LOG_PORT_NONE, "ARP cache initialized");
        return 0;
    }
    else{
        RM_LOG_ERROR(RM_LOG_MODULE_TABLE, RM_EVENT_TABLE_INITIALIZATION_FAILED,
            RM_LOG_PORT_NONE, "ARP cache initialization failed: hash allocation failed");
        rte_free(ip_mac_store);
        ip_mac_store = NULL;
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
        RM_LOG_ERROR(RM_LOG_MODULE_TABLE, RM_EVENT_RESOURCE_EXHAUSTED,
            RM_LOG_PORT_NONE, "ARP cache update failed: unable to allocate key");
        return -1;
    }
    *key = ip;
    

    new_info = (ip_mac_info_t *)rte_malloc(NULL, sizeof(ip_mac_info_t), 0);
    if (new_info == NULL) {
        rte_free(key);
        RM_LOG_ERROR(RM_LOG_MODULE_TABLE, RM_EVENT_RESOURCE_EXHAUSTED,
            RM_LOG_PORT_NONE, "ARP cache update failed: unable to allocate value");
        return -1;
    }
    for(i = 0; i < 6; i++)
        new_info->addr_bytes[i] = mac_addr_bytes[i];
    
    check_pt = cuckoo_hash_insert(ip_mac_store, key, 4, new_info);
    if(check_pt == NULL)
    {
        return 1;
    }
    else if (check_pt == CUCKOO_HASH_FAILED)
    {
        rte_free(key);
        rte_free(new_info);
        RM_LOG_ERROR(RM_LOG_MODULE_TABLE, RM_EVENT_RESOURCE_EXHAUSTED,
            RM_LOG_PORT_NONE, "ARP cache update failed: hash table expansion failed");
        return -1;
    }
    else
    {
        rte_free(key);
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

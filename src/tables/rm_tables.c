#include "tables/rm_tables.h"

/* 存储全局br信息 */
br_info_store_t br_store; // rb-tree

proxy_info_store_t proxy_store;

as_info_store_t as_store;

sid_store_t sid_store;

rm_acl_store_t acl_store;

my_basic_info_t *my_as_info;

cuckoo_hash_t *ip_mac_store;

tsid_table_t g_tsid_table = {0};

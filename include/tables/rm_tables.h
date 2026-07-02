#ifndef __RM_TABLES_H__
#define __RM_TABLES_H__

#include "tables/rm_as_graph.h"
#include "tables/rm_as_table.h"
#include "tables/rm_br_table.h"
#include "tables/rm_control.h"
#include "tables/rm_ip_mac.h"
#include "tables/rm_proxy_table.h"
#include "tables/rm_acl_table.h"
#include "tables/rm_sid_table.h"
#include "tables/rm_status.h"
#include "tables/rm_info.h"


extern br_info_store_t br_store; // rb-tree
extern proxy_info_store_t proxy_store; // rb-tree
extern as_info_store_t as_store; // array[array[array[]]]
extern sid_store_t sid_store; // cuckoo-hash
extern rm_acl_store_t acl_store;
extern my_basic_info_t *my_as_info;
extern cuckoo_hash_t *ip_mac_store;
extern tsid_table_t g_tsid_table;


#endif

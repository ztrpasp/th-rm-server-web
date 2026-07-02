#ifndef __RM_IP_MAC_STORE__
#define __RM_IP_MAC_STORE__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vendor/cuckoo_hash.h"
#include "vendor/utils.h"

typedef struct ip_mac_info
{
	uint8_t addr_bytes[6];
} ip_mac_info_t;


/** 初始化ARP缓存 */
int ip_mac_store_initialize();
/** 插入ARP缓存表 */ 
int ip_mac_store_insert(uint32_t ip, uint8_t *mac_addr_bytes);
/** 查找ARP缓存 */
uint8_t *ip_mac_store_search(uint32_t ip);

#endif
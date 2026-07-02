#ifndef __UTILS_H__
#define __UTILS_H__

#include "vendor/cuckoo_hash.h"
#include "vendor/jsw_rbtree.h"
#include "vendor/rte_array.h"


typedef struct jsw_rbtree tree_t;
typedef struct cuckoo_hash cuckoo_hash_t;

uint8_t _to_hex(char str);

// 0x12345678 10
// 0x12345 7
// 0x000000000000
int str_to_hex(char *str, uint8_t *buf, int buf_byte_len);
uint32_t str_to_ipaddr(char *str);

#endif
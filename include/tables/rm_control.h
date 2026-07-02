#ifndef __RM_CONTROL__
#define __RM_CONTROL__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tables/rm_tables.h"
#include "vendor/utils.h"

#define RM_PARSE_SUCCESS 1
#define RM_PARSE_ERR_FORMAT -1



char *next_mark(char *pt);

typedef struct parsr_br_buf
{
    int br_n;
    br_info_t *br_info_pt;
} parse_br_buf_t;

// parse ip setting for RM
const char * parse_ip_config(char *buf);

// parse basic_info
int parse_basic_config(char *buf);

// parse as_br_px config
int parse_as_br_px_config(char *buf);

// parse proxy_info and register
int parse_proxy_config(char *buf);

// parse ip config from file
/** 
 * 从配置文件中读取port id及其IP地址
 * 格式如下，#后面是port id和对应的IP地址，@标志开始位置，&结束位置
 * @
 * #portid ip_addr
 * #portid ip_addr
 * ...
 * &
 */
int read_ip_config(char *filename);

// parse config from file
int read_config_file(char *filename);

#endif
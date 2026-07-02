#include <stdint.h>
#ifndef RM_ROUTING_TABLE
#define RM_ROUTING_TABLE

/*
routing table made by lcf at 2025.07.04 11:33
*/

#define MAX_ROUTES 512

typedef struct route_entry{
    uint32_t prefix; //目的网络前缀(网段)
    uint32_t mask; //子网掩码
    uint32_t next_hop; //下一跳ip地址
    uint16_t port;//端口号，默认为0，目前RM只用端口0
} route_entry_t;

int add_route(uint32_t prefix, uint32_t mask, uint32_t next_hop, uint16_t port);
int longest_prefix_match(uint32_t dest_ip_nbo,
                         uint32_t *out_next_hop,
                         uint16_t *out_port);
int load_routes_from_file(const char *filename);


#endif
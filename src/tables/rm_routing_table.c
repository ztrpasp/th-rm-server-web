#include "commandline/cli.h"
#include "tables/rm_routing_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "vendor/print.h"

// 全局路由表和当前路由数量
static route_entry_t route_table[MAX_ROUTES];
static int current_route_count = 0;

// 辅助函数：将点分十进制IP转换为网络字节序的32位整数
static uint32_t ip_str_to_uint32_nbo(const char* ip_str) {
    unsigned int a, b, c, d;
    if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return 0; // 解析失败
    }
    return (a << 24) | (b << 16) | (c << 8) | d;
}

// 辅助函数：计算掩码的前缀长度
static uint32_t mask_to_prefix_length(uint32_t mask) {
    uint8_t *p = (uint8_t*)&mask; // 大端序字节：p[0]为最高字节
    uint32_t count = 0;
    
    for (int i = 0; i < 4; i++) {
        uint8_t b = p[i];
        if (b == 0xFF) {
            count += 8;
        } else {
            for (int j = 0; j < 8; j++) {
                if (b & (0x80 >> j)) { // 检查高位开始的每个bit
                    count++;
                } else {
                    return count; // 遇到0即停止
                }
            }
        }
    }
    return count;
}

// 添加路由表项
int add_route(uint32_t prefix, uint32_t mask, uint32_t next_hop, uint16_t port) {
    if (current_route_count >= MAX_ROUTES) {
        return -1; // 路由表已满
    }
    
    // 检查是否重复（所有字段完全匹配）
    for (int i = 0; i < current_route_count; i++) {
        if (route_table[i].prefix == prefix &&
            route_table[i].mask == mask &&
            route_table[i].next_hop == next_hop &&
            route_table[i].port == port) {
            return -1; // 重复条目
        }
    }
    
    // 添加新条目
    route_table[current_route_count] = (route_entry_t){
        .prefix = prefix,
        .mask = mask,
        .next_hop = next_hop,
        .port = port
    };
    current_route_count++;
    return 0;
}

// 最长前缀匹配
int longest_prefix_match(uint32_t dest_ip_nbo,
                         uint32_t *out_next_hop,
                         uint16_t *out_port) {
    int best_index = -1;
    uint32_t best_prefix_len = 0;
    
    for (int i = 0; i < current_route_count; i++) {
        uint32_t net_addr = dest_ip_nbo & route_table[i].mask;
        
        // 检查目标IP是否匹配当前路由表项[3,4](@ref)
        if (net_addr == route_table[i].prefix) {
            uint32_t prefix_len = mask_to_prefix_length(route_table[i].mask);
            
            // 更新最长匹配项[1,2](@ref)
            if (best_index == -1 || prefix_len > best_prefix_len) {
                best_index = i;
                best_prefix_len = prefix_len;
            }
        }
    }
    
    if (best_index != -1) {
        *out_next_hop = route_table[best_index].next_hop;
        *out_port = route_table[best_index].port;
        return 0;
    }
    return -1; // 未找到匹配项
}

// 从文件加载路由表项
int load_routes_from_file(const char *filename) {
    FILE* file = fopen(filename, "r");
    if (!file) return -1;
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // 移除换行符
        line[strcspn(line, "\r\n")] = 0;
        
        char *tokens[5];
        int token_count = 0;
        char *token = strtok(line, " ");
        
        // 分割行内容
        while (token && token_count < 5) {
            tokens[token_count++] = token;
            token = strtok(NULL, " ");
        }
        
        // 验证格式：ip route <prefix> <mask> <next_hop>
        if (token_count != 5 || 
            strcmp(tokens[0], "ip") != 0 || 
            strcmp(tokens[1], "route") != 0) {
            continue;
        }
        
        // 转换并添加路由
        uint32_t prefix = ip_str_to_uint32_nbo(tokens[2]);
        uint32_t mask = ip_str_to_uint32_nbo(tokens[3]);
        uint32_t next_hop = ip_str_to_uint32_nbo(tokens[4]);
        
        add_route(prefix, mask, next_hop, 0); // port默认为0
    }

    cli_print(GREEN "[+] Add routes finished\n");
    cli_print(RESET "");
    fclose(file);
    return 0;
}
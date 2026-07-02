#include "commandline/cli.h"
#include "tables/rm_control.h"
#include "tables/rm_tables.h"
#include "vendor/print.h"
#include <stdio.h>



char *next_group_mark(char *pt)
{
    int i;
    if(pt == NULL || *pt == '\0')
        return NULL;
    for(i = 0; ; i++)
    {
        if(pt[i] == '\0')
            return NULL;
        if(pt[i] == '@')
            return pt + i;
    }
}

char *next_mark(char *pt)
{
    int i;

    if(pt == NULL || *pt == '\0')
        return NULL;

    for(i = 0; ; i++)
    {
        if(pt[i] == '\0')
            return NULL;
        if(pt[i] == '#'
        || pt[i] == '&'
        || pt[i] == '$'
        || pt[i] == '^')
            return pt + i;
    }
}

// parse ip setting for RM
// #port ip_addr
// &
const char *parse_ip_config(char *buf)
{
    if(!buf)
        return "[!] bad port-ip format";
    
    char *pt = buf, mid_buf[32];
    int port;
    for(;;)
    {
        pt = next_mark(pt);
        if(!pt || *pt != '#')
            break;
        if(sscanf(pt++, "#%d %s", &port, mid_buf) != 2)
            return "[#] bad port-ip format";
        if(port < 0 || port > 32)
            continue;
        cli_print(GREEN "[+] port %d, ip address is %s\n", port, mid_buf);
        cli_print(RESET "");
        my_as_info->port_ip[port] = str_to_ipaddr(mid_buf);
    }
    return "[+] parse ip config success";

}

// parse basic_info
// #aid security_level sk
// &
// #t l v
// &
int parse_basic_config(char *buf)
{
    if(buf == NULL)
        return RM_PARSE_ERR_FORMAT;

    void *vpt;
    int a, t, l, s_level;
    uint32_t sk;
    uint8_t nid[128];
    char mid_buf[128], nid_str[128];
    char *v, *pt = next_mark(buf);

    if(!pt || *pt == '$')
    {
        if(sscanf(pt++, "$%s %d %d %x", nid_str, &a, &s_level, &sk) != 4)
            return RM_PARSE_ERR_FORMAT;
        str_to_hex(nid_str, nid, 16);
        global_set_as_info(nid, a, s_level, sk);
        pt = next_mark(pt);
    }

    // t l v
    for(;;)
    {
        pt = next_mark(pt);
        if(!pt || *pt != '#')
            break;
        if(sscanf(pt++, "#%d %d %s", &t, &l, mid_buf) != 3)
            return RM_PARSE_ERR_FORMAT;
        vpt = (void *)rte_malloc(NULL, l, 0);
        str_to_hex(mid_buf, vpt, l);
        global_add_strategy(t, l, vpt);
    }
    return RM_PARSE_SUCCESS;
}

// parse as_br_px config
// #br_n nid ip_addr
// &
// #aid security $br_n ^px
// &
int parse_as_br_px_config(char *buf)
{
    if(buf == NULL)
        return RM_PARSE_ERR_FORMAT;
    
    int i, br_n, aid, secu, px_count, br_buf_count, port;
    char *pt = buf;
    uint8_t nid[128], ip_addr[32], px[16];
    uint16_t px_mid;
    br_info_t br_mid;
    parse_br_buf_t br_buf[64];
    as_info_t *as_mid = NULL;
    connect_info_t *con_mid = NULL;

    // parse br_info
    for(br_buf_count = 0; ; )
    {
        pt = next_mark(pt);
        if(*pt != '#')
        {
            pt++;
            break;
        }
        if(sscanf(pt++, "#%d %s %s %d", &br_n, nid, ip_addr, &port) != 4)
            return RM_PARSE_ERR_FORMAT;
        // construct BR and insert
        str_to_hex(nid, br_mid.nid, 16);
        br_mid.ip_addr = str_to_ipaddr(ip_addr);
        br_mid.port = port; // TODO: port decision
        br_store_insert(&br_mid); // check
        br_buf[br_buf_count].br_n = br_n;
        br_buf[br_buf_count++].br_info_pt = br_store_search(br_mid.nid);
    }

    // parse as_info
    for(as_mid = NULL; ; )
    {
        pt = next_mark(pt); // as-info
        if(!pt || *pt != '$')
            break;

        if(sscanf(pt++, "$%d %d", &aid, &secu) != 2)
            return RM_PARSE_ERR_FORMAT;
        as_mid = as_info_create(aid, secu);
        
        for(con_mid = NULL; ; ) // connection-info
        {
            pt = next_mark(pt);
            if(!pt || *pt != '#')
                break;

            if(sscanf(pt++, "#%d", &br_n) != 1)
                return RM_PARSE_ERR_FORMAT;

            // find br for con and build con
            for(i = 0; i < br_buf_count; i++)
                if(br_buf[i].br_n == br_n)
                    break;
            if(i == br_buf_count) // unable to find the br for con
                return -1;
            con_mid = connect_info_create(br_buf[i].br_info_pt);
            
            for(;;)
            {
                pt = next_mark(pt);
                if(!pt || *pt != '^')
                    break;

                if(sscanf(pt++, "^%x", &px_mid) != 1)
                    return RM_PARSE_ERR_FORMAT;
                connect_info_insert(con_mid, px_mid);
            }
            as_info_insert(as_mid, con_mid);
        }
        as_store_insert(as_mid);
    }
    return RM_PARSE_SUCCESS;
}

// parse proxy_info and register
// #nid ip_addr port
int parse_proxy_config(char *buf)
{
    if(!buf){
        cli_print("[#] 无法读取字符串\n");
        return RM_PARSE_ERR_FORMAT;
    }
    // cli_print("[lcf@debug] 可以读取字符串\n");
    proxy_info_t ans;
    int port;
    char nid[128], ip[32], *pt = next_mark(buf);

    if(pt && *pt == '#')
    {
        if(sscanf(buf, "#%s %s %d", nid, ip, &port) != 3){
            cli_print("[lcf@debug] 输入格式不对\n");
            return RM_PARSE_ERR_FORMAT;
        }
            
        ans.port = port; // TODO: port decision
        ans.ip_addr = str_to_ipaddr(ip);
        str_to_hex(nid, ans.nid, 16);
        proxy_store_insert(&ans);
    }
    return RM_PARSE_SUCCESS;
}

int parse_multi_proxy_config(char *buf)
{
    if(!buf)
        return RM_PARSE_ERR_FORMAT;
    
    proxy_info_t ans;
    int port;
    char nid[128], ip[32], *pt = next_mark(buf);

    for(;;)
    {
        pt = next_mark(pt);
        if(!pt || *pt != '#')
            break;
        if(sscanf(pt++, "#%s %s %d", nid, ip, &port) != 3)
            return RM_PARSE_ERR_FORMAT;
        ans.port = port; // TODO: port decision
        ans.ip_addr = str_to_ipaddr(ip);
        str_to_hex(nid, ans.nid, 16);
        proxy_store_insert(&ans);
    }

    return RM_PARSE_SUCCESS;
}

int
read_ip_config(char *filename)
{
    if(filename == NULL)
        return -1;

    FILE *fp;
    int read_n, offset;
    uint8_t *read_buf, *pt;

    // open and read the file
    if((fp = fopen(filename, "r")) == NULL)
    {
        cli_print("config-file missing\n");
        return -2;
    }

    read_buf = rte_malloc(NULL, 4096, 0);

    for(offset = 0;;)
    {
        read_n = fread(read_buf + offset, 1, 64, fp);
        offset += read_n;
        if(read_n < 64)
        {
            read_buf[offset] = '\0';
            break;
        }
    }
    
    cli_print("Read %dB of config\n", offset);
    
    pt = next_group_mark(read_buf);
    if(pt == NULL)
    {
        rte_free(read_buf);
        return -3;
    }
    cli_print("port - IP configuration: %s\n", parse_ip_config(pt));
    
    rte_free(read_buf);
    fclose(fp);
    return 0;
}

int
read_config_file(char *filename)
{
    if(filename == NULL)
        return -1;

    FILE *fp;
    int read_n, offset;
    uint8_t *read_buf, *pt;

    // open and read the file
    if((fp = fopen(filename, "r")) == NULL)
    {
        cli_print("config-file missing\n");
        return -2;
    }

    read_buf = rte_malloc(NULL, 4096, 0);

    for(offset = 0;;)
    {
        read_n = fread(read_buf + offset, 1, 64, fp);
        offset += read_n;
        if(read_n < 64)
        {
            read_buf[offset] = '\0';
            break;
        }
    }
    
    cli_print("Read %dB of config\n", offset);

    pt = next_group_mark(read_buf);
    if(pt == NULL)
    {
        rte_free(read_buf);
        return -3;
    }
    else
        cli_print("port - IP configuration: %s\n", parse_ip_config(pt));

    pt = next_group_mark(pt + 1);
    if(pt == NULL)
    {
        rte_free(read_buf);
        return -3;
    }
    else
        cli_print("BASIC_PARSE_RES : %d\n", parse_basic_config(pt));

    pt = next_group_mark(pt + 1);
    if(pt == NULL)
    {
        rte_free(read_buf);
        return -3;
    }
    else
        cli_print("ABP_PARSE_RES : %d\n", parse_as_br_px_config(pt));

    pt = next_group_mark(pt + 1);
    if(pt == NULL)
    {
        rte_free(read_buf);
        return -3;
    }
    else
        cli_print("PROXY_PARSE_RES : %d\n", parse_multi_proxy_config(pt));

    rte_free(read_buf);
    fclose(fp);
    return 0;
}
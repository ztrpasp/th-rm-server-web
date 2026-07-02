//
// Created by icccn-edge-1 on 25-5-16.
//

#ifndef PKT_H
#define PKT_H

#include "packets/rte_CoLoR.h"
#include "process/rm_send.h"
#include "tables/rm_tables.h"
#include "tables/rm_routing_table.h"

__rte_always_inline int
rm_search_ip_return_port(uint32_t ip_addr)
{
    int i;
    for(i = 0; i < 32; i++)
        if(my_as_info->port_ip[i] == ip_addr)
            return i;
    return -1;
}

/** 从内存池中分配mbuf，并用 \b buf 的数据构造新的pkt。查ARP表，如果存在 \b dst_ip 对应的表项，
 * 发送新构造的数据包到端口 \b port ；否则，构造一个ARP请求报文并发送到端口 \b port 。 */
__rte_always_inline void
rm_send_new_ip_pkt(struct lcore_conf* qconf, void* buf, int len,
                   int src_ip, int dst_ip, uint8_t* src_mac, int port)
{
    uint8_t* dst_mac;
    struct rte_ether_hdr eth_layer;
    struct rte_ipv4_hdr ip_layer;
    struct rte_mbuf* pkt = (struct rte_mbuf*)rte_pktmbuf_alloc((struct rte_mempool*)broadcast_tx_pool);


    pkt->data_len = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + len;

    // find mac-addr and update eth header
    //lcf:这里不是dst_ip的mac了，应该改成下一跳ip的mac
    //dst_mac = ip_mac_store_search(dst_ip);
    uint32_t next_hop_ip;
    uint16_t out_port;
    int ret = 0;
    ret = longest_prefix_match((uint32_t)dst_ip, &next_hop_ip, &out_port);
    if(ret == -1)
    {
        //如果我们在静态路由表中，没有找到对应条目，那么我们就认为下一跳地址就是目的地址
        next_hop_ip = dst_ip;
    }
    dst_mac = ip_mac_store_search(next_hop_ip);
    my_setup_ether_header(&eth_layer, src_mac, dst_mac);
    // set up ip header
    my_setup_ip_header(&ip_layer, src_ip, dst_ip, len);

    copy_buf_to_pkt(&eth_layer, sizeof(struct rte_ether_hdr), pkt, 0);
    copy_buf_to_pkt(&ip_layer, sizeof(struct rte_ipv4_hdr), pkt, sizeof(eth_layer));
    copy_buf_to_pkt(buf, len, pkt, sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));

    //set basic info of pkg
    pkt->nb_segs = 1; //nb_segs;//
    pkt->ol_flags = RTE_MBUF_F_TX_IPV4; //ol_flags;//
    pkt->vlan_tci = 0; //vlan_tci;//
    pkt->vlan_tci_outer = 0; //vlan_tci_outer;//
    pkt->l2_len = sizeof(struct rte_ether_hdr);
    pkt->l3_len = sizeof(struct rte_ipv4_hdr);
    pkt->pkt_len = pkt->data_len;


    if (dst_mac == NULL)
    {
        // if unable to find ip-mac, store pkt
        unsend_buf_add(dst_ip, next_hop_ip, port, pkt);
        // send ARP request
        //lcf
        struct rte_mbuf* arp_pkt = construct_arp_req(src_ip, next_hop_ip, src_mac);
        send_single_packet(qconf, arp_pkt, port);
    }
    else
        send_single_packet(qconf, pkt, port);
}

/** 将 \b buf 指向的控制报文的头部及数据保存到文件 \b filename 中。 */
__rte_always_inline void
store_control_rm_config(char *filename, uint8_t *buf)
{
    int offset = 0, len;

    // get the len of pkg
    CONTROL_front_t *con_pt = (CONTROL_front_t *)buf;
    len = con_pt->header_len + con_pt->len;

    FILE *fp = fopen(filename, "wb");

    if(fp == NULL)
    {
        printf("Unable to open file\n");
        return;
    }

    for(;;)
    {
        if(len >= 256)
        {
            fwrite(buf + offset, 1, 256, fp);
            len -= 256;
            offset += 256;
        }
        else
        {
            fwrite(buf + offset, 1, len, fp);
            break;
        }
    }

    fclose(fp);
}

/** 发送配置信息（SK，AS-PX-IP）给BR */
__rte_always_inline void
send_config_br(struct lcore_conf *qconf)
{
    // SYN topology config to BRs
    // NID SK
    // num [AS PX IP]n
    uint8_t *pkt_buf = rte_malloc(NULL, 512, 0);
    uint8_t *con_data = rte_malloc(NULL, 512, 0);
    br_info_t *br_buf[64];
    int num_br = br_store_ret_node_list(br_buf);
    int num_path = as_store_ret_as_px_ip_list(con_data + 21);
    // con_data前16字节是BR的NID
    *(uint32_t *)(con_data + 16) = my_as_info->sk;
    *(uint8_t *)(con_data + 20) = num_path;
    // as id: 4 bytes, prefix length: 1byte, px: 4 bytes, ip address: 4 bytes
    int con_len = 16 + 4 + 1 + num_path * 13;
    for(int i = 0; i < num_br; i++)
    {
        rte_memcpy(con_data, br_buf[i]->nid, 16);
        CONTROL_init(pkt_buf, CONTROL_CONFIG_BR, con_len, con_data);
        CONTROL_update_checksum(pkt_buf);

        uint8_t *p = pkt_buf + sizeof(CONTROL_front_t) + 21;
        for(int i = 0; i < num_path; i++)
        {
            printf("AS %x, PX 0x%08x/%d, IP %08x\n",
                    *(uint32_t *)p,
                    *(uint32_t *)(p + 5),
                    *(uint8_t *)(p + 4),
                    *(uint32_t *)(p + 9));
            p += 13;
        }

        uint8_t dst_port = br_buf[i]->port;
        uint32_t src_ip = my_as_info->port_ip[dst_port];
        uint32_t dst_ip = br_buf[i]->ip_addr;
        struct rte_ether_addr port_mac_addr;
        rte_eth_macaddr_get(dst_port, &port_mac_addr);
        rm_send_new_ip_pkt(qconf, pkt_buf, CoLoR_get_pkg_length(pkt_buf),
            src_ip, dst_ip, port_mac_addr.addr_bytes, dst_port);
    }
    rte_free(pkt_buf);
    rte_free(con_data);
}

/** 发送config_rm确认 */
__rte_always_inline void
send_control_config_rm_ack(uint8_t dst_port, uint32_t dst_ip, struct lcore_conf *qconf)
{
    printf("Returning ACK_CONTROL_CONFIG_RM to sender\n");
    uint32_t src_ip = my_as_info->port_ip[dst_port];
    struct rte_ether_addr port_mac_addr;
    rte_eth_macaddr_get(dst_port, &port_mac_addr);
    uint8_t *pkt_buf = rte_malloc(NULL, 512, 0);
    uint8_t *con_data = rte_malloc(NULL, 512, 0);
    *con_data = ACK_SUCCESS;
    CONTROL_init(pkt_buf, ACK_CONTROL_CONFIG_RM, 1, con_data);
    CONTROL_update_checksum(pkt_buf);
    rm_send_new_ip_pkt(qconf, pkt_buf, CoLoR_get_pkg_length(pkt_buf),
            src_ip, dst_ip, port_mac_addr.addr_bytes, dst_port);
    rte_free(pkt_buf);
    rte_free(con_data);
}

#endif //PKT_H

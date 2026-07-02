//
// Created by lcf2001523@gmail.com on 25-5-16.
//
#include "packets/arp.h"

void
process_arp_packet(struct rte_mbuf *m, uint8_t portid, struct lcore_conf *qconf)
{
    struct rte_ether_addr port_mac_addr;
    struct rte_ether_hdr *eth_layer_pt = (struct rte_ether_hdr *)rte_pktmbuf_mtod_offset(m, struct rte_ether_hdr *, 0);
    struct rte_arp_hdr *a_pt = rte_pktmbuf_mtod_offset(m, struct rte_arp_hdr *, sizeof(struct rte_ether_hdr));

    // if get ARP REQ
    if(a_pt->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REQUEST))
    {
        struct rte_ether_addr request_mac_addr;
        // check IP dst
        int dst_port = rm_search_ip_return_port(rte_cpu_to_be_32(a_pt->arp_data.arp_tip));
        if(dst_port == -1)
        {
            rte_pktmbuf_free(m);
            return;
        }
        else
            rte_eth_macaddr_get(dst_port, &request_mac_addr);

        // update rte_ether_hdr
        rte_eth_macaddr_get(portid, &port_mac_addr);
        copy_eth_addr(eth_layer_pt->src_addr.addr_bytes, eth_layer_pt->dst_addr.addr_bytes);
        copy_eth_addr(port_mac_addr.addr_bytes, eth_layer_pt->src_addr.addr_bytes);

        // update rte_arp_hdr
        a_pt->arp_opcode = rte_cpu_to_be_16(RTE_ARP_OP_REPLY);
        uint32_t dst_ip = a_pt->arp_data.arp_tip;
        a_pt->arp_data.arp_tip = a_pt->arp_data.arp_sip;
        a_pt->arp_data.arp_sip = dst_ip;
        copy_eth_addr(a_pt->arp_data.arp_sha.addr_bytes, a_pt->arp_data.arp_tha.addr_bytes);
        copy_eth_addr(request_mac_addr.addr_bytes, a_pt->arp_data.arp_sha.addr_bytes);

        send_single_packet(qconf, m, portid);
    }
    // if get ARP RES
    else if(a_pt->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REPLY))
    {
        // printf("Get ARP res\n");
        // printf("src_MAC : ");show_data(&(a_pt->arp_data.arp_sha), 6);
        // printf("\ndst_MAC : ");show_data(&(a_pt->arp_data.arp_tha), 6);
        // printf("\nsrc_ip : %x\ndst_ip : %x\n", a_pt->arp_data.arp_sip, a_pt->arp_data.arp_tip);

        uint32_t dst_ip = rte_cpu_to_be_32(a_pt->arp_data.arp_sip);

        // update ip-mac table
        ip_mac_store_insert(dst_ip, a_pt->arp_data.arp_sha.addr_bytes);
        // resend pkts in buf
        unsend_buf_resend(qconf, dst_ip, a_pt->arp_data.arp_tha.addr_bytes,
                                            a_pt->arp_data.arp_sha.addr_bytes);

        rte_pktmbuf_free(m);
    }
}

#include "process/rm_send.h"

unsend_buf_t *unsend_store;

int
unsend_buf_initialize(void)
{
    unsend_store = (unsend_buf_t *)rte_zmalloc(
        "rm_unsent_store", sizeof(unsend_buf_t), 0);
    if (unsend_store == NULL) {
        RM_LOG_ERROR(RM_LOG_MODULE_TABLE, RM_EVENT_TABLE_INITIALIZATION_FAILED,
            RM_LOG_PORT_NONE, "unsent packet queue initialization failed");
        return -1;
    }
    unsend_store->head_node = (unsend_pkt_t *)rte_malloc(NULL, sizeof(unsend_pkt_t), 0);
    if (unsend_store->head_node == NULL) {
        rte_free(unsend_store);
        unsend_store = NULL;
        RM_LOG_ERROR(RM_LOG_MODULE_TABLE, RM_EVENT_TABLE_INITIALIZATION_FAILED,
            RM_LOG_PORT_NONE, "unsent packet queue head allocation failed");
        return -1;
    }
    unsend_store->head_node->next = NULL;
    unsend_store->tail_node = unsend_store->head_node;
    RM_LOG_INFO(RM_LOG_MODULE_TABLE, RM_EVENT_TABLE_INITIALIZED,
        RM_LOG_PORT_NONE, "unsent packet queue initialized");
    return 0;
}

int
unsend_buf_add(uint32_t d_ip, uint32_t next_hop_ip, uint32_t d_port, struct rte_mbuf *pkt)
{
    unsend_pkt_t *new_pkt = (unsend_pkt_t *)rte_malloc(NULL, sizeof(unsend_pkt_t), 0);
    if (new_pkt == NULL) {
        rte_pktmbuf_free(pkt);
        RM_LOG_ERROR(RM_LOG_MODULE_TABLE, RM_EVENT_RESOURCE_EXHAUSTED,
            d_port, "unable to queue packet awaiting ARP resolution");
        return -1;
    }

    new_pkt->ip_addr = d_ip;
    new_pkt->next_hop_ip = next_hop_ip;
    new_pkt->port = d_port;
    new_pkt->pkt = pkt;
    new_pkt->next = NULL;

    unsend_store->tail_node->next = new_pkt;
    unsend_store->tail_node = new_pkt;
    return 0;
}

void
unsend_buf_resend(struct lcore_conf *qconf, uint32_t d_ip, uint8_t *src_mac, uint8_t *dst_mac)
{
    if(unsend_store->head_node->next == NULL)
        return;

    unsend_pkt_t *npt, *p_npt;
    struct rte_ether_hdr *eth_layer_pt;

    // check if tail node need to be sent
    if(unsend_store->tail_node->next_hop_ip == d_ip)
        unsend_store->tail_node = NULL;

    for(p_npt = unsend_store->head_node, npt = p_npt->next; npt != NULL; )
    {
        // match ip_addr
        if(npt->next_hop_ip == d_ip)
        {
            // send the pkt
            struct rte_mbuf *pt = npt->pkt;
            eth_layer_pt = (struct rte_ether_hdr *)rte_pktmbuf_mtod_offset(pt, struct rte_ether_hdr *, 0);
            my_setup_ether_header(eth_layer_pt, src_mac, dst_mac);
            send_single_packet(qconf, pt, npt->port);

            // delete node
            p_npt->next = npt->next;
            rte_free(npt);
            npt = p_npt->next;
        }
        else
        {
            p_npt = npt;
            npt = npt->next;
        }
    }

    // update tail pointer
    if(unsend_store->tail_node == NULL)
    {
        for(npt = unsend_store->head_node; npt->next != NULL; npt = npt->next)
            continue;
        unsend_store->tail_node = npt;
    }
}

struct rte_mbuf *
construct_arp_req(uint32_t src_ip, uint32_t dst_ip, uint8_t *my_src_mac)
{
    struct rte_mbuf *pkt = (struct rte_mbuf *)rte_pktmbuf_alloc((struct rte_mempool *)broadcast_tx_pool);
    if (pkt == NULL) {
        RM_LOG_ERROR(RM_LOG_MODULE_ARP, RM_EVENT_RESOURCE_EXHAUSTED,
            RM_LOG_PORT_NONE, "unable to allocate ARP request packet");
        return NULL;
    }

    struct rte_ether_hdr eth_layer;
    struct rte_arp_hdr arp_layer;

    // ether initialize
    // src mac
    copy_eth_addr(my_src_mac, (eth_layer.src_addr).addr_bytes);
    // dst mac
    memset((eth_layer.dst_addr).addr_bytes, 0xff, 6);
    eth_layer.ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP);

    // arp initialize
    arp_layer.arp_opcode = rte_cpu_to_be_16(RTE_ARP_OP_REQUEST);
    arp_layer.arp_hardware = rte_cpu_to_be_16(RTE_ARP_HRD_ETHER);
    arp_layer.arp_protocol = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    arp_layer.arp_hlen = RTE_ETHER_ADDR_LEN;
    arp_layer.arp_plen = sizeof(uint32_t);

    arp_layer.arp_data.arp_sip = rte_cpu_to_be_32(src_ip);
    arp_layer.arp_data.arp_tip = rte_cpu_to_be_32(dst_ip);
    copy_eth_addr(my_src_mac, (arp_layer.arp_data).arp_sha.addr_bytes);
    memset(&arp_layer.arp_data.arp_tha.addr_bytes, 0, RTE_ETHER_ADDR_LEN);

    pkt->data_len = sizeof(struct rte_ether_hdr) + sizeof(struct rte_arp_hdr);
    copy_buf_to_pkt(&eth_layer, sizeof(struct rte_ether_hdr), pkt, 0);
    copy_buf_to_pkt(&arp_layer, sizeof(struct rte_arp_hdr), pkt, sizeof(eth_layer));

    //set basic info of pkg
    pkt->nb_segs = 1;//nb_segs;//
    pkt->ol_flags = RTE_MBUF_F_TX_IPV4;//ol_flags;//
    pkt->vlan_tci = 0;//vlan_tci;//
    pkt->vlan_tci_outer = 0;//vlan_tci_outer;//
    pkt->l2_len = sizeof(struct rte_ether_hdr);
    pkt->l3_len = sizeof(struct rte_arp_hdr);
    pkt->pkt_len = pkt->data_len;

    return pkt;
}

#ifndef __RM_SEND__
#define __RM_SEND__

#include <rte_common.h>
#include <rte_byteorder.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_memory.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_ether.h>
#include <rte_arp.h>
#include "process/send.h"
#include "tables/rm_routing_table.h"
#include "telemetry/rm_log.h"
#include "vendor/print.h"


#ifdef __GCC__
#define RTE_BE_TO_CPU_16(be_16_v)  rte_be_to_cpu_16((be_16_v))
#define RTE_CPU_TO_BE_16(cpu_16_v) rte_cpu_to_be_16((cpu_16_v))
#else
#if RTE_BYTE_ORDER == RTE_BIG_ENDIAN
#define RTE_BE_TO_CPU_16(be_16_v)  (be_16_v)
#define RTE_CPU_TO_BE_16(cpu_16_v) (cpu_16_v)
#else
#define RTE_BE_TO_CPU_16(be_16_v) \
	(uint16_t) ((((be_16_v) & 0xFF) << 8) | ((be_16_v) >> 8))
#define RTE_CPU_TO_BE_16(cpu_16_v) \
	(uint16_t) ((((cpu_16_v) & 0xFF) << 8) | ((cpu_16_v) >> 8))
#endif
#endif /* __GCC__ */

#define IP_DEFTTL  64   /* from RFC 1340. */
#define IP_VERSION 0x40
#define IP_HDRLEN  0x05 /* default IP header length == five 32-bits words. */
#define IP_VHL_DEF (IP_VERSION | IP_HDRLEN)

typedef struct unsend_pkt
{
	uint32_t ip_addr;
	uint32_t next_hop_ip;
	uint32_t port;
	struct rte_mbuf *pkt;
	struct unsend_pkt *next;

} unsend_pkt_t;

typedef struct unsend_buf
{
	unsend_pkt_t *head_node;
	unsend_pkt_t *tail_node;
} unsend_buf_t;

extern unsend_buf_t *unsend_store;

static void
copy_eth_addr(uint8_t *f, uint8_t *t)
{
	uint16_t *from = (uint16_t *)f;
	uint16_t *to = (uint16_t *)t;
	to[0] = from[0];
	to[1] = from[1];
	to[2] = from[2];
}

static void
copy_buf_to_pkt_segs(void* buf, unsigned len, struct rte_mbuf *pkt,
		     unsigned offset)
{
	struct rte_mbuf *seg;
	void *seg_buf;
	unsigned copy_len;

	seg = pkt;
	while (offset >= seg->data_len) {
		offset -= seg->data_len;
		seg = seg->next;
	}
	copy_len = seg->data_len - offset;
	seg_buf = rte_pktmbuf_mtod_offset(seg, char *, offset);
	while (len > copy_len) {
		rte_memcpy(seg_buf, buf, (size_t) copy_len);
		len -= copy_len;
		buf = ((char*) buf + copy_len);
		seg = seg->next;
		seg_buf = rte_pktmbuf_mtod(seg, char *);
	}
	rte_memcpy(seg_buf, buf, (size_t) len);
}

static inline void
copy_buf_to_pkt(void* buf, unsigned len, struct rte_mbuf *pkt, unsigned offset)
{
	if (offset + len <= pkt->data_len) {
		rte_memcpy(rte_pktmbuf_mtod_offset(pkt, char *, offset),
			buf, (size_t) len);
		return;
	}
	copy_buf_to_pkt_segs(buf, len, pkt, offset);
}

static void
calculate_checksum_for_ip(struct rte_ipv4_hdr *ip_hdr)
{
	/*
	 * Compute IP header checksum.
	 */
	uint16_t *ptr16;
	uint32_t ip_cksum;

	ptr16 = (unaligned_uint16_t*) ip_hdr;
	ip_cksum = 0;
	ip_cksum += ptr16[0]; ip_cksum += ptr16[1];
	ip_cksum += ptr16[2]; ip_cksum += ptr16[3];
	ip_cksum += ptr16[4];
	ip_cksum += ptr16[6]; ip_cksum += ptr16[7];
	ip_cksum += ptr16[8]; ip_cksum += ptr16[9];

	/*
	 * Reduce 32 bit checksum to 16 bits and complement it.
	 */
	ip_cksum = ((ip_cksum & 0xFFFF0000) >> 16) +
		(ip_cksum & 0x0000FFFF);
	if (ip_cksum > 65535)
		ip_cksum -= 65535;
	ip_cksum = (~ip_cksum) & 0x0000FFFF;
	if (ip_cksum == 0)
		ip_cksum = 0xFFFF;
	ip_hdr->hdr_checksum = (uint16_t) ip_cksum;
}

static void
my_ip_set_length(struct rte_ipv4_hdr *ip_hdr, uint32_t len)
{
	if(ip_hdr == NULL)
		return;
	ip_hdr->total_length = RTE_CPU_TO_BE_16(len);
	calculate_checksum_for_ip(ip_hdr);
}

//construct IP header
static void
my_setup_ip_header(struct rte_ipv4_hdr *ip_hdr, uint32_t src_addr, uint32_t dst_addr, uint32_t len)
{
	if(ip_hdr == NULL)
		return;

	ip_hdr->version_ihl   = IP_VHL_DEF;
	ip_hdr->type_of_service   = 0;
	ip_hdr->fragment_offset = 0;
	ip_hdr->time_to_live   = IP_DEFTTL;
	ip_hdr->next_proto_id = 150;
	ip_hdr->packet_id = 0;
	ip_hdr->total_length = RTE_CPU_TO_BE_16(20 + len);//RTE_CPU_TO_BE_16(pkt_len);
	ip_hdr->src_addr = rte_cpu_to_be_32(src_addr);
	ip_hdr->dst_addr = rte_cpu_to_be_32(dst_addr);

	calculate_checksum_for_ip(ip_hdr);
}

static void
my_update_ip_header(struct rte_ipv4_hdr *ip_hdr, uint32_t src_addr, uint32_t dst_addr, uint32_t len)
{
	if(ip_hdr == NULL)
		return;
	ip_hdr->total_length = RTE_CPU_TO_BE_16(20 + len);//RTE_CPU_TO_BE_16(pkt_len);
	ip_hdr->src_addr = rte_cpu_to_be_32(src_addr);
	ip_hdr->dst_addr = rte_cpu_to_be_32(dst_addr);

	calculate_checksum_for_ip(ip_hdr);
}

static void
my_setup_ether_header(struct rte_ether_hdr *eth_hdr, uint8_t *src_mac, uint8_t *dst_mac)
{
    eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
	int i;
	
	if(!src_mac)
		memset(eth_hdr->src_addr.addr_bytes, 0x0, 6);
	else
		for(i = 0; i < 6; i++)
			(eth_hdr->src_addr).addr_bytes[i] = src_mac[i];
	
	if(!dst_mac)
		memset(eth_hdr->dst_addr.addr_bytes, 0xff, 6);
	else
		for(i = 0; i < 6; i++)
			(eth_hdr->dst_addr).addr_bytes[i] = dst_mac[i];
}

static int
unsend_buf_initialize()
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

static int
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

static void
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



extern struct rte_mempool *broadcast_tx_pool;

static struct rte_mbuf *
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

#endif

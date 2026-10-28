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

static inline void
copy_eth_addr(uint8_t *f, uint8_t *t)
{
	uint16_t *from = (uint16_t *)f;
	uint16_t *to = (uint16_t *)t;
	to[0] = from[0];
	to[1] = from[1];
	to[2] = from[2];
}

static inline void
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

static inline void
calculate_checksum_for_ip(struct rte_ipv4_hdr *ip_hdr)
{
	uint16_t cksum;

	cksum = rte_raw_cksum(ip_hdr, sizeof(struct rte_ipv4_hdr));
	cksum = (cksum == 0xffff) ? cksum : (uint16_t)~cksum;
	ip_hdr->hdr_checksum = cksum;
}

static inline void
my_ip_set_length(struct rte_ipv4_hdr *ip_hdr, uint32_t len)
{
	if(ip_hdr == NULL)
		return;
	ip_hdr->total_length = RTE_CPU_TO_BE_16(len);
	calculate_checksum_for_ip(ip_hdr);
}

//construct IP header
static inline void
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

static inline void
my_update_ip_header(struct rte_ipv4_hdr *ip_hdr, uint32_t src_addr, uint32_t dst_addr, uint32_t len)
{
	if(ip_hdr == NULL)
		return;
	ip_hdr->total_length = RTE_CPU_TO_BE_16(20 + len);//RTE_CPU_TO_BE_16(pkt_len);
	ip_hdr->src_addr = rte_cpu_to_be_32(src_addr);
	ip_hdr->dst_addr = rte_cpu_to_be_32(dst_addr);

	calculate_checksum_for_ip(ip_hdr);
}

static inline void
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

int unsend_buf_initialize(void);
int unsend_buf_add(uint32_t d_ip, uint32_t next_hop_ip, uint32_t d_port, struct rte_mbuf *pkt);
void unsend_buf_resend(struct lcore_conf *qconf, uint32_t d_ip, uint8_t *src_mac, uint8_t *dst_mac);
extern struct rte_mempool *broadcast_tx_pool;
struct rte_mbuf *construct_arp_req(uint32_t src_ip, uint32_t dst_ip, uint8_t *my_src_mac);

#endif

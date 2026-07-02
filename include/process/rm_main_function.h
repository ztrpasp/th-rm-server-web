#ifndef __RM_FUNC_H__
#define __RM_FUNC_H__

extern struct rte_mempool *broadcast_tx_pool;

#include "packets/rte_CoLoR.h"
#include "process/rm_pkt_process.h"
#include "process/rm_send.h"
#include "tables/rm_tables.h"
#include "packets/pkt.h"
#include "packets/arp.h"
#include "packets/ann.h"
#include "packets/ctrl.h"
#include "packets/get.h"


static __rte_always_inline void
rm_process_single_packet(struct rte_mbuf *m, uint8_t portid, struct lcore_conf *qconf)
{
    int i, dst_port = 3, check, num_path, c_len;
    uint32_t src_ip, dst_ip;
    uint8_t *dst_mac;
    struct rte_ether_addr port_mac_addr;
    struct rte_mbuf *pkt;
    struct rte_ether_hdr *eth_layer_pt = (struct rte_ether_hdr *)rte_pktmbuf_mtod_offset(m, struct rte_ether_hdr *, 0);

	// arp type
	if(eth_layer_pt->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP))
	{
		process_arp_packet(m, portid, qconf);
		return;
	}

	// filter non-IP protocol
	else if(eth_layer_pt->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
	{
		// printf("Filter pkg of no IPv4\n");
		rte_pktmbuf_free(m);
		return;
	}

	// Not CoLoR packet
	struct rte_ipv4_hdr *ip_layer_pt = (struct rte_ipv4_hdr *)rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
	if (ip_layer_pt->next_proto_id != 150) {
		rte_pktmbuf_free(m);
		return;
	}

	uint8_t *pkt_data = rte_pktmbuf_mtod_offset(m, uint8_t *, sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));

	// color protocol
	if((check = CoLoR_get_pkg_protocol(pkt_data)) != COLOR_PROTOCOL)
	{
		// printf("Get pkg of unknown CoLoR protocol\n");
		rte_pktmbuf_free(m);
		return;
	}

	switch(CoLoR_get_pkg_type(pkt_data))
	{
		case COLOR_ANN_TYPE:
		{
			process_ann_packet(m, portid, qconf);
			break;
		}
		case COLOR_GET_TYPE:
		{
			process_get_packet(m, portid, qconf);
			break;
		}
		case COLOR_DATA_TYPE:
		{
			// RM不处理DATA包
			rte_pktmbuf_free(m);
			break;
		}
		case COLOR_CONTROL_TYPE:
		{
			process_control_packet(m, portid, qconf);
			break;
		}
		default: break;
	}

}

/*
 * 此函数处理若干个包，从main loop中调用此函数。
 */
static inline void
rm_process_packets(int nb_rx, struct rte_mbuf **pkts_recv,
			uint8_t portid, struct
			 lcore_conf *qconf)
{
	int32_t j;

	//process every single packet
	//including forward/create/drop...
	for(j = 0; j < nb_rx; j++)
	{
		rm_process_single_packet(pkts_recv[j], portid, qconf);
		// status_show(my_as_info->my_status);
	}

}


#endif

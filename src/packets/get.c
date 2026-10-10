//
// Created by lcf on 25-5-16.
//
#include "packets/get.h"
#include "tables/rm_routing_table.h"
#include "telemetry/rm_log.h"
#include "telemetry/rm_metrics.h"

static rm_packet_result_t
get_metric_result(int result)
{
	switch (result) {
	case RM_PROCESS_SUCCESS: return RM_PACKET_RESULT_OK;
	case RM_RETURN_SID_LIST: return RM_PACKET_RESULT_SID_LIST_RETURNED;
	case RM_ERR_CHECKSUM_UNPASSED: return RM_PACKET_RESULT_BAD_CHECKSUM;
	case RM_GET_SID_UNFOUND: return RM_PACKET_RESULT_SID_NOT_FOUND;
	case RM_GET_ACL_DENIED: return RM_PACKET_RESULT_ACL_DENIED;
	case RM_GET_POLICY_DENIED:
	case RM_GET_FILTERED: return RM_PACKET_RESULT_POLICY_DENIED;
	case RM_ERR_NO_MEMORY: return RM_PACKET_RESULT_NO_MEMORY;
	case RM_ERR_NULL_PT_INPUT: return RM_PACKET_RESULT_MALFORMED;
	default: return RM_PACKET_RESULT_INTERNAL_ERROR;
	}
}

void
process_get_packet(struct rte_mbuf *m, uint8_t portid, struct lcore_conf *qconf)
{
	(void)portid;
	rm_metrics_record_rx(RM_PACKET_GET);
	struct rte_ether_hdr *eth_layer_pt = rte_pktmbuf_mtod_offset(
		m, struct rte_ether_hdr *, 0);
	struct rte_ipv4_hdr *ip_layer_pt = rte_pktmbuf_mtod_offset(
		m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
	uint8_t *pkt_data = rte_pktmbuf_mtod_offset(m, uint8_t *, sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
	bool from_outside = ((GET_front_t *)pkt_data)->PID_num != 0;

	sid_info_t *sid_info;
	int process_result = rm_GET_process(pkt_data, &sid_info, ip_layer_pt, qconf);
	rm_packet_result_t metric_result = get_metric_result(process_result);
	if(RM_PROCESS_SUCCESS != process_result)
	{
		rm_metrics_record_result(RM_PACKET_GET, metric_result);
		rte_pktmbuf_free(m);
		return;
	}
	if (from_outside)
		rm_metrics_add(RM_METRIC_GET_FROM_OUTSIDE, 1);
	else
		rm_metrics_add(RM_METRIC_GET_FROM_INSIDE, 1);
	uint8_t dst_port;
	uint32_t dst_ip;
	if(sid_info->px == 0) // 提供者在本域
	{
		rm_metrics_add(RM_METRIC_GET_TO_INSIDE, 1);

		proxy_info_t *node = (proxy_info_t *)sid_info->node_pt;
		dst_port = node->port;
		dst_ip = node->ip_addr; // 下一跳IP地址是终端节点的IP地址

		// 开启数据泄漏检测时，返回CONTROL to BR to SYN SID
		if(global_quick_check_strategy(STRATEGY_DATA_LEAKAGE_CHECK))
		{
			GET_front_t *get_pt = (GET_front_t *)pkt_data;
			if (get_pt->PID_num > 0) {
				// get the BR
				uint16_t px = (uint16_t)((*(uint32_t *)(pkt_data + (get_pt->pkg_len - 4))) >> 16);
				search_px_result_t res_br;
				struct rte_ether_addr port_mac_addr_br;

				if(1 != as_store_search_px(px, &res_br))
				{
					rm_metrics_record_result(RM_PACKET_GET,
						RM_PACKET_RESULT_POLICY_DENIED);
					RM_LOG_WARN(RM_LOG_MODULE_GET, RM_EVENT_GET_POLICY_DENIED,
						dst_port, "PX 0x%04x has no border-router mapping", px);
					rte_pktmbuf_free(m);
					return;
				}

				// append random number
				uint32_t rn = 0x20220220; // todo: random number
				GET_append_rn(pkt_data, rn);
				get_pt->flag = SET_GET_FLAG_R(get_pt->flag);
				GET_update_checksum(pkt_data);

				uint8_t dst_port_br = res_br.target_br->port;
				uint32_t dst_ip_br = res_br.target_br->ip_addr;
				uint32_t src_ip_br = my_as_info->port_ip[dst_port_br];
				rte_eth_macaddr_get(dst_port_br, &port_mac_addr_br);

				void *pkt_buf = rte_malloc(NULL, 128, 0);
				void *con_buf = rte_malloc(NULL, 1024, 0);
				if (pkt_buf == NULL || con_buf == NULL) {
					rte_free(pkt_buf);
					rte_free(con_buf);
					rm_metrics_record_result(RM_PACKET_GET,
						RM_PACKET_RESULT_NO_MEMORY);
					RM_LOG_ERROR(RM_LOG_MODULE_GET, RM_EVENT_RESOURCE_EXHAUSTED,
						dst_port, "unable to allocate data-leakage control packet");
					rte_pktmbuf_free(m);
					return;
				}

				*(uint32_t *)con_buf = rn; // random number
				rte_memcpy(con_buf + 4, node->nid, 16); // nid of provider
				rte_memcpy(con_buf + 20, get_pt->L_sid, 20);
				rte_memcpy(con_buf + 40, get_pt->nid, 16); // nid of consumer
				uint32_t pids_len = GET_pids(pkt_data, con_buf + 56);

				CONTROL_init(pkt_buf, CONTROL_SYN_BR_SID, 56 + pids_len, con_buf);
				CONTROL_update_checksum(pkt_buf);

				rm_send_new_ip_pkt(qconf, pkt_buf, CoLoR_get_pkg_length(pkt_buf),
						src_ip_br, dst_ip_br, port_mac_addr_br.addr_bytes, dst_port_br);
				rte_free(pkt_buf);
				rte_free(con_buf);
			}
		}
	}
	else // 提供者在其他域，将GET包转发给BR
	{
		rm_metrics_add(RM_METRIC_GET_TO_OUTSIDE, 1);

		br_info_t *node = (br_info_t *)sid_info->node_pt;
		dst_port = node->port;
		dst_ip = node->ip_addr;

	}

	uint8_t *dst_mac;
	struct rte_ether_addr port_mac_addr;
	int c_len = CoLoR_get_pkg_length(pkt_data);
	// update mbuf
	m->data_len = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + c_len;
	m->pkt_len = m->data_len;
	my_update_ip_header(ip_layer_pt, my_as_info->port_ip[dst_port], dst_ip, c_len);

	uint32_t next_hop_ip;
	uint16_t out_port;
	int ret = 0;
	ret = longest_prefix_match(dst_ip, &next_hop_ip, &out_port);
	if(ret == -1) next_hop_ip = dst_ip;//如果找不到路由表项则认为下一跳地址就是目的地址

	// find mac-addr and update eth header
	if(NULL == (dst_mac = ip_mac_store_search(next_hop_ip)))
	{
		// if unable to find ip-mac, store pkt
        if (unsend_buf_add(dst_ip, next_hop_ip, dst_port, m) != 0) {
            rm_metrics_record_result(RM_PACKET_GET, RM_PACKET_RESULT_NO_MEMORY);
            return;
        }

		// send ARP request to require ip-mac
		rte_eth_macaddr_get(dst_port, &port_mac_addr);
        struct rte_mbuf *arp_pkt = construct_arp_req(my_as_info->port_ip[dst_port], next_hop_ip, port_mac_addr.addr_bytes);
        if (arp_pkt == NULL)
            rm_metrics_record_result(RM_PACKET_GET, RM_PACKET_RESULT_NO_MEMORY);
        else {
            send_single_packet(qconf, arp_pkt, dst_port);
            rm_metrics_record_result(RM_PACKET_GET, RM_PACKET_RESULT_OK);
        }
        return;
	}
	rte_eth_macaddr_get(dst_port, &port_mac_addr);
	my_setup_ether_header(eth_layer_pt, port_mac_addr.addr_bytes, dst_mac);
	send_single_packet(qconf, m, dst_port);
	rm_metrics_record_result(RM_PACKET_GET, RM_PACKET_RESULT_OK);
}

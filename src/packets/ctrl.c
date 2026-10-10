//
// Created by lcf2001523@gmail.com on 25-5-16.
//
#include "packets/ctrl.h"
#include "telemetry/rm_log.h"
#include "telemetry/rm_metrics.h"

void
process_control_packet(struct rte_mbuf *m, uint8_t portid, struct lcore_conf *qconf)
{
	rm_metrics_record_rx(RM_PACKET_CONTROL);
	// process CONCROL
	struct rte_ipv4_hdr *ip_layer_pt = (struct rte_ipv4_hdr *)rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
	uint8_t *pkt_data = rte_pktmbuf_mtod_offset(m, uint8_t *, sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
	int control_result = rm_CONTROL_process(pkt_data, portid);
	if (control_result < 0) {
		rm_metrics_record_result(RM_PACKET_CONTROL,
			control_result == RM_ERR_CHECKSUM_UNPASSED ?
			RM_PACKET_RESULT_BAD_CHECKSUM : RM_PACKET_RESULT_MALFORMED);
		rte_pktmbuf_free(m);
		return;
	}
	rm_packet_result_t packet_result =
		control_result == CONTROL_UNKNOWN_TAG ?
		RM_PACKET_RESULT_UNKNOWN : RM_PACKET_RESULT_OK;
	switch(control_result)
	{
		case CONTROL_CONFIG_RM:
		{
			store_control_rm_config("./config_recieve_file", pkt_data);
			send_config_br(qconf);
			send_control_config_rm_ack(portid, rte_cpu_to_be_32(ip_layer_pt->src_addr), qconf); // todo cpu_to_be??
			RM_LOG_INFO(RM_LOG_MODULE_CONTROL, RM_EVENT_CONTROL_CONFIG_APPLIED,
				portid, "RM configuration applied");
			break;
		}
		case CONTROL_REG_PROXY:
		{
			// todo 如果有很多proxy，需要分成多个包发送
			uint8_t *pkt_buf = rte_malloc(NULL, 512, 0);
			uint8_t *con_data = rte_malloc(NULL, 512, 0);
			uint8_t *pt;
			if (pkt_buf == NULL || con_data == NULL) {
				rte_free(pkt_buf);
				rte_free(con_data);
				packet_result = RM_PACKET_RESULT_NO_MEMORY;
				RM_LOG_ERROR(RM_LOG_MODULE_CONTROL, RM_EVENT_RESOURCE_EXHAUSTED,
					portid, "unable to allocate proxy registration response");
				break;
			}

			CONTROL_front_t *con_pt = (CONTROL_front_t *)pkt_data;
			uint32_t reg_ip = *(uint32_t *)(pkt_data + con_pt->header_len);

			br_info_t *br_buf[64];
			proxy_info_t *proxy_buf[128]; // todo proxy数量最多为128，应动态分配

			// ret config to reg proxy
			// construct nid-ip list
			// num [NID IP]n
			int num_proxy = proxy_store_ret_node_list(proxy_buf);
			*con_data = num_proxy;
			pt = con_data + 1;
			for(int i = 0; i < num_proxy; i++)
			{
				*(uint32_t *)(pt) = proxy_buf[i]->ip_addr;
				rte_memcpy(pt + 4, proxy_buf[i]->nid, 16);
				pt += 20;
			}
			// construct px-ip list
			// num [PX IP]n
			int num_path = as_store_ret_as_px_ip_list(pkt_buf);
			*pt++ = num_path;
			for(int i = 0; i < num_path; i++)
			{
				*(uint16_t *)pt = *(uint16_t *)(pkt_buf + i * 13 + 7); // px
				*(uint32_t *)(pt + 2) = *(uint32_t *)(pkt_buf + i * 13 + 9); // IP address
				//*(uint32_t *)pt = *(uint32_t *)(pkt_buf + i * 13); // AS ID
				//*(uint8_t *)(pt + 4) = *(uint8_t *)(pkt_buf + i * 13 + 4); // prefix length
				//*(uint32_t *)(pt + 5) = *(uint32_t *)(pkt_buf + i * 13 + 5); // px
				//*(uint32_t *)(pt + 9) = *(uint32_t *)(pkt_buf + i * 13 + 9); // IP address
				//pt += 13;
				pt += 6;
			}
			int con_len = pt - con_data;

			// send ret CON back to proxy
			CONTROL_init(pkt_buf, CONTROL_RET_PROXY, con_len, con_data);
			CONTROL_update_checksum(pkt_buf);
			uint8_t dst_port = portid;
			uint32_t src_ip = my_as_info->port_ip[dst_port];
			uint32_t dst_ip = rte_cpu_to_be_32(ip_layer_pt->src_addr);
			struct rte_ether_addr port_mac_addr;
			rte_eth_macaddr_get(dst_port, &port_mac_addr);
			rm_send_new_ip_pkt(qconf, pkt_buf, CoLoR_get_pkg_length(pkt_buf),
					src_ip, dst_ip, port_mac_addr.addr_bytes, dst_port);

			CONTROL_init(pkt_buf, CONTROL_SYN_CONFIG_PROXY, 20, pkt_data + sizeof(CONTROL_front_t));
			CONTROL_update_checksum(pkt_buf);
			// SYN proxy config to other proxys
			// IP NID
			// int num_proxy = proxy_store_ret_node_list(proxy_buf);
			for(int i = 0; i < num_proxy; i++)
			{
				if(reg_ip == proxy_buf[i]->ip_addr)
					continue;
				uint8_t dst_port = proxy_buf[i]->port;
				src_ip = my_as_info->port_ip[dst_port];
				dst_ip = proxy_buf[i]->ip_addr;
				rte_eth_macaddr_get(dst_port, &port_mac_addr);

				rm_send_new_ip_pkt(qconf, pkt_buf, CoLoR_get_pkg_length(pkt_buf),
					src_ip, dst_ip, port_mac_addr.addr_bytes, dst_port);
			}

			// SYN proxy config to BRs
			// IP NID
			int num_br = br_store_ret_node_list(br_buf);
			for(int i = 0; i < num_br; i++)
			{
				uint8_t dst_port = br_buf[i]->port;
				uint32_t src_ip = my_as_info->port_ip[dst_port];
				uint32_t dst_ip = br_buf[i]->ip_addr;
				rte_eth_macaddr_get(dst_port, &port_mac_addr);

				rm_send_new_ip_pkt(qconf, pkt_buf, CoLoR_get_pkg_length(pkt_buf),
					src_ip, dst_ip, port_mac_addr.addr_bytes, dst_port);
			}

			rte_free(pkt_buf);
			rte_free(con_data);
			break;
		}
		case CONTROL_STRATEGY_GLOBAL:
		{
			break;
		}
		case CONTROL_GLOBAL_TOPOLOGY_INFO:
		{
			RM_LOG_INFO(RM_LOG_MODULE_CONTROL, RM_EVENT_CONTROL_CONFIG_APPLIED,
				portid, "global topology updated");
			break;
		}
		case STAT_ANN_INFO:
		{
			void *pkt_buf = rte_malloc(NULL, 512, 0);
			void *con_data = rte_malloc(NULL, 512, 0);
			if (pkt_buf == NULL || con_data == NULL) {
				rte_free(pkt_buf);
				rte_free(con_data);
				packet_result = RM_PACKET_RESULT_NO_MEMORY;
				RM_LOG_ERROR(RM_LOG_MODULE_CONTROL, RM_EVENT_RESOURCE_EXHAUSTED,
					portid, "unable to allocate ANN status response");
				break;
			}

			uint8_t dst_port = portid;
			uint32_t src_ip = my_as_info->port_ip[dst_port];
			uint32_t dst_ip = rte_cpu_to_be_32(ip_layer_pt->src_addr);
			struct rte_ether_addr port_mac_addr;
			rte_eth_macaddr_get(dst_port, &port_mac_addr);

			int check = status_construct(my_as_info->my_status, con_data, ANN_STAT_TYPE);
			if (check < 0) {
				packet_result = RM_PACKET_RESULT_INTERNAL_ERROR;
				rte_free(pkt_buf);
				rte_free(con_data);
				break;
			}
			CONTROL_init(pkt_buf, RET_STAT_ANN_INFO, check, con_data);
			CONTROL_update_checksum(pkt_buf);

			rm_send_new_ip_pkt(qconf, pkt_buf, CoLoR_get_pkg_length(pkt_buf),
					src_ip, dst_ip, port_mac_addr.addr_bytes, dst_port);

			rte_free(pkt_buf);
			rte_free(con_data);
			break;
		}
		case STAT_GET_INFO:
		{
			void *pkt_buf = rte_malloc(NULL, 512, 0);
			void *con_data = rte_malloc(NULL, 512, 0);
			if (pkt_buf == NULL || con_data == NULL) {
				rte_free(pkt_buf);
				rte_free(con_data);
				packet_result = RM_PACKET_RESULT_NO_MEMORY;
				RM_LOG_ERROR(RM_LOG_MODULE_CONTROL, RM_EVENT_RESOURCE_EXHAUSTED,
					portid, "unable to allocate GET status response");
				break;
			}

			uint8_t dst_port = portid;
			uint32_t src_ip = my_as_info->port_ip[dst_port];
			uint32_t dst_ip = rte_cpu_to_be_32(ip_layer_pt->src_addr);
			struct rte_ether_addr port_mac_addr;
			rte_eth_macaddr_get(dst_port, &port_mac_addr);

			int check = status_construct(my_as_info->my_status, con_data, GET_STAT_TYPE);
			if (check < 0) {
				packet_result = RM_PACKET_RESULT_INTERNAL_ERROR;
				rte_free(pkt_buf);
				rte_free(con_data);
				break;
			}
			CONTROL_init(pkt_buf, RET_STAT_GET_INFO, check, con_data);
			CONTROL_update_checksum(pkt_buf);

			rm_send_new_ip_pkt(qconf, pkt_buf, CoLoR_get_pkg_length(pkt_buf),
					src_ip, dst_ip, port_mac_addr.addr_bytes, dst_port);

			rte_free(pkt_buf);
			rte_free(con_data);
			break;
		}
		case STAT_PROXY_INFO:
		{
			void *pkt_buf = rte_malloc(NULL, 512, 0);
			void *con_data = rte_malloc(NULL, 512, 0);
			if (pkt_buf == NULL || con_data == NULL) {
				rte_free(pkt_buf);
				rte_free(con_data);
				packet_result = RM_PACKET_RESULT_NO_MEMORY;
				RM_LOG_ERROR(RM_LOG_MODULE_CONTROL, RM_EVENT_RESOURCE_EXHAUSTED,
					portid, "unable to allocate proxy status response");
				break;
			}
			struct rte_ether_addr port_mac_addr;

			uint8_t dst_port = portid;
			uint32_t src_ip = my_as_info->port_ip[dst_port];
			uint32_t dst_ip = rte_cpu_to_be_32(ip_layer_pt->src_addr);
			rte_eth_macaddr_get(dst_port, &port_mac_addr);

			int check = status_construct(my_as_info->my_status, con_data, PROXY_STAT_TYPE);
			if (check < 0) {
				packet_result = RM_PACKET_RESULT_INTERNAL_ERROR;
				rte_free(pkt_buf);
				rte_free(con_data);
				break;
			}
			CONTROL_init(pkt_buf, RET_STAT_PROXY_INFO, check, con_data);
			CONTROL_update_checksum(pkt_buf);

			rm_send_new_ip_pkt(qconf, pkt_buf, CoLoR_get_pkg_length(pkt_buf),
					src_ip, dst_ip, port_mac_addr.addr_bytes, dst_port);

			rte_free(pkt_buf);
			rte_free(con_data);
			break;
		}
		case CONTROL_UNKNOWN_TAG:
		{
			RM_LOG_WARN(RM_LOG_MODULE_CONTROL, RM_EVENT_CONTROL_UNKNOWN,
				portid, "unknown CONTROL tag");
		}
	}
	rm_metrics_record_result(RM_PACKET_CONTROL, packet_result);
	rte_pktmbuf_free(m);
}

//
// Created by lcf2001523@gmail.com on 25-5-16.
//
#include "packets/ctrl.h"
#include "commandline/cli.h"

void
process_control_packet(struct rte_mbuf *m, uint8_t portid, struct lcore_conf *qconf)
{
	CLR_SCREEN();
	cli_print(YELLOW " $$$$$$\\  $$$$$$$$\\ $$$$$$$\\  $$\\       \n");
	cli_print(YELLOW "$$  __$$\\ \\__$$  __|$$  __$$\\ $$ |      \n");
	cli_print(YELLOW "$$ /  \\__|   $$ |   $$ |  $$ |$$ |      \n");
	cli_print(YELLOW "$$ |         $$ |   $$$$$$$  |$$ |      \n");
	cli_print(YELLOW "$$ |         $$ |   $$  __$$< $$ |      \n");
	cli_print(YELLOW "$$ |  $$\\    $$ |   $$ |  $$ |$$ |      \n");
	cli_print(YELLOW "\\$$$$$$  |   $$ |   $$ |  $$ |$$$$$$$$\\ \n");
	cli_print(YELLOW " \\______/    \\__|   \\__|  \\__|\\________|\n");
	cli_print(RESET ""); 
	cli_print("Receive a CONTROL packet\n");
	// process CONCROL
	struct rte_ipv4_hdr *ip_layer_pt = (struct rte_ipv4_hdr *)rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
	uint8_t *pkt_data = rte_pktmbuf_mtod_offset(m, uint8_t *, sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
	switch(rm_CONTROL_process(pkt_data, portid))
	{
		case CONTROL_CONFIG_RM:
		{
			cli_print("[debug] received a CONTROL_CONFIG_RM");
			as_store_show();
			basic_info_show();
			store_control_rm_config("./config_recieve_file", pkt_data);
			send_config_br(qconf);
			send_control_config_rm_ack(portid, rte_cpu_to_be_32(ip_layer_pt->src_addr), qconf); // todo cpu_to_be??
			break;
		}
		case CONTROL_REG_PROXY:
		{
			// todo 如果有很多proxy，需要分成多个包发送
			uint8_t *pkt_buf = rte_malloc(NULL, 512, 0);
			uint8_t *con_data = rte_malloc(NULL, 512, 0);
			uint8_t *pt;

			CONTROL_front_t *con_pt = (CONTROL_front_t *)pkt_data;
			uint32_t reg_ip = *(uint32_t *)(pkt_data + con_pt->header_len);

			br_info_t *br_buf[64];
			proxy_info_t *proxy_buf[128]; // todo proxy数量最多为128，应动态分配

			// ret config to reg proxy
			// construct nid-ip list
			// num [NID IP]n
			cli_print("Start sending PROXY_RET\n");
			int num_proxy = proxy_store_ret_node_list(proxy_buf);
			cli_print("Read %d proxy node\n", num_proxy);
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
			cli_print("Read %d as-px-ip list\n", num_path);
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
			cli_print("Start sending PROXY_SYN to %d proxy\n", num_proxy);
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
			cli_print("Start sending PROXY_SYN to %d br\n", num_br);
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
			cli_print("Returning CONTROL_STRATEGY_GLOBAL_ACK to sender\n");
			break;
			struct rte_ether_addr port_mac_addr_ret;

			uint8_t dst_port = portid;
			uint32_t src_ip = my_as_info->port_ip[dst_port];
			uint32_t dst_ip = rte_cpu_to_be_32(ip_layer_pt->src_addr);
			rte_eth_macaddr_get(dst_port, &port_mac_addr_ret);

			void *pkt_buf = rte_malloc(NULL, 128, 0);
			uint8_t con_buf;

			con_buf = ACK_SUCCESS;
			CONTROL_init(pkt_buf, CONTROL_STRATEGY_GLOBAL_ACK, 1, con_buf);
			CONTROL_update_checksum(pkt_buf);

			rm_send_new_ip_pkt(qconf, pkt_buf, CoLoR_get_pkg_length(pkt_buf),
					src_ip, dst_ip, port_mac_addr_ret.addr_bytes, dst_port);

			rte_free(pkt_buf);
			break;
		}
		case CONTROL_GLOBAL_TOPOLOGY_INFO:
		{
			cli_print("Receive a global topology info\n");
			graph_show(my_as_info->as_graph);
			break;
		}
		case STAT_ANN_INFO:
		{
			void *pkt_buf = rte_malloc(NULL, 512, 0);
			void *con_data = rte_malloc(NULL, 512, 0);
			struct rte_ether_addr port_mac_addr_ret;

			uint8_t dst_port = portid;
			uint32_t src_ip = my_as_info->port_ip[dst_port];
			uint32_t dst_ip = rte_cpu_to_be_32(ip_layer_pt->src_addr);
			struct rte_ether_addr port_mac_addr;
			rte_eth_macaddr_get(dst_port, &port_mac_addr);

			int check = status_construct(my_as_info->my_status, con_data, ANN_STAT_TYPE);
			show_segment(con_data, check);
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
			struct rte_ether_addr port_mac_addr_ret;

			uint8_t dst_port = portid;
			uint32_t src_ip = my_as_info->port_ip[dst_port];
			uint32_t dst_ip = rte_cpu_to_be_32(ip_layer_pt->src_addr);
			struct rte_ether_addr port_mac_addr;
			rte_eth_macaddr_get(dst_port, &port_mac_addr);

			int check = status_construct(my_as_info->my_status, con_data, GET_STAT_TYPE);
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
			struct rte_ether_addr port_mac_addr;

			uint8_t dst_port = portid;
			uint32_t src_ip = my_as_info->port_ip[dst_port];
			uint32_t dst_ip = rte_cpu_to_be_32(ip_layer_pt->src_addr);
			rte_eth_macaddr_get(dst_port, &port_mac_addr);

			int check = status_construct(my_as_info->my_status, con_data, PROXY_STAT_TYPE);
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
			cli_print("[?] Unknown for CONTROL\n");
		}
	}
	rte_pktmbuf_free(m);
}
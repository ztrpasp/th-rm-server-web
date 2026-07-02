//
// Created by lcf2001523@gmail.com on 25-5-16.
//
#include "packets/ann.h"
#include "commandline/cli.h"

void
process_ann_packet(struct rte_mbuf *m, uint8_t portid, struct lcore_conf *qconf)
{
    CLR_SCREEN();
    cli_print(GREEN " $$$$$$\\  $$\\   $$\\ $$\\   $$\\ \n");
    cli_print(GREEN "$$  __$$\\ $$$\\  $$ |$$$\\  $$ |\n");
    cli_print(GREEN "$$ /  $$ |$$$$\\ $$ |$$$$\\ $$ |\n");
    cli_print(GREEN "$$$$$$$$ |$$ $$\\$$ |$$ $$\\$$ |\n");
    cli_print(GREEN "$$  __$$ |$$ \\$$$$ |$$ \\$$$$ |\n");
    cli_print(GREEN "$$ |  $$ |$$ |\\$$$ |$$ |\\$$$ |\n");
    cli_print(GREEN "$$ |  $$ |$$ | \\$$ |$$ | \\$$ |\n");
    cli_print(GREEN "\\__|  \\__|\\__|  \\__|\\__|  \\__|\n");
    cli_print(RESET "");
    cli_print("Receive an ANN packet\n");

    uint8_t *pkt_data = rte_pktmbuf_mtod_offset(m, uint8_t *, sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
    // mid-data
    mark_t mark_array[32];
    search_px_result_t as_px_result;
    px_unit_t px_unit_array[32];
    void *new_ann_array[32];

    // ANN stage 1 : parse and mark ANN format
    if(RM_PROCESS_SUCCESS != rm_ANN_parse_store(pkt_data, mark_array, &as_px_result))
    {
        // STATUS
        my_as_info->my_status->ANN_stat.filtered_count++;
        rte_pktmbuf_free(m);
        cli_print("The ANN packet is filtered(dropped).\n");
        return;
    }

    // ANN stage 2 : decision on forwarding
    int num_px_units = rm_ANN_forward_px_units(pkt_data, mark_array, &as_px_result, px_unit_array);
    if(num_px_units <= 0)
    {
        rte_pktmbuf_free(m);
        return;
    }

    show_px_unit_array(px_unit_array);

    // ANN stage 3 :
    for(int i = 0; i < num_px_units; i++)
        new_ann_array[i] = rte_malloc(NULL, 256, 0);
    rm_ANN_construct_ann(pkt_data, mark_array, px_unit_array, new_ann_array);

    // STATUS
    my_as_info->my_status->ANN_stat.outbound_count += num_px_units;

    // broadcast
    for(int i = 0; i < num_px_units; i++)
    {
        int c_len = CoLoR_get_pkg_length(new_ann_array[i]);
        uint8_t dst_port = px_unit_array[i].br_pt->port;
        uint32_t src_ip = my_as_info->port_ip[dst_port];
        uint32_t dst_ip = px_unit_array[i].br_pt->ip_addr;
        struct rte_ether_addr port_mac_addr;
        rte_eth_macaddr_get(dst_port, &port_mac_addr);

        rm_send_new_ip_pkt(qconf, new_ann_array[i], c_len,
            src_ip, dst_ip, port_mac_addr.addr_bytes, dst_port);

        rte_free(new_ann_array[i]);
    }

    rte_pktmbuf_free(m);
}
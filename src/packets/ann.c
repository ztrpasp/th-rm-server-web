//
// Created by lcf2001523@gmail.com on 25-5-16.
//
#include "packets/ann.h"
#include "telemetry/rm_log.h"
#include "telemetry/rm_metrics.h"

static rm_packet_result_t
ann_metric_result(int result)
{
    if (result == RM_ERR_CHECKSUM_UNPASSED)
        return RM_PACKET_RESULT_BAD_CHECKSUM;
    if (result == RM_ERR_NO_MEMORY)
        return RM_PACKET_RESULT_NO_MEMORY;
    if (result == RM_ERR_NULL_PT_INPUT)
        return RM_PACKET_RESULT_MALFORMED;
    return result == RM_PROCESS_SUCCESS ? RM_PACKET_RESULT_OK :
        RM_PACKET_RESULT_INTERNAL_ERROR;
}

void
process_ann_packet(struct rte_mbuf *m, uint8_t portid, struct lcore_conf *qconf)
{
    rm_metrics_record_rx(RM_PACKET_ANN);

    uint8_t *pkt_data = rte_pktmbuf_mtod_offset(m, uint8_t *, sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
    // mid-data
    mark_t mark_array[32];
    search_px_result_t as_px_result;
    px_unit_t px_unit_array[32];
    void *new_ann_array[32];

    // ANN stage 1 : parse and mark ANN format
    int parse_result = rm_ANN_parse_store(pkt_data, mark_array, &as_px_result);
    if(RM_PROCESS_SUCCESS != parse_result)
    {
        rm_metrics_record_result(RM_PACKET_ANN, ann_metric_result(parse_result));
        rte_pktmbuf_free(m);
        return;
    }

    // ANN stage 2 : decision on forwarding
    int num_px_units = rm_ANN_forward_px_units(pkt_data, mark_array, &as_px_result, px_unit_array);
    if(num_px_units <= 0)
    {
        rm_metrics_record_result(RM_PACKET_ANN,
            num_px_units < 0 ? RM_PACKET_RESULT_INTERNAL_ERROR : RM_PACKET_RESULT_OK);
        rte_pktmbuf_free(m);
        return;
    }

    // ANN stage 3 :
    for(int i = 0; i < num_px_units; i++) {
        new_ann_array[i] = rte_malloc(NULL, 256, 0);
        if (new_ann_array[i] == NULL) {
            for (int j = 0; j < i; j++)
                rte_free(new_ann_array[j]);
            rm_metrics_record_result(RM_PACKET_ANN, RM_PACKET_RESULT_NO_MEMORY);
            RM_LOG_ERROR(RM_LOG_MODULE_ANN, RM_EVENT_RESOURCE_EXHAUSTED,
                portid, "unable to allocate forwarded ANN buffer");
            rte_pktmbuf_free(m);
            return;
        }
    }
    rm_ANN_construct_ann(pkt_data, mark_array, px_unit_array, new_ann_array);

    rm_metrics_add(RM_METRIC_ANN_OUTBOUND, num_px_units);

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

    rm_metrics_record_result(RM_PACKET_ANN, RM_PACKET_RESULT_OK);
    rte_pktmbuf_free(m);
}

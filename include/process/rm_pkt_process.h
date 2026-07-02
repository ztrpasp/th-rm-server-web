#ifndef __RM_PKT_PROCESS__
#define __RM_PKT_PROCESS__

#include "packets/rte_CoLoR.h"
#include "tables/rm_tables.h"
#include "process/send.h"

typedef struct mark
{
    bool filtered;
    void *unit_head; // null to end
    void *N_pt;
    void *L_pt;
    void *nid_pt;
    strategy_t strategy_pt[32]; // .tag = 0 and .len = 0 to end
} mark_t;

typedef struct px_unit
{
    uint16_t px;
    br_info_t *br_pt; // br to send, br = null to end
    uint8_t unit_to_forward[32]; // 255 to end array
} px_unit_t;

#define RM_PROCESS_SUCCESS 1
#define RM_ERR_NULL_PT_INPUT -1
#define RM_ERR_CHECKSUM_UNPASSED -2
#define RM_GET_SID_UNFOUND -3
#define RM_GET_FILTERED -4
#define RM_PROCESS_ERR -5
#define RM_RETURN_SID_LIST -6


// ANN process
int rm_ANN_parse_store(void *pkt, mark_t *mark_array, search_px_result_t *src_px_res);
int rm_ANN_forward_px_units(void *pkt, mark_t *mark_array, search_px_result_t *src_px_res, px_unit_t *px_unit_array);
int rm_ANN_construct_ann(void *pkt, mark_t *mark_array, px_unit_t *px_units, void *ann_buf[32]);

// GET process
sid_info_t *rm_GET_search(void *pkt);
int rm_GET_process(void *pkt, sid_info_t **node_pt, struct rte_ipv4_hdr *ip_layer_pt, struct lcore_conf *qconf);

// CONTROL process
int rm_CONTROL_process(pkt_data, dst_port);

// TEST
void show_mark_t_array(mark_t *ans);
void show_px_unit_array(px_unit_t *ans);



#endif
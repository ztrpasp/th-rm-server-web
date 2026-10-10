#ifndef RM_METRICS_H
#define RM_METRICS_H

#include <stddef.h>
#include <stdint.h>

#define RM_TRACE_AS_COUNT 256

typedef enum rm_packet_type {
    RM_PACKET_ANN = 0,
    RM_PACKET_GET,
    RM_PACKET_CONTROL,
    RM_PACKET_ARP,
    RM_PACKET_TYPE_COUNT
} rm_packet_type_t;

typedef enum rm_packet_result {
    RM_PACKET_RESULT_OK = 0,
    RM_PACKET_RESULT_BAD_CHECKSUM,
    RM_PACKET_RESULT_MALFORMED,
    RM_PACKET_RESULT_SID_NOT_FOUND,
    RM_PACKET_RESULT_ACL_DENIED,
    RM_PACKET_RESULT_POLICY_DENIED,
    RM_PACKET_RESULT_NO_ROUTE,
    RM_PACKET_RESULT_NO_MEMORY,
    RM_PACKET_RESULT_SID_LIST_RETURNED,
    RM_PACKET_RESULT_INTERNAL_ERROR,
    RM_PACKET_RESULT_UNKNOWN,
    RM_PACKET_RESULT_COUNT
} rm_packet_result_t;

typedef enum rm_metric_id {
    RM_METRIC_RX_PACKETS = 0,
    RM_METRIC_RX_NON_IPV4,
    RM_METRIC_RX_NON_COLOR,
    RM_METRIC_RX_UNSUPPORTED,
    RM_METRIC_TX_PACKETS,
    RM_METRIC_TX_DROPPED,
    RM_METRIC_ANN_FROM_INSIDE,
    RM_METRIC_ANN_FROM_OUTSIDE,
    RM_METRIC_ANN_OUTBOUND,
    RM_METRIC_ANN_FILTERED,
    RM_METRIC_GET_FROM_INSIDE,
    RM_METRIC_GET_FROM_OUTSIDE,
    RM_METRIC_GET_TO_INSIDE,
    RM_METRIC_GET_TO_OUTSIDE,
    RM_METRIC_GET_FILTERED,
    RM_METRIC_PROXY_REGISTERED,
    RM_METRIC_LOG_DROPPED,
    RM_METRIC_LOG_SUPPRESSED,
    RM_METRIC_COUNT
} rm_metric_id_t;

typedef struct rm_metrics_snapshot {
    uint64_t captured_at_ns;
    uint64_t generation;
    uint64_t counters[RM_METRIC_COUNT];
    uint64_t packet_rx[RM_PACKET_TYPE_COUNT];
    uint64_t packet_results[RM_PACKET_TYPE_COUNT][RM_PACKET_RESULT_COUNT];
    uint64_t ann_trace[RM_TRACE_AS_COUNT];
    uint64_t get_trace[RM_TRACE_AS_COUNT];
} rm_metrics_snapshot_t;

void rm_metrics_init(void);
void rm_metrics_add(rm_metric_id_t metric, uint64_t value);
void rm_metrics_record_rx(rm_packet_type_t type);
void rm_metrics_record_result(rm_packet_type_t type, rm_packet_result_t result);
void rm_metrics_record_trace(rm_packet_type_t type, uint8_t aid);
int rm_metrics_read_snapshot(rm_metrics_snapshot_t *out);
const char *rm_packet_type_name(rm_packet_type_t type);
const char *rm_packet_result_name(rm_packet_result_t result);

#endif

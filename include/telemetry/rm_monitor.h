#ifndef RM_MONITOR_H
#define RM_MONITOR_H

#include <stddef.h>
#include <stdint.h>

#include <rte_ethdev.h>

#include "telemetry/rm_log.h"
#include "telemetry/rm_metrics.h"

#define RM_MONITOR_LOG_LIMIT_DEFAULT 100U
#define RM_MONITOR_LOG_LIMIT_MAX 256U

typedef struct rm_monitor_port_snapshot {
    uint16_t port_id;
    uint8_t link_up;
    uint32_t link_speed_mbps;
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t rx_errors;
    uint64_t rx_missed;
    uint64_t rx_no_mbuf;
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t tx_errors;
} rm_monitor_port_snapshot_t;

typedef struct rm_monitor_snapshot {
    uint64_t captured_at_ns;
    rm_metrics_snapshot_t metrics;
    rm_log_stats_t logs;
    uint16_t port_count;
    rm_monitor_port_snapshot_t ports[RTE_MAX_ETHPORTS];
} rm_monitor_snapshot_t;

int rm_monitor_snapshot_read(rm_monitor_snapshot_t *out);
int rm_monitor_snapshot_json(const rm_monitor_snapshot_t *snapshot,
    char *out, size_t capacity, size_t *required);
int rm_monitor_logs_json(uint64_t after_sequence, size_t limit,
    char *out, size_t capacity, size_t *required);

#endif

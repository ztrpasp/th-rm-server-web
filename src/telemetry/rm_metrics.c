#include "telemetry/rm_metrics.h"

#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include <rte_common.h>
#include <rte_lcore.h>

typedef struct rm_lcore_metrics {
    _Atomic uint64_t counters[RM_METRIC_COUNT];
    _Atomic uint64_t packet_rx[RM_PACKET_TYPE_COUNT];
    _Atomic uint64_t packet_results[RM_PACKET_TYPE_COUNT][RM_PACKET_RESULT_COUNT];
    _Atomic uint64_t ann_trace[RM_TRACE_AS_COUNT];
    _Atomic uint64_t get_trace[RM_TRACE_AS_COUNT];
} __rte_cache_aligned rm_lcore_metrics_t;

/* The final slot is used by non-EAL threads. */
static rm_lcore_metrics_t metrics_by_lcore[RTE_MAX_LCORE + 1];
static _Atomic uint64_t snapshot_generation;

static unsigned
metrics_slot(void)
{
    unsigned lcore_id = rte_lcore_id();
    return lcore_id < RTE_MAX_LCORE ? lcore_id : RTE_MAX_LCORE;
}

static uint64_t
realtime_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void
rm_metrics_init(void)
{
    unsigned slot;
    unsigned metric;
    unsigned type;
    unsigned result;
    unsigned aid;

    for (slot = 0; slot <= RTE_MAX_LCORE; slot++) {
        for (metric = 0; metric < RM_METRIC_COUNT; metric++)
            atomic_init(&metrics_by_lcore[slot].counters[metric], 0);
        for (type = 0; type < RM_PACKET_TYPE_COUNT; type++) {
            atomic_init(&metrics_by_lcore[slot].packet_rx[type], 0);
            for (result = 0; result < RM_PACKET_RESULT_COUNT; result++)
                atomic_init(&metrics_by_lcore[slot].packet_results[type][result], 0);
        }
        for (aid = 0; aid < RM_TRACE_AS_COUNT; aid++) {
            atomic_init(&metrics_by_lcore[slot].ann_trace[aid], 0);
            atomic_init(&metrics_by_lcore[slot].get_trace[aid], 0);
        }
    }
    atomic_init(&snapshot_generation, 0);
}

void
rm_metrics_add(rm_metric_id_t metric, uint64_t value)
{
    if ((unsigned)metric >= RM_METRIC_COUNT)
        return;
    atomic_fetch_add_explicit(&metrics_by_lcore[metrics_slot()].counters[metric],
        value, memory_order_relaxed);
}

void
rm_metrics_record_rx(rm_packet_type_t type)
{
    if ((unsigned)type >= RM_PACKET_TYPE_COUNT)
        return;
    atomic_fetch_add_explicit(&metrics_by_lcore[metrics_slot()].packet_rx[type],
        1, memory_order_relaxed);
}

void
rm_metrics_record_result(rm_packet_type_t type, rm_packet_result_t result)
{
    if ((unsigned)type >= RM_PACKET_TYPE_COUNT ||
        (unsigned)result >= RM_PACKET_RESULT_COUNT)
        return;

    atomic_fetch_add_explicit(
        &metrics_by_lcore[metrics_slot()].packet_results[type][result],
        1, memory_order_relaxed);

    if (result == RM_PACKET_RESULT_BAD_CHECKSUM ||
        result == RM_PACKET_RESULT_MALFORMED ||
        result == RM_PACKET_RESULT_SID_NOT_FOUND ||
        result == RM_PACKET_RESULT_ACL_DENIED ||
        result == RM_PACKET_RESULT_POLICY_DENIED ||
        result == RM_PACKET_RESULT_NO_ROUTE ||
        result == RM_PACKET_RESULT_NO_MEMORY ||
        result == RM_PACKET_RESULT_INTERNAL_ERROR ||
        result == RM_PACKET_RESULT_UNKNOWN) {
        if (type == RM_PACKET_ANN)
            rm_metrics_add(RM_METRIC_ANN_FILTERED, 1);
        else if (type == RM_PACKET_GET)
            rm_metrics_add(RM_METRIC_GET_FILTERED, 1);
    }
}

void
rm_metrics_record_trace(rm_packet_type_t type, uint16_t aid)
{
    rm_lcore_metrics_t *slot = &metrics_by_lcore[metrics_slot()];

    if (aid >= RM_TRACE_AS_COUNT)
        return;
    if (type == RM_PACKET_ANN)
        atomic_fetch_add_explicit(&slot->ann_trace[aid], 1, memory_order_relaxed);
    else if (type == RM_PACKET_GET)
        atomic_fetch_add_explicit(&slot->get_trace[aid], 1, memory_order_relaxed);
}

int
rm_metrics_read_snapshot(rm_metrics_snapshot_t *out)
{
    unsigned slot;
    unsigned metric;
    unsigned type;
    unsigned result;
    unsigned aid;

    if (out == NULL)
        return -1;

    memset(out, 0, sizeof(*out));
    for (slot = 0; slot <= RTE_MAX_LCORE; slot++) {
        for (metric = 0; metric < RM_METRIC_COUNT; metric++)
            out->counters[metric] += atomic_load_explicit(
                &metrics_by_lcore[slot].counters[metric], memory_order_relaxed);
        for (type = 0; type < RM_PACKET_TYPE_COUNT; type++) {
            out->packet_rx[type] += atomic_load_explicit(
                &metrics_by_lcore[slot].packet_rx[type], memory_order_relaxed);
            for (result = 0; result < RM_PACKET_RESULT_COUNT; result++)
                out->packet_results[type][result] += atomic_load_explicit(
                    &metrics_by_lcore[slot].packet_results[type][result],
                    memory_order_relaxed);
        }
        for (aid = 0; aid < RM_TRACE_AS_COUNT; aid++) {
            out->ann_trace[aid] += atomic_load_explicit(
                &metrics_by_lcore[slot].ann_trace[aid], memory_order_relaxed);
            out->get_trace[aid] += atomic_load_explicit(
                &metrics_by_lcore[slot].get_trace[aid], memory_order_relaxed);
        }
    }

    out->captured_at_ns = realtime_ns();
    out->generation = atomic_fetch_add_explicit(
        &snapshot_generation, 1, memory_order_relaxed) + 1;
    return 0;
}

const char *
rm_metric_name(rm_metric_id_t metric)
{
    static const char *const names[RM_METRIC_COUNT] = {
        "rx_packets",
        "rx_non_ipv4",
        "rx_non_color",
        "rx_unsupported",
        "tx_packets",
        "tx_dropped",
        "ann_from_inside",
        "ann_from_outside",
        "ann_outbound",
        "ann_filtered",
        "get_from_inside",
        "get_from_outside",
        "get_to_inside",
        "get_to_outside",
        "get_filtered",
        "proxy_registered",
        "log_dropped",
        "log_suppressed"
    };

    return (unsigned)metric < RM_METRIC_COUNT ? names[metric] : "unknown";
}

const char *
rm_packet_type_name(rm_packet_type_t type)
{
    static const char *const names[RM_PACKET_TYPE_COUNT] = {
        "ann", "get", "control", "arp"
    };
    return (unsigned)type < RM_PACKET_TYPE_COUNT ? names[type] : "unknown";
}

const char *
rm_packet_result_name(rm_packet_result_t result)
{
    static const char *const names[RM_PACKET_RESULT_COUNT] = {
        "ok", "bad_checksum", "malformed", "sid_not_found",
        "acl_denied", "policy_denied", "no_route", "no_memory",
        "sid_list_returned", "internal_error", "unknown"
    };
    return (unsigned)result < RM_PACKET_RESULT_COUNT ? names[result] : "unknown";
}

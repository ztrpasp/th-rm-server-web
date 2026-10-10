#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tables/rm_status.h"
#include "telemetry/rm_metrics.h"

/* rm_status.c uses the CLI for display only; tests do not need terminal output. */
void
cli_print(const char *fmt, ...)
{
    (void)fmt;
}

static void
assert_initial_snapshot_is_zero(void)
{
    rm_metrics_snapshot_t snapshot;

    assert(rm_metrics_read_snapshot(&snapshot) == 0);
    for (int i = 0; i < RM_METRIC_COUNT; i++)
        assert(snapshot.counters[i] == 0);
    for (int type = 0; type < RM_PACKET_TYPE_COUNT; type++) {
        assert(snapshot.packet_rx[type] == 0);
        for (int result = 0; result < RM_PACKET_RESULT_COUNT; result++)
            assert(snapshot.packet_results[type][result] == 0);
    }
    for (int aid = 0; aid < RM_TRACE_AS_COUNT; aid++) {
        assert(snapshot.ann_trace[aid] == 0);
        assert(snapshot.get_trace[aid] == 0);
    }
}

static void
assert_get_result_is_counted_once(void)
{
    rm_metrics_snapshot_t snapshot;

    rm_metrics_record_rx(RM_PACKET_GET);
    rm_metrics_add(RM_METRIC_RX_PACKETS, 1);
    rm_metrics_add(RM_METRIC_RX_NON_IPV4, 2);
    rm_metrics_add(RM_METRIC_TX_PACKETS, 3);
    rm_metrics_add(RM_METRIC_TX_DROPPED, 1);
    rm_metrics_add(RM_METRIC_GET_FROM_INSIDE, 1);
    rm_metrics_record_result(RM_PACKET_GET, RM_PACKET_RESULT_ACL_DENIED);
    rm_metrics_record_result(RM_PACKET_GET, RM_PACKET_RESULT_SID_LIST_RETURNED);
    rm_metrics_record_trace(RM_PACKET_GET, 7);
    rm_metrics_record_trace(RM_PACKET_GET, RM_TRACE_AS_COUNT);

    assert(rm_metrics_read_snapshot(&snapshot) == 0);
    assert(snapshot.packet_rx[RM_PACKET_GET] == 1);
    assert(snapshot.counters[RM_METRIC_RX_PACKETS] == 1);
    assert(snapshot.counters[RM_METRIC_RX_NON_IPV4] == 2);
    assert(snapshot.counters[RM_METRIC_TX_PACKETS] == 3);
    assert(snapshot.counters[RM_METRIC_TX_DROPPED] == 1);
    assert(snapshot.packet_results[RM_PACKET_GET][RM_PACKET_RESULT_ACL_DENIED] == 1);
    assert(snapshot.packet_results[RM_PACKET_GET][RM_PACKET_RESULT_SID_LIST_RETURNED] == 1);
    assert(snapshot.counters[RM_METRIC_GET_FILTERED] == 1);
    assert(snapshot.counters[RM_METRIC_GET_FROM_INSIDE] == 1);
    assert(snapshot.get_trace[7] == 1);
}

static void
assert_legacy_status_payload_is_preserved(void)
{
    rm_status_t owner = {0};
    rm_status_t snapshot;
    uint32_t payload[5];

    assert(status_snapshot(&owner, &snapshot) == 0);
    assert(snapshot.GET_stat.filtered_count == 1);
    assert(snapshot.GET_stat.from_inside_count == 1);
    assert(snapshot.GET_stat.trace_res[7] == 1);

    memset(payload, 0, sizeof(payload));
    assert(status_construct(&owner, payload, GET_STAT_TYPE) == 20);
    assert(payload[0] == 1);
    assert(payload[1] == 0);
    assert(payload[2] == 0);
    assert(payload[3] == 0);
    assert(payload[4] == 1);
}

static void
assert_legacy_counters_saturate(void)
{
    rm_status_t owner = {0};
    uint8_t storage[17] = {0};
    uint32_t first_counter;

    rm_metrics_add(RM_METRIC_ANN_FROM_INSIDE, (uint64_t)UINT32_MAX + 1);
    assert(status_construct(&owner, storage + 1, ANN_STAT_TYPE) == 16);
    memcpy(&first_counter, storage + 1, sizeof(first_counter));
    assert(first_counter == UINT32_MAX);
}

int
main(void)
{
    rm_metrics_init();
    assert_initial_snapshot_is_zero();
    assert_get_result_is_counted_once();
    assert_legacy_status_payload_is_preserved();
    assert_legacy_counters_saturate();
    puts("metrics/status tests passed");
    return 0;
}

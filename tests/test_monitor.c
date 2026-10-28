#include <assert.h>
#include <string.h>

#include "telemetry/rm_monitor.h"

static void
test_snapshot_json(void)
{
    rm_monitor_snapshot_t snapshot = {0};
    char output[65536];
    size_t required = 0;

    snapshot.captured_at_ns = 123456789;
    snapshot.metrics.generation = 7;
    snapshot.metrics.counters[RM_METRIC_RX_PACKETS] = 42;
    snapshot.metrics.packet_rx[RM_PACKET_GET] = 9;
    snapshot.logs.history_count = 2;
    snapshot.logs.newest_sequence = 11;
    snapshot.port_count = 1;
    snapshot.ports[0].port_id = 0;
    snapshot.ports[0].link_up = 1;
    snapshot.ports[0].link_speed_mbps = 1000;
    snapshot.ports[0].rx_packets = 10;
    snapshot.ports[0].tx_packets = 8;

    assert(rm_monitor_snapshot_json(&snapshot, NULL, 0, &required) != 0);
    assert(required > 0);
    assert(rm_monitor_snapshot_json(&snapshot, output, sizeof(output),
        &required) == 0);
    assert(strstr(output, "\"captured_at_ns\":123456789") != NULL);
    assert(strstr(output, "\"rx_packets\":42") != NULL);
    assert(strstr(output, "\"port_id\":0") != NULL);
}

static void
test_logs_json(void)
{
    char output[4096];
    size_t required = 0;

    assert(rm_monitor_logs_json(10, 0, output, sizeof(output), &required) == 0);
    assert(strstr(output, "\"after_sequence\":10") != NULL);
    assert(strstr(output, "\"count\":0") != NULL);
    assert(required == strlen(output) + 1);
}

int
main(void)
{
    assert(strcmp(rm_metric_name(RM_METRIC_RX_PACKETS), "rx_packets") == 0);
    assert(strcmp(rm_metric_name(RM_METRIC_COUNT), "unknown") == 0);
    test_snapshot_json();
    test_logs_json();
    return 0;
}

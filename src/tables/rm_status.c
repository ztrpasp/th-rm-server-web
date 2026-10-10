#include "commandline/cli.h"
#include "tables/rm_status.h"
#include "telemetry/rm_metrics.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

rm_status_t *status_create()
{
    return (rm_status_t *)rte_zmalloc("rm_status", sizeof(rm_status_t), 0);
}

void
status_destroy(rm_status_t *st)
{
    rte_free(st);
}

int
status_snapshot(const rm_status_t *st, rm_status_t *out)
{
    rm_metrics_snapshot_t metrics;
    int i;

    if (st == NULL || out == NULL || rm_metrics_read_snapshot(&metrics) != 0)
        return -1;

    memset(out, 0, sizeof(*out));
    out->ANN_stat.from_inside_count = metrics.counters[RM_METRIC_ANN_FROM_INSIDE];
    out->ANN_stat.from_outside_count = metrics.counters[RM_METRIC_ANN_FROM_OUTSIDE];
    out->ANN_stat.outbound_count = metrics.counters[RM_METRIC_ANN_OUTBOUND];
    out->ANN_stat.filtered_count = metrics.counters[RM_METRIC_ANN_FILTERED];
    out->GET_stat.from_inside_count = metrics.counters[RM_METRIC_GET_FROM_INSIDE];
    out->GET_stat.from_outside_count = metrics.counters[RM_METRIC_GET_FROM_OUTSIDE];
    out->GET_stat.to_inside_count = metrics.counters[RM_METRIC_GET_TO_INSIDE];
    out->GET_stat.to_outside_count = metrics.counters[RM_METRIC_GET_TO_OUTSIDE];
    out->GET_stat.filtered_count = metrics.counters[RM_METRIC_GET_FILTERED];
    out->proxy_stat.register_count = metrics.counters[RM_METRIC_PROXY_REGISTERED];
    for (i = 0; i < 256; i++) {
        out->ANN_stat.trace_res[i] = metrics.ann_trace[i];
        out->GET_stat.trace_res[i] = metrics.get_trace[i];
    }
    return 0;
}

void status_show(const rm_status_t *st)
{
    rm_status_t snapshot;
    int i;

    if (status_snapshot(st, &snapshot) != 0) {
        cli_print("Status unavailable\n");
        return;
    }
    cli_print("Status : ANN\nfrom_inside: %" PRIu64 "\nfrom_outside: %" PRIu64
        "\noutbound: %" PRIu64 "\nfiltered: %" PRIu64 "\nTrace result:\n",
        snapshot.ANN_stat.from_inside_count,
        snapshot.ANN_stat.from_outside_count,
        snapshot.ANN_stat.outbound_count,
        snapshot.ANN_stat.filtered_count);
    for(i = 0; i < 256; i++)
    {
        if(snapshot.ANN_stat.trace_res[i] != 0)
        {
            cli_print("AS:\t%d\t--%" PRIu64 "\n", i, snapshot.ANN_stat.trace_res[i]);
        }
    }
    
    cli_print("Status : GET\nfrom_inside: %" PRIu64 "\nfrom_outside: %" PRIu64
        "\nto_inside: %" PRIu64 "\nto_outside: %" PRIu64
        "\nfiltered: %" PRIu64 "\nTrace result:\n",
        snapshot.GET_stat.from_inside_count,
        snapshot.GET_stat.from_outside_count,
        snapshot.GET_stat.to_inside_count,
        snapshot.GET_stat.to_outside_count,
        snapshot.GET_stat.filtered_count);
    for(i = 0; i < 256; i++)
    {
        if(snapshot.GET_stat.trace_res[i] != 0)
        {
            cli_print("AS:\t%d\t--%" PRIu64 "\n", i, snapshot.GET_stat.trace_res[i]);
        }
    }

    cli_print("Status : Proxy\nreg_count: %" PRIu64 "\n",
        snapshot.proxy_stat.register_count);

    {
        rm_metrics_snapshot_t metrics;

        if (rm_metrics_read_snapshot(&metrics) == 0) {
            cli_print("Status : Runtime\nrx_packets: %" PRIu64
                "\nrx_non_ipv4: %" PRIu64
                "\nrx_non_color: %" PRIu64
                "\nrx_unsupported: %" PRIu64
                "\ntx_packets: %" PRIu64
                "\ntx_dropped: %" PRIu64
                "\nlog_dropped: %" PRIu64
                "\nlog_suppressed: %" PRIu64 "\n",
                metrics.counters[RM_METRIC_RX_PACKETS],
                metrics.counters[RM_METRIC_RX_NON_IPV4],
                metrics.counters[RM_METRIC_RX_NON_COLOR],
                metrics.counters[RM_METRIC_RX_UNSUPPORTED],
                metrics.counters[RM_METRIC_TX_PACKETS],
                metrics.counters[RM_METRIC_TX_DROPPED],
                metrics.counters[RM_METRIC_LOG_DROPPED],
                metrics.counters[RM_METRIC_LOG_SUPPRESSED]);
        }
    }
}

static uint32_t
legacy_counter(uint64_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static void
write_legacy_counter(uint8_t *buf, size_t offset, uint64_t value)
{
    uint32_t encoded = legacy_counter(value);

    memcpy(buf + offset, &encoded, sizeof(encoded));
}

int status_construct(const rm_status_t *st, void *buf, uint8_t type)
{
    rm_status_t snapshot;
    uint8_t *output = buf;
    int size = 0;

    if (buf == NULL || status_snapshot(st, &snapshot) != 0)
        return -1;
    switch(type)
    {
        case ANN_STAT_TYPE:
        {
            write_legacy_counter(output, 0, snapshot.ANN_stat.from_inside_count);
            write_legacy_counter(output, 4, snapshot.ANN_stat.from_outside_count);
            write_legacy_counter(output, 8, snapshot.ANN_stat.outbound_count);
            write_legacy_counter(output, 12, snapshot.ANN_stat.filtered_count);
            size = 16;
            break;
        }
        case GET_STAT_TYPE:
        {
            write_legacy_counter(output, 0, snapshot.GET_stat.from_inside_count);
            write_legacy_counter(output, 4, snapshot.GET_stat.from_outside_count);
            write_legacy_counter(output, 8, snapshot.GET_stat.to_inside_count);
            write_legacy_counter(output, 12, snapshot.GET_stat.to_outside_count);
            write_legacy_counter(output, 16, snapshot.GET_stat.filtered_count);
            size = 20;
            break;
        }
        case PROXY_STAT_TYPE:
        {
            write_legacy_counter(output, 0, snapshot.proxy_stat.register_count);
            size = 4;
            break;
        }
        default:
            size = -1;
    }
    return size;
}

#include "telemetry/rm_monitor.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct rm_json_writer {
    char *out;
    size_t capacity;
    size_t length;
} rm_json_writer_t;

static void
json_append_bytes(rm_json_writer_t *writer, const char *text, size_t length)
{
    size_t available;
    size_t copy_length;

    if (writer == NULL || text == NULL)
        return;
    if (writer->capacity == 0 || writer->out == NULL) {
        writer->length += length;
        return;
    }

    available = writer->length < writer->capacity - 1 ?
        writer->capacity - 1 - writer->length : 0;
    copy_length = length < available ? length : available;
    if (copy_length != 0)
        memcpy(writer->out + writer->length, text, copy_length);
    writer->length += length;
    writer->out[writer->length < writer->capacity ? writer->length :
        writer->capacity - 1] = '\0';
}

static void
json_append_text(rm_json_writer_t *writer, const char *text)
{
    if (text != NULL)
        json_append_bytes(writer, text, strlen(text));
}

static void
json_append_format(rm_json_writer_t *writer, const char *format, ...)
{
    char buffer[512];
    va_list args;
    int length;

    va_start(args, format);
    length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (length <= 0)
        return;
    if ((size_t)length < sizeof(buffer)) {
        json_append_bytes(writer, buffer, (size_t)length);
        return;
    }

    /* All current monitor values fit in buffer; retain correct sizing if that changes. */
    json_append_bytes(writer, buffer, sizeof(buffer) - 1);
}

static void
json_append_string(rm_json_writer_t *writer, const char *text)
{
    const unsigned char *p = (const unsigned char *)(text == NULL ? "" : text);
    char escaped[7];

    json_append_text(writer, "\"");
    while (*p != '\0') {
        switch (*p) {
        case '\\': json_append_text(writer, "\\\\"); break;
        case '"': json_append_text(writer, "\\\""); break;
        case '\b': json_append_text(writer, "\\b"); break;
        case '\f': json_append_text(writer, "\\f"); break;
        case '\n': json_append_text(writer, "\\n"); break;
        case '\r': json_append_text(writer, "\\r"); break;
        case '\t': json_append_text(writer, "\\t"); break;
        default:
            if (*p < 0x20) {
                snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
                json_append_text(writer, escaped);
            } else {
                json_append_bytes(writer, (const char *)p, 1);
            }
            break;
        }
        p++;
    }
    json_append_text(writer, "\"");
}

static int
json_finish(rm_json_writer_t *writer, size_t *required)
{
    if (writer == NULL)
        return -1;
    if (required != NULL)
        *required = writer->length + 1;
    if (writer->capacity == 0 || writer->out == NULL ||
        writer->length >= writer->capacity)
        return -ENOSPC;
    writer->out[writer->length] = '\0';
    return 0;
}

static void
json_append_metric_object(rm_json_writer_t *writer,
    const rm_metrics_snapshot_t *metrics)
{
    unsigned i;

    json_append_text(writer, "\"counters\":{");
    for (i = 0; i < RM_METRIC_COUNT; i++) {
        if (i != 0)
            json_append_text(writer, ",");
        json_append_string(writer, rm_metric_name((rm_metric_id_t)i));
        json_append_format(writer, ":%" PRIu64, metrics->counters[i]);
    }
    json_append_text(writer, "},\"packet_rx\":{");
    for (i = 0; i < RM_PACKET_TYPE_COUNT; i++) {
        if (i != 0)
            json_append_text(writer, ",");
        json_append_string(writer, rm_packet_type_name((rm_packet_type_t)i));
        json_append_format(writer, ":%" PRIu64, metrics->packet_rx[i]);
    }
    json_append_text(writer, "},\"packet_results\":{");
    for (i = 0; i < RM_PACKET_TYPE_COUNT; i++) {
        unsigned result;

        if (i != 0)
            json_append_text(writer, ",");
        json_append_string(writer, rm_packet_type_name((rm_packet_type_t)i));
        json_append_text(writer, ":{");
        for (result = 0; result < RM_PACKET_RESULT_COUNT; result++) {
            if (result != 0)
                json_append_text(writer, ",");
            json_append_string(writer,
                rm_packet_result_name((rm_packet_result_t)result));
            json_append_format(writer, ":%" PRIu64,
                metrics->packet_results[i][result]);
        }
        json_append_text(writer, "}");
    }
    json_append_text(writer, "},\"trace\":{\"ann\":[");
    for (i = 0; i < RM_TRACE_AS_COUNT; i++) {
        if (i != 0)
            json_append_text(writer, ",");
        json_append_format(writer, "%" PRIu64, metrics->ann_trace[i]);
    }
    json_append_text(writer, "],\"get\":[");
    for (i = 0; i < RM_TRACE_AS_COUNT; i++) {
        if (i != 0)
            json_append_text(writer, ",");
        json_append_format(writer, "%" PRIu64, metrics->get_trace[i]);
    }
    json_append_text(writer, "]}");
}

int
rm_monitor_snapshot_read(rm_monitor_snapshot_t *out)
{
    uint16_t port_id;

    if (out == NULL || rm_metrics_read_snapshot(&out->metrics) != 0 ||
        rm_log_get_stats(&out->logs) != 0)
        return -1;

    memset(out->ports, 0, sizeof(out->ports));
    out->captured_at_ns = out->metrics.captured_at_ns;
    out->port_count = 0;
    for (port_id = 0; port_id < RTE_MAX_ETHPORTS; port_id++) {
        struct rte_eth_stats stats;
        struct rte_eth_link link;

        if (!rte_eth_dev_is_valid_port(port_id))
            continue;
        if (out->port_count >= RTE_MAX_ETHPORTS)
            break;

        memset(&stats, 0, sizeof(stats));
        memset(&link, 0, sizeof(link));
        rte_eth_stats_get(port_id, &stats);
        rte_eth_link_get_nowait(port_id, &link);
        out->ports[out->port_count].port_id = port_id;
        out->ports[out->port_count].link_up = link.link_status != 0;
        out->ports[out->port_count].link_speed_mbps = link.link_speed;
        out->ports[out->port_count].rx_packets = stats.ipackets;
        out->ports[out->port_count].rx_bytes = stats.ibytes;
        out->ports[out->port_count].rx_errors = stats.ierrors;
        out->ports[out->port_count].rx_missed = stats.imissed;
        out->ports[out->port_count].rx_no_mbuf = stats.rx_nombuf;
        out->ports[out->port_count].tx_packets = stats.opackets;
        out->ports[out->port_count].tx_bytes = stats.obytes;
        out->ports[out->port_count].tx_errors = stats.oerrors;
        out->port_count++;
    }
    return 0;
}

int
rm_monitor_snapshot_json(const rm_monitor_snapshot_t *snapshot,
    char *out, size_t capacity, size_t *required)
{
    rm_json_writer_t writer = { out, capacity, 0 };
    unsigned i;

    if (snapshot == NULL)
        return -1;
    json_append_format(&writer, "{\"captured_at_ns\":%" PRIu64
        ",\"generation\":%" PRIu64 ",",
        snapshot->captured_at_ns, snapshot->metrics.generation);
    json_append_metric_object(&writer, &snapshot->metrics);
    json_append_format(&writer, ",\"logs\":{\"accepted\":%" PRIu64
        ",\"dropped\":%" PRIu64 ",\"suppressed\":%" PRIu64
        ",\"oldest_sequence\":%" PRIu64
        ",\"newest_sequence\":%" PRIu64
        ",\"history_count\":%zu},\"ports\":[",
        snapshot->logs.accepted, snapshot->logs.dropped,
        snapshot->logs.suppressed, snapshot->logs.oldest_sequence,
        snapshot->logs.newest_sequence, snapshot->logs.history_count);
    for (i = 0; i < snapshot->port_count; i++) {
        const rm_monitor_port_snapshot_t *port = &snapshot->ports[i];

        if (i != 0)
            json_append_text(&writer, ",");
        json_append_format(&writer,
            "{\"port_id\":%u,\"link_up\":%s,\"link_speed_mbps\":%u"
            ",\"rx_packets\":%" PRIu64 ",\"rx_bytes\":%" PRIu64
            ",\"rx_errors\":%" PRIu64 ",\"rx_missed\":%" PRIu64
            ",\"rx_no_mbuf\":%" PRIu64 ",\"tx_packets\":%" PRIu64
            ",\"tx_bytes\":%" PRIu64 ",\"tx_errors\":%" PRIu64 "}",
            port->port_id, port->link_up ? "true" : "false",
            port->link_speed_mbps, port->rx_packets, port->rx_bytes,
            port->rx_errors, port->rx_missed, port->rx_no_mbuf,
            port->tx_packets, port->tx_bytes, port->tx_errors);
    }
    json_append_text(&writer, "]}");
    return json_finish(&writer, required);
}

static void
json_append_log_event(rm_json_writer_t *writer, const rm_log_event_t *event)
{
    json_append_format(writer,
        "{\"sequence\":%" PRIu64 ",\"timestamp_ns\":%" PRIu64
        ",\"level\":",
        event->sequence, event->timestamp_ns);
    json_append_string(writer, rm_log_level_name((rm_log_level_t)event->level));
    json_append_text(writer, ",\"module\":");
    json_append_string(writer, rm_log_module_name((rm_log_module_t)event->module));
    json_append_format(writer, ",\"event_code\":%u,\"lcore\":%u,\"port\":",
        event->event_code, event->lcore_id);
    if (event->port_id == RM_LOG_PORT_NONE)
        json_append_text(writer, "null");
    else
        json_append_format(writer, "%u", event->port_id);
    json_append_text(writer, ",\"message\":");
    json_append_string(writer, event->message);
    json_append_text(writer, "}");
}

int
rm_monitor_logs_json(uint64_t after_sequence, size_t limit,
    char *out, size_t capacity, size_t *required)
{
    rm_json_writer_t writer = { out, capacity, 0 };
    rm_log_event_t events[RM_MONITOR_LOG_LIMIT_MAX];
    uint64_t newest_sequence = 0;
    size_t count;
    size_t i;

    if (limit == 0)
        limit = RM_MONITOR_LOG_LIMIT_DEFAULT;
    if (limit > RM_MONITOR_LOG_LIMIT_MAX)
        limit = RM_MONITOR_LOG_LIMIT_MAX;
    count = rm_log_read_since(after_sequence, events, limit, &newest_sequence);
    json_append_format(&writer, "{\"after_sequence\":%" PRIu64
        ",\"next_sequence\":%" PRIu64 ",\"count\":%zu,\"logs\":[",
        after_sequence, newest_sequence, count);
    for (i = 0; i < count; i++) {
        if (i != 0)
            json_append_text(&writer, ",");
        json_append_log_event(&writer, &events[i]);
    }
    json_append_text(&writer, "]}");
    return json_finish(&writer, required);
}

#define _DEFAULT_SOURCE

#include "telemetry/rm_web.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <strings.h>

#include "tables/rm_tables.h"
#include "telemetry/rm_log.h"
#include "telemetry/rm_monitor.h"

#define RM_WEB_DEFAULT_BIND "127.0.0.1"
#define RM_WEB_DEFAULT_PORT 8080U
#define RM_WEB_DEFAULT_ROOT "./web"
#define RM_WEB_REQUEST_SIZE 8192U
/*
 * 每个请求 malloc 一块。最大的是 /api/v1/sids：1024 条 SID，每条带最长 255 字节的名称和描述，
 * 转义后最坏约 1.2 KB 一条，所以给 2 MB。
 */
#define RM_WEB_RESPONSE_SIZE (2U * 1024U * 1024U)
#define RM_WEB_ASSET_SIZE (512U * 1024U)
#define RM_WEB_BACKLOG 16
#define RM_WEB_BODY_MAX 4096U

static pthread_t web_thread;
static pthread_mutex_t web_state_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic bool web_running;
static _Atomic int web_server_fd = -1;
static uint16_t web_port;
static char web_bind[INET_ADDRSTRLEN];
static char web_root[PATH_MAX];

static bool
text_equals_ignore_case(const char *left, const char *right)
{
    if (left == NULL || right == NULL)
        return false;
    while (*left != '\0' && *right != '\0') {
        char l = *left >= 'A' && *left <= 'Z' ? (char)(*left - 'A' + 'a') : *left;
        char r = *right >= 'A' && *right <= 'Z' ? (char)(*right - 'A' + 'a') : *right;
        if (l != r)
            return false;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static bool
env_bool(const char *name, bool default_value)
{
    const char *value = getenv(name);

    if (value == NULL || value[0] == '\0')
        return default_value;
    if (text_equals_ignore_case(value, "0") ||
        text_equals_ignore_case(value, "false") ||
        text_equals_ignore_case(value, "off") ||
        text_equals_ignore_case(value, "no"))
        return false;
    if (text_equals_ignore_case(value, "1") ||
        text_equals_ignore_case(value, "true") ||
        text_equals_ignore_case(value, "on") ||
        text_equals_ignore_case(value, "yes"))
        return true;
    fprintf(stderr, "RM warning: invalid %s=%s, using %s\n", name, value,
        default_value ? "1" : "0");
    return default_value;
}

static unsigned
env_port(void)
{
    const char *value = getenv("RM_WEB_PORT");
    char *end;
    unsigned long parsed;

    if (value == NULL || value[0] == '\0')
        return RM_WEB_DEFAULT_PORT;
    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0' || parsed > 65535) {
        fprintf(stderr, "RM warning: invalid RM_WEB_PORT=%s, using %u\n",
            value, RM_WEB_DEFAULT_PORT);
        return RM_WEB_DEFAULT_PORT;
    }
    return (unsigned)parsed;
}

static int
send_all(int fd, const char *data, size_t length)
{
    while (length != 0) {
        ssize_t sent = send(fd, data, length, MSG_NOSIGNAL);
        if (sent > 0) {
            data += sent;
            length -= (size_t)sent;
        } else if (sent < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

/*
static const char *
content_type_for(const char *path)
{
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(path, "/app.js") == 0)
        return "application/javascript; charset=utf-8";
    if (strcmp(path, "/style.css") == 0)
        return "text/css; charset=utf-8";
    return "application/octet-stream";
}
*/

/*
static int
append_text(char *out, size_t capacity, size_t *length, const char *text)
{
    size_t text_length;

    if (out == NULL || length == NULL || text == NULL)
        return -1;
    text_length = strlen(text);
    if (*length > capacity || text_length > capacity - *length - 1)
        return -1;
    memcpy(out + *length, text, text_length);
    *length += text_length;
    out[*length] = '\0';
    return 0;
}
*/
static int
append_format(char *out, size_t capacity, size_t *length, const char *format, ...)
{
    va_list args;
    int written;

    va_start(args, format);
    written = vsnprintf(out + *length, capacity - *length, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= capacity - *length)
        return -1;
    *length += (size_t)written;
    return 0;
}

static int
append_char(char *out, size_t capacity, size_t *length, char c)
{
    if (*length + 1 >= capacity)
        return -1;
    out[(*length)++] = c;
    out[*length] = '\0';
    return 0;
}

/* 追加一个带引号的 JSON 字符串，转义 '"'、'\' 和全部控制字符 */
static int
append_json_string(char *out, size_t capacity, size_t *length, const char *text)
{
    static const char digits[] = "0123456789abcdef";
    const unsigned char *p;

    if (append_char(out, capacity, length, '"') != 0)
        return -1;
    for (p = (const unsigned char *)text; *p != '\0'; p++) {
        int ret;

        if (*p == '"' || *p == '\\') {
            ret = append_char(out, capacity, length, '\\');
            if (ret == 0)
                ret = append_char(out, capacity, length, (char)*p);
        } else if (*p < 0x20) {
            char escaped[7] = { '\\', 'u', '0', '0', digits[*p >> 4], digits[*p & 0x0f], '\0' };
            ret = append_format(out, capacity, length, "%s", escaped);
        } else {
            ret = append_char(out, capacity, length, (char)*p);
        }
        if (ret != 0)
            return -1;
    }
    return append_char(out, capacity, length, '"');
}

static uint64_t
query_uint64(const char *query, const char *name, uint64_t default_value)
{
    const char *cursor = query;
    size_t name_length = strlen(name);

    while (cursor != NULL && *cursor != '\0') {
        const char *equals = strchr(cursor, '=');
        const char *end = strchr(cursor, '&');
        char *parse_end;
        unsigned long long parsed;

        if (equals != NULL && (size_t)(equals - cursor) == name_length &&
            strncmp(cursor, name, name_length) == 0) {
            const char *value = equals + 1;
            size_t value_length = end == NULL ? strlen(value) :
                (size_t)(end - value);
            char buffer[32];

            if (value_length == 0 || value_length >= sizeof(buffer))
                return default_value;
            memcpy(buffer, value, value_length);
            buffer[value_length] = '\0';
            errno = 0;
            parsed = strtoull(buffer, &parse_end, 10);
            if (errno == 0 && parse_end != buffer && *parse_end == '\0')
                return (uint64_t)parsed;
            return default_value;
        }
        cursor = end == NULL ? NULL : end + 1;
    }
    return default_value;
}

static const char *
content_type_for(const char *path)
{
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(path, "/app.js") == 0)
        return "application/javascript; charset=utf-8";
    if (strcmp(path, "/style.css") == 0)
        return "text/css; charset=utf-8";
    return "application/octet-stream";
}

static int
hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* 十六进制文本转定长字节数组：右侧对齐，高位补零（与 CLI 行为一致） */
static bool
parse_hex_bytes(const char *text, uint8_t *dst, size_t size)
{
    size_t length;
    size_t i;
    size_t offset;

    if (text == NULL || dst == NULL || size == 0)
        return false;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text += 2;
    length = strlen(text);
    if (length == 0 || (length & 1) != 0 || length / 2 > size)
        return false;
    for (i = 0; i < length; i++)
        if (hex_value(text[i]) < 0)
            return false;
    memset(dst, 0, size);
    offset = size - length / 2;
    for (i = 0; i < length / 2; i++)
        dst[offset + i] = (uint8_t)((hex_value(text[i * 2]) << 4) |
            hex_value(text[i * 2 + 1]));
    return true;
}

static void
write_hex(char *out, size_t out_size, const uint8_t *data, size_t length)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;

    if (out == NULL || out_size < length * 2 + 1) {
        if (out != NULL && out_size != 0)
            out[0] = '\0';
        return;
    }
    for (i = 0; i < length; i++) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 0x0f];
    }
    out[length * 2] = '\0';
}

/*
 * 从小型 JSON 对象中提取字段值（字符串去引号，数字/字面量取原始 token）。
 * 仅用于本服务自身前端提交的受控 payload，不做通用转义还原。
 */
static bool
json_find_value(const char *json, const char *name, char *out, size_t out_size)
{
    char pattern[48];
    const char *cursor;
    const char *value;
    size_t name_length;

    if (json == NULL || name == NULL || out == NULL || out_size == 0)
        return false;
    name_length = strlen(name);
    if (name_length == 0 || name_length >= sizeof(pattern) - 3)
        return false;
    pattern[0] = '"';
    memcpy(pattern + 1, name, name_length);
    pattern[name_length + 1] = '"';
    pattern[name_length + 2] = '\0';
    cursor = json;
    while ((cursor = strstr(cursor, pattern)) != NULL) {
        value = cursor + name_length + 2;
        while (*value == ' ' || *value == '\t' || *value == '\n' ||
            *value == '\r')
            value++;
        if (*value != ':') {
            cursor += name_length + 2;
            continue;
        }
        value++;
        while (*value == ' ' || *value == '\t' || *value == '\n' ||
            *value == '\r')
            value++;
        if (*value == '"') {
            size_t i = 0;
            value++;
            while (*value != '"') {
                if (*value == '\0')
                    return false;
                if (*value == '\\' && value[1] != '\0') {
                    value++;
                    if (i + 1 >= out_size)
                        return false;
                    out[i++] = *value++;
                    continue;
                }
                if (i + 1 >= out_size)
                    return false;
                out[i++] = *value++;
            }
            out[i] = '\0';
            return true;
        }
        {
            size_t i = 0;
            while (*value != '\0' && *value != ',' && *value != '}' &&
                *value != ']' && *value != ' ' && *value != '\n' &&
                *value != '\r' && *value != '\t') {
                if (i + 1 >= out_size)
                    return false;
                out[i++] = *value++;
            }
            out[i] = '\0';
            return i != 0;
        }
    }
    return false;
}

static bool
json_find_uint(const char *json, const char *name, uint32_t *out)
{
    char token[40];
    char *end;
    unsigned long long parsed;

    if (out == NULL || !json_find_value(json, name, token, sizeof(token)))
        return false;
    errno = 0;
    if (token[0] == '0' && (token[1] == 'x' || token[1] == 'X'))
        parsed = strtoull(token + 2, &end, 16);
    else
        parsed = strtoull(token, &end, 10);
    if (errno != 0 || end == token || *end != '\0' || parsed > 0xffffffffULL)
        return false;
    *out = (uint32_t)parsed;
    return true;
}

/* 解析请求头中的 Content-Length（上限 2 MiB，异常时返回 0） */
static size_t
request_content_length(const char *request, size_t header_length)
{
    const char *cursor = request;
    const char *end = request + header_length;

    while (cursor < end) {
        const char *line_end = memchr(cursor, '\n', (size_t)(end - cursor));
        size_t line_length = line_end != NULL ?
            (size_t)(line_end - cursor) : (size_t)(end - cursor);

        if (line_length > 15 && strncasecmp(cursor, "Content-Length:", 15) == 0) {
            const char *value = cursor + 15;
            char *parse_end;
            unsigned long parsed;

            errno = 0;
            parsed = strtoul(value, &parse_end, 10);
            if (errno == 0 && parse_end != value && parsed <= 2U * 1024U * 1024U)
                return (size_t)parsed;
            return 0;
        }
        if (line_end == NULL)
            break;
        cursor = line_end + 1;
    }
    return 0;
}

static int
read_asset(const char *path, char *body, size_t capacity, size_t *body_length, const char **content_type)
{
    char full_path[PATH_MAX];
    FILE *file;
    long file_size;

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)
        path = "/index.html";
    else if (strcmp(path, "/app.js") != 0 && strcmp(path, "/style.css") != 0)
        return 404;

    if (snprintf(full_path, sizeof(full_path), "%s%s", web_root, path) >=
        (int)sizeof(full_path))
        return 500;
    file = fopen(full_path, "rb");
    if (file == NULL)
        return errno == ENOENT ? 404 : 500;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 500;
    }
    file_size = ftell(file);
    if (file_size < 0 || (unsigned long)file_size > RM_WEB_ASSET_SIZE ||
        (size_t)file_size >= capacity) {
        fclose(file);
        return 500;
    }
    rewind(file);
    *body_length = fread(body, 1, (size_t)file_size, file);
    fclose(file);
    if (*body_length != (size_t)file_size)
        return 500;
    *content_type = content_type_for(path);
    return 200;
}

static int
build_prometheus(char *body, size_t capacity, size_t *body_length)
{
    rm_monitor_snapshot_t snapshot;
    unsigned metric;
    unsigned type;
    unsigned result;
    unsigned port;

    if (rm_monitor_snapshot_read(&snapshot) != 0)
        return 500;
    *body_length = 0;
    for (metric = 0; metric < RM_METRIC_COUNT; metric++) {
        if (append_format(body, capacity, body_length,
            "rm_%s %" PRIu64 "\n", rm_metric_name((rm_metric_id_t)metric),
            snapshot.metrics.counters[metric]) != 0)
            return 500;
    }
    for (type = 0; type < RM_PACKET_TYPE_COUNT; type++) {
        if (append_format(body, capacity, body_length,
            "rm_packet_rx_total{type=\"%s\"} %" PRIu64 "\n",
            rm_packet_type_name((rm_packet_type_t)type),
            snapshot.metrics.packet_rx[type]) != 0)
            return 500;
        for (result = 0; result < RM_PACKET_RESULT_COUNT; result++) {
            if (append_format(body, capacity, body_length,
                "rm_packet_result_total{type=\"%s\",result=\"%s\"} %" PRIu64 "\n",
                rm_packet_type_name((rm_packet_type_t)type),
                rm_packet_result_name((rm_packet_result_t)result),
                snapshot.metrics.packet_results[type][result]) != 0)
                return 500;
        }
    }
    if (append_format(body, capacity, body_length,
        "rm_log_history_count %zu\nrm_log_accepted_total %" PRIu64
        "\nrm_log_dropped_total %" PRIu64 "\nrm_log_suppressed_total %" PRIu64 "\n",
        snapshot.logs.history_count, snapshot.logs.accepted,
        snapshot.logs.dropped, snapshot.logs.suppressed) != 0)
        return 500;
    for (port = 0; port < snapshot.port_count; port++) {
        const rm_monitor_port_snapshot_t *stats = &snapshot.ports[port];

        if (append_format(body, capacity, body_length,
            "rm_port_link_up{port=\"%u\"} %u\n"
            "rm_port_rx_packets{port=\"%u\"} %" PRIu64 "\n"
            "rm_port_tx_packets{port=\"%u\"} %" PRIu64 "\n"
            "rm_port_rx_errors{port=\"%u\"} %" PRIu64 "\n"
            "rm_port_tx_errors{port=\"%u\"} %" PRIu64 "\n",
            stats->port_id, stats->link_up, stats->port_id, stats->rx_packets,
            stats->port_id, stats->tx_packets, stats->port_id, stats->rx_errors,
            stats->port_id, stats->tx_errors) != 0)
            return 500;
    }
    return 200;
}

static int
build_health(char *body, size_t capacity, size_t *body_length)
{
    *body_length = 0;
    if (append_format(body, capacity, body_length,
        "{\"status\":\"ok\",\"service\":\"rm\","
        "\"web_running\":true,\"port\":%u}\n", rm_web_bound_port()) != 0)
        return 500;
    return 200;
}

/*
 * {"count":N,"sids":[{"sid":"<72 位 hex>","name":"…","name_verified":true,"desc":"…"}]}
 * 名称、描述来自终端，是不可信输入：TSID 表里存的已经是清洗过的副本，这里再做 JSON 转义。
 * 没有名称或描述时为空串；没有名称时 name_verified 为 false。
 */
static int
build_sid_list(char *body, size_t capacity, size_t *body_length)
{
    tsid_entry_t entry;
    uint32_t count = __atomic_load_n(&g_tsid_table.count, __ATOMIC_ACQUIRE);
    uint32_t i;

    if (count > MAX_SID_NUM)
        count = MAX_SID_NUM;
    *body_length = 0;
    if (append_format(body, capacity, body_length, "{\"count\":%u,\"sids\":[",
        count) != 0)
        return 500;
    for (i = 0; i < count; i++) {
        char hex[SID_LEN * 2 + 1];

        if (tsid_table_snapshot(i, &entry) != 0)
            return 500;
        write_hex(hex, sizeof(hex), entry.tsid, SID_LEN);
        if (append_format(body, capacity, body_length,
                "%s{\"sid\":\"%s\",\"name\":", i != 0 ? "," : "", hex) != 0 ||
            append_json_string(body, capacity, body_length, entry.name) != 0 ||
            append_format(body, capacity, body_length, ",\"name_verified\":%s,\"desc\":",
                entry.name_verified ? "true" : "false") != 0 ||
            append_json_string(body, capacity, body_length, entry.desc) != 0 ||
            append_char(body, capacity, body_length, '}') != 0)
            return 500;
    }
    if (append_format(body, capacity, body_length, "]}") != 0)
        return 500;
    return 200;
}

static int
build_acl_list(char *body, size_t capacity, size_t *body_length)
{
    int i;

    *body_length = 0;
    if (acl_store.rule_array == NULL) {
        if (append_format(body, capacity, body_length,
            "{\"count\":0,\"acls\":[]}") != 0)
            return 500;
        return 200;
    }
    if (append_format(body, capacity, body_length, "{\"count\":%d,\"acls\":[",
        acl_store.rule_array->count) != 0)
        return 500;
    for (i = 0; i < acl_store.rule_array->count; i++) {
        rm_acl_rule_t *rule = (rm_acl_rule_t *)rte_array_get(acl_store.rule_array, i);
        char sid_hex[RM_ACL_SID_LEN * 2 + 1];
        char nid_hex[RM_ACL_NID_LEN * 2 + 1];
        bool nid_wildcard = true;
        int j;

        if (rule == NULL)
            continue;
        for (j = 0; j < RM_ACL_NID_LEN; j++) {
            if (rule->nid[j] != 0) {
                nid_wildcard = false;
                break;
            }
        }
        write_hex(sid_hex, sizeof(sid_hex), rule->sid, RM_ACL_SID_LEN);
        write_hex(nid_hex, sizeof(nid_hex), rule->nid, RM_ACL_NID_LEN);
        if (append_format(body, capacity, body_length,
            "%s{\"id\":%u,\"action\":\"%s\",\"sid\":\"%s\",\"asid\":%u,"
            "\"nid\":\"%s\",\"nid_wildcard\":%s,\"allow_hits\":%llu,"
            "\"deny_hits\":%llu}",
            i != 0 ? "," : "", rule->id,
            rule->action == RM_ACL_ALLOW ? "allow" : "deny",
            sid_hex, rule->asid, nid_hex,
            nid_wildcard ? "true" : "false",
            (unsigned long long)atomic_load_explicit(&rule->allow_hits,
                memory_order_relaxed),
            (unsigned long long)atomic_load_explicit(&rule->deny_hits,
                memory_order_relaxed)) != 0)
            return 500;
    }
    if (append_format(body, capacity, body_length, "]}") != 0)
        return 500;
    return 200;
}

static int
api_error(char *body, size_t capacity, size_t *body_length, int status,
    const char *message)
{
    *body_length = (size_t)snprintf(body, capacity,
        "{\"status\":\"error\",\"message\":\"%s\"}", message);
    return status;
}

static int
handle_acl_create(const char *payload, char *body, size_t capacity,
    size_t *body_length)
{
    rm_acl_rule_t rule;
    char action[16];
    char hex[RM_ACL_SID_LEN * 2 + 3];
    uint32_t asid = 0;
    int ret;

    memset(&rule, 0, sizeof(rule));
    if (!json_find_uint(payload, "id", &rule.id))
        return api_error(body, capacity, body_length, 400,
            "invalid or missing id");
    if (!json_find_value(payload, "action", action, sizeof(action)) ||
        (!text_equals_ignore_case(action, "allow") &&
        !text_equals_ignore_case(action, "deny")))
        return api_error(body, capacity, body_length, 400,
            "action must be allow or deny");
    rule.action = text_equals_ignore_case(action, "allow") ?
        RM_ACL_ALLOW : RM_ACL_DENY;
    if (!json_find_value(payload, "sid", hex, sizeof(hex)) ||
        !parse_hex_bytes(hex, rule.sid, RM_ACL_SID_LEN))
        return api_error(body, capacity, body_length, 400,
            "sid must be non-empty even-length hex, at most 36 bytes");
    json_find_uint(payload, "asid", &asid);
    if (asid > 0xFF)
        return api_error(body, capacity, body_length, 400,
            "asid must be in range 0-255");
    rule.asid = asid;
    if (json_find_value(payload, "nid", hex, sizeof(hex)) &&
        hex[0] != '\0' && !parse_hex_bytes(hex, rule.nid, RM_ACL_NID_LEN))
        return api_error(body, capacity, body_length, 400,
            "nid must be even-length hex, at most 16 bytes");
    ret = rm_acl_rule_add(&rule);
    if (ret == -2)
        return api_error(body, capacity, body_length, 400,
            "acl rule id already exists");
    if (ret != 0)
        return 500;
    RM_LOG_INFO(RM_LOG_MODULE_CONTROL, RM_EVENT_CONTROL_CONFIG_APPLIED,
        RM_LOG_PORT_NONE, "acl rule added via web: id=%u action=%s",
        rule.id, rule.action == RM_ACL_ALLOW ? "allow" : "deny");
    *body_length = (size_t)snprintf(body, capacity, "{\"status\":\"ok\"}");
    return 200;
}

static int
handle_acl_delete(const char *query, char *body, size_t capacity,
    size_t *body_length)
{
    uint64_t id = query_uint64(query, "id", UINT64_MAX);

    if (id > 0xffffffffULL)
        return api_error(body, capacity, body_length, 400,
            "missing or invalid id");
    if (rm_acl_rule_delete((uint32_t)id) < 0)
        return api_error(body, capacity, body_length, 404, "rule not found");
    RM_LOG_INFO(RM_LOG_MODULE_CONTROL, RM_EVENT_CONTROL_CONFIG_APPLIED,
        RM_LOG_PORT_NONE, "acl rule deleted via web: id=%u", (uint32_t)id);
    *body_length = (size_t)snprintf(body, capacity, "{\"status\":\"ok\"}");
    return 200;
}

static int
build_api_response(const char *method, const char *path, const char *query,
    const char *payload, char *body, size_t capacity, size_t *body_length,
    const char **content_type)
{
    if (strcmp(path, "/api/v1/health") == 0) {
        if (strcmp(method, "GET") != 0)
            return 405;
        *content_type = "application/json; charset=utf-8";
        return build_health(body, capacity, body_length);
    }
    if (strcmp(path, "/api/v1/sids") == 0) {
        if (strcmp(method, "GET") != 0)
            return 405;
        *content_type = "application/json; charset=utf-8";
        return build_sid_list(body, capacity, body_length);
    }
    if (strcmp(path, "/api/v1/acls") == 0) {
        *content_type = "application/json; charset=utf-8";
        if (strcmp(method, "GET") == 0)
            return build_acl_list(body, capacity, body_length);
        if (strcmp(method, "POST") == 0)
            return handle_acl_create(payload, body, capacity, body_length);
        if (strcmp(method, "DELETE") == 0)
            return handle_acl_delete(query, body, capacity, body_length);
        return 405;
    }
    if (strcmp(path, "/api/v1/metrics") == 0) {
        rm_monitor_snapshot_t snapshot;
        size_t required;
        int ret;

        if (strcmp(method, "GET") != 0)
            return 405;
        *content_type = "application/json; charset=utf-8";
        if (rm_monitor_snapshot_read(&snapshot) != 0)
            return 500;
        ret = rm_monitor_snapshot_json(&snapshot, body, capacity, &required);
        if (ret != 0)
            return 500;
        *body_length = required - 1;
        return 200;
    }
    if (strcmp(path, "/api/v1/logs") == 0) {
        uint64_t after = query_uint64(query, "after", 0);
        uint64_t requested_limit = query_uint64(query, "limit",
            RM_MONITOR_LOG_LIMIT_DEFAULT);
        size_t limit = requested_limit > SIZE_MAX ? SIZE_MAX :
            (size_t)requested_limit;
        size_t required;
        int ret;

        if (strcmp(method, "GET") != 0)
            return 405;
        *content_type = "application/json; charset=utf-8";
        ret = rm_monitor_logs_json(after, limit, body, capacity, &required);
        if (ret != 0)
            return 500;
        *body_length = required - 1;
        return 200;
    }
    if (strcmp(path, "/metrics") == 0) {
        *content_type = "text/plain; version=0.0.4; charset=utf-8";
        return build_prometheus(body, capacity, body_length);
    }
    return 0;
}

static void
send_http_response(int client, int status, const char *content_type,
    const char *body, size_t body_length)
{
    char header[512];
    const char *status_text = status == 200 ? "OK" :
        (status == 400 ? "Bad Request" :
        (status == 404 ? "Not Found" :
        (status == 405 ? "Method Not Allowed" : "Internal Server Error")));
    int header_length = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        status, status_text, content_type, body_length);

    if (header_length > 0 && (size_t)header_length < sizeof(header)) {
        send_all(client, header, (size_t)header_length);
        if (body_length != 0)
            send_all(client, body, body_length);
    }
}

static void
handle_client(int client)
{
    char request[RM_WEB_REQUEST_SIZE + 1];
    char payload[RM_WEB_BODY_MAX + 1];
    char method[8];
    char target[2048];
    char version[16];
    char path[2048];
    char *query;
    char *body;
    const char *content_type = "text/plain; charset=utf-8";
    size_t received = 0;
    size_t body_length = 0;
    size_t header_total = 0;
    size_t declared_length = 0;
    size_t payload_length = 0;
    const char *header_end;
    ssize_t count;
    int status;

    while (received < RM_WEB_REQUEST_SIZE) {
        count = recv(client, request + received,
            RM_WEB_REQUEST_SIZE - received, 0);
        if (count > 0) {
            received += (size_t)count;
            request[received] = '\0';
            if (strstr(request, "\r\n\r\n") != NULL ||
                strstr(request, "\n\n") != NULL)
                break;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return;
        }
    }
    if (received == RM_WEB_REQUEST_SIZE ||
        sscanf(request, "%7s %2047s %15s", method, target, version) != 3 ||
        (strcmp(version, "HTTP/1.0") != 0 &&
            strcmp(version, "HTTP/1.1") != 0)) {
        send_http_response(client, 400, content_type, "bad request\n", 12);
        return;
    }
    if (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0 &&
        strcmp(method, "DELETE") != 0) {
        send_http_response(client, 405, content_type, "method not allowed\n", 19);
        return;
    }
    query = strchr(target, '?');
    if (query != NULL) {
        *query++ = '\0';
    }
    if (target[0] != '/' || strlen(target) >= sizeof(path)) {
        send_http_response(client, 400, content_type, "bad path\n", 9);
        return;
    }
    snprintf(path, sizeof(path), "%s", target);

    // 提取请求体（POST/DELETE 需要）：头部结束后继续读取到声明的 Content-Length
    header_end = strstr(request, "\r\n\r\n");
    if (header_end != NULL)
        header_total = (size_t)(header_end - request) + 4;
    else if ((header_end = strstr(request, "\n\n")) != NULL)
        header_total = (size_t)(header_end - request) + 2;
    if (header_total != 0) {
        declared_length = request_content_length(request, header_total);
        if (declared_length > RM_WEB_BODY_MAX) {
            send_http_response(client, 400, content_type,
                "payload too large\n", 18);
            return;
        }
        while (received < header_total + declared_length &&
            received < RM_WEB_REQUEST_SIZE) {
            count = recv(client, request + received,
                RM_WEB_REQUEST_SIZE - received, 0);
            if (count > 0) {
                received += (size_t)count;
                request[received] = '\0';
            } else if (count < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
        if (received > header_total) {
            payload_length = received - header_total;
            if (payload_length > declared_length)
                payload_length = declared_length;
            if (payload_length > RM_WEB_BODY_MAX)
                payload_length = RM_WEB_BODY_MAX;
            memcpy(payload, request + header_total, payload_length);
        }
    }
    payload[payload_length] = '\0';

    body = malloc(RM_WEB_RESPONSE_SIZE);
    if (body == NULL) {
        send_http_response(client, 500, content_type, "out of memory\n", 14);
        return;
    }
    status = build_api_response(method, path, query, payload, body,
        RM_WEB_RESPONSE_SIZE, &body_length, &content_type);
    if (status == 0) {
        if (strcmp(method, "GET") != 0) {
            status = 405;
        } else {
            status = read_asset(path, body, RM_WEB_RESPONSE_SIZE, &body_length,
                &content_type);
            if (status == 404)
                content_type = "text/plain; charset=utf-8";
        }
    }
    if (status == 404) {
        const char *message = "not found\n";
        send_http_response(client, status, content_type, message, strlen(message));
    } else if (status == 200) {
        send_http_response(client, status, content_type, body, body_length);
    } else {
        const char *message = "internal server error\n";
        send_http_response(client, status, content_type, message, strlen(message));
    }
    free(body);
}

static void *
web_thread_main(void *unused)
{
    struct pollfd poll_fd;

    (void)unused;
    while (atomic_load_explicit(&web_running, memory_order_acquire)) {
        int server_fd = atomic_load_explicit(&web_server_fd, memory_order_acquire);
        int client;
        int poll_result;

        if (server_fd < 0)
            break;
        poll_fd.fd = server_fd;
        poll_fd.events = POLLIN;
        poll_fd.revents = 0;
        poll_result = poll(&poll_fd, 1, 200);
        if (poll_result <= 0)
            continue;
        if ((poll_fd.revents & POLLIN) == 0)
            continue;
        client = accept(server_fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            if (!atomic_load_explicit(&web_running, memory_order_acquire))
                break;
            continue;
        }
        handle_client(client);
        close(client);
    }
    return NULL;
}

int
rm_web_start(void)
{
    struct sockaddr_in address;
    struct sockaddr_in bound_address;
    socklen_t bound_length = sizeof(bound_address);
    const char *bind_address;
    const char *root;
    int server_fd;
    int reuse = 1;
    unsigned configured_port;
    int ret;

    if (!env_bool("RM_WEB_ENABLED", true))
        return 0;
    pthread_mutex_lock(&web_state_lock);
    if (atomic_load_explicit(&web_running, memory_order_acquire)) {
        pthread_mutex_unlock(&web_state_lock);
        return 0;
    }
    bind_address = getenv("RM_WEB_BIND");
    if (bind_address == NULL || bind_address[0] == '\0')
        bind_address = RM_WEB_DEFAULT_BIND;
    root = getenv("RM_WEB_ROOT");
    if (root == NULL || root[0] == '\0')
        root = RM_WEB_DEFAULT_ROOT;
    if (inet_pton(AF_INET, bind_address, &address.sin_addr) != 1 ||
        snprintf(web_bind, sizeof(web_bind), "%s", bind_address) >=
            (int)sizeof(web_bind) || snprintf(web_root, sizeof(web_root),
            "%s", root) >= (int)sizeof(web_root)) {
        pthread_mutex_unlock(&web_state_lock);
        return -1;
    }
    configured_port = env_port();
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        pthread_mutex_unlock(&web_state_lock);
        return -1;
    }
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)configured_port);
    if (inet_pton(AF_INET, bind_address, &address.sin_addr) != 1 ||
        bind(server_fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(server_fd, RM_WEB_BACKLOG) != 0 ||
        getsockname(server_fd, (struct sockaddr *)&bound_address, &bound_length) != 0) {
        close(server_fd);
        pthread_mutex_unlock(&web_state_lock);
        return -1;
    }
    web_port = ntohs(bound_address.sin_port);
    atomic_store_explicit(&web_server_fd, server_fd, memory_order_release);
    atomic_store_explicit(&web_running, true, memory_order_release);
    ret = pthread_create(&web_thread, NULL, web_thread_main, NULL);
    if (ret != 0) {
        atomic_store_explicit(&web_running, false, memory_order_release);
        atomic_store_explicit(&web_server_fd, -1, memory_order_release);
        close(server_fd);
        pthread_mutex_unlock(&web_state_lock);
        return -1;
    }
    pthread_mutex_unlock(&web_state_lock);
    RM_LOG_INFO(RM_LOG_MODULE_APP, RM_EVENT_WEB_STARTED, RM_LOG_PORT_NONE,
        "web monitor listening on %s:%u root=%s", web_bind, web_port, web_root);
    return 0;
}

void
rm_web_stop(void)
{
    int server_fd;

    pthread_mutex_lock(&web_state_lock);
    if (!atomic_load_explicit(&web_running, memory_order_acquire)) {
        pthread_mutex_unlock(&web_state_lock);
        return;
    }
    atomic_store_explicit(&web_running, false, memory_order_release);
    server_fd = atomic_exchange_explicit(&web_server_fd, -1,
        memory_order_acq_rel);
    if (server_fd >= 0)
        shutdown(server_fd, SHUT_RDWR);
    pthread_mutex_unlock(&web_state_lock);
    if (server_fd >= 0)
        close(server_fd);
    pthread_join(web_thread, NULL);
    RM_LOG_INFO(RM_LOG_MODULE_APP, RM_EVENT_WEB_STOPPING, RM_LOG_PORT_NONE,
        "web monitor stopped");
}

int
rm_web_is_running(void)
{
    return atomic_load_explicit(&web_running, memory_order_acquire) ? 1 : 0;
}

uint16_t
rm_web_bound_port(void)
{
    return web_port;
}

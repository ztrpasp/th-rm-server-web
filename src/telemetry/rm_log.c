#define _DEFAULT_SOURCE

#include "telemetry/rm_log.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <rte_lcore.h>
#include <rte_mempool.h>
#include <rte_ring.h>

#include "telemetry/rm_metrics.h"

#define RM_LOG_RING_SIZE 2048
#define RM_LOG_POOL_SIZE (RM_LOG_RING_SIZE - 1)
#define RM_LOG_HISTORY_SIZE 512
#define RM_LOG_RATE_BUCKETS 1024
#define RM_LOG_RATE_INTERVAL_NS 1000000000ULL

static struct rte_ring *log_ring;
static struct rte_mempool *log_pool;
static pthread_t log_thread;
static _Atomic bool log_running;
static _Atomic bool log_initialized;
static _Atomic uint64_t log_sequence;
static _Atomic uint64_t log_accepted;
static _Atomic uint64_t log_dropped;
static _Atomic uint64_t log_suppressed;
static _Atomic uint64_t log_rate_last_ns[RM_LOG_RATE_BUCKETS];
static rm_log_event_t log_history[RM_LOG_HISTORY_SIZE];
static size_t log_history_next;
static size_t log_history_count;
static pthread_mutex_t log_history_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t log_state_cond = PTHREAD_COND_INITIALIZER;
static unsigned log_active_writers;
static FILE *log_file;
static char log_file_path[PATH_MAX];
static rm_log_level_t log_min_level = RM_LOG_LEVEL_INFO;
static bool log_stderr_enabled;
static uint64_t log_file_max_bytes;
static unsigned log_rotate_files;

static uint64_t
realtime_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t
monotonic_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static bool
string_equals_ignore_case(const char *left, const char *right)
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

int
rm_log_level_parse(const char *text, rm_log_level_t *out)
{
    if (text == NULL || out == NULL)
        return -1;
    if (string_equals_ignore_case(text, "debug"))
        *out = RM_LOG_LEVEL_DEBUG;
    else if (string_equals_ignore_case(text, "info"))
        *out = RM_LOG_LEVEL_INFO;
    else if (string_equals_ignore_case(text, "warn") ||
        string_equals_ignore_case(text, "warning"))
        *out = RM_LOG_LEVEL_WARN;
    else if (string_equals_ignore_case(text, "error"))
        *out = RM_LOG_LEVEL_ERROR;
    else
        return -1;
    return 0;
}

int
rm_log_level_enabled(rm_log_level_t level)
{
    return (unsigned)level < RM_LOG_LEVEL_COUNT && level >= log_min_level;
}

static bool
parse_bool_value(const char *text, bool *out)
{
    if (text == NULL || out == NULL)
        return false;
    if (string_equals_ignore_case(text, "1") ||
        string_equals_ignore_case(text, "true") ||
        string_equals_ignore_case(text, "yes") ||
        string_equals_ignore_case(text, "on")) {
        *out = true;
        return true;
    }
    if (string_equals_ignore_case(text, "0") ||
        string_equals_ignore_case(text, "false") ||
        string_equals_ignore_case(text, "no") ||
        string_equals_ignore_case(text, "off")) {
        *out = false;
        return true;
    }
    return false;
}

static bool
parse_unsigned_value(const char *text, uint64_t *out)
{
    char *end;
    unsigned long long value;

    if (text == NULL || out == NULL || text[0] == '\0' || text[0] == '-')
        return false;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0')
        return false;
    *out = (uint64_t)value;
    return true;
}

static void
load_log_config(void)
{
    const char *value;
    uint64_t parsed;
    bool parsed_bool;

    log_min_level = RM_LOG_LEVEL_INFO;
    log_stderr_enabled = true;
    log_file_max_bytes = RM_LOG_DEFAULT_FILE_MAX_BYTES;
    log_rotate_files = RM_LOG_DEFAULT_ROTATE_FILES;

    value = getenv("RM_LOG_LEVEL");
    if (value != NULL && value[0] != '\0' && rm_log_level_parse(value, &log_min_level) != 0) {
        fprintf(stderr, "RM warning: invalid RM_LOG_LEVEL=%s, using info\n", value);
        log_min_level = RM_LOG_LEVEL_INFO;
    }

    value = getenv("RM_LOG_STDERR");
    if (value != NULL && value[0] != '\0' && !parse_bool_value(value, &parsed_bool))
        fprintf(stderr, "RM warning: invalid RM_LOG_STDERR=%s, using 1\n", value);
    else if (value != NULL && value[0] != '\0')
        log_stderr_enabled = parsed_bool;

    value = getenv("RM_LOG_ROTATE_SIZE");
    if (value != NULL && value[0] != '\0' &&
        (!parse_unsigned_value(value, &parsed) || parsed < 1024)) {
        fprintf(stderr, "RM warning: invalid RM_LOG_ROTATE_SIZE=%s, using default\n", value);
    } else if (value != NULL && value[0] != '\0') {
        log_file_max_bytes = parsed;
    }

    value = getenv("RM_LOG_ROTATE_FILES");
    if (value != NULL && value[0] != '\0' &&
        (!parse_unsigned_value(value, &parsed) || parsed == 0 || parsed > 100)) {
        fprintf(stderr, "RM warning: invalid RM_LOG_ROTATE_FILES=%s, using default\n", value);
    } else if (value != NULL && value[0] != '\0') {
        log_rotate_files = (unsigned)parsed;
    }
}

static bool
rate_limit_allows(uint32_t event_code)
{
    uint64_t now;
    uint64_t previous;
    _Atomic uint64_t *bucket;

    if (!((event_code >= 2000 && event_code < 4000) ||
        (event_code >= 6000 && event_code < 7000) ||
        event_code == RM_EVENT_CONTROL_UNKNOWN ||
        event_code == RM_EVENT_CONTROL_CONFIG_PERSIST_FAILED))
        return true;

    now = monotonic_ns();
    bucket = &log_rate_last_ns[event_code % RM_LOG_RATE_BUCKETS];
    previous = atomic_load_explicit(bucket, memory_order_relaxed);
    if (previous == 0) {
        if (atomic_compare_exchange_strong_explicit(bucket, &previous, now,
                memory_order_relaxed, memory_order_relaxed))
            return true;
    }
    while (now >= previous && now - previous >= RM_LOG_RATE_INTERVAL_NS) {
        if (atomic_compare_exchange_weak_explicit(bucket, &previous, now,
                memory_order_relaxed, memory_order_relaxed))
            return true;
    }
    atomic_fetch_add_explicit(&log_suppressed, 1, memory_order_relaxed);
    rm_metrics_add(RM_METRIC_LOG_SUPPRESSED, 1);
    return false;
}

const char *
rm_log_level_name(rm_log_level_t level)
{
    static const char *const names[] = { "debug", "info", "warn", "error" };
    return (unsigned)level < sizeof(names) / sizeof(names[0]) ?
        names[level] : "unknown";
}

const char *
rm_log_module_name(rm_log_module_t module)
{
    static const char *const names[RM_LOG_MODULE_COUNT] = {
        "app", "ann", "get", "control", "arp", "table", "security"
    };
    return (unsigned)module < RM_LOG_MODULE_COUNT ? names[module] : "unknown";
}

static void
write_escaped_message(FILE *stream, const char *message)
{
    const unsigned char *p = (const unsigned char *)message;

    while (*p != '\0') {
        switch (*p) {
        case '\\': fputs("\\\\", stream); break;
        case '"': fputs("\\\"", stream); break;
        case '\r': fputs("\\r", stream); break;
        case '\n': fputs("\\n", stream); break;
        case '\t': fputs("\\t", stream); break;
        default:
            if (*p >= 0x20)
                fputc(*p, stream);
            break;
        }
        p++;
    }
}

static void
store_history(const rm_log_event_t *event)
{
    pthread_mutex_lock(&log_history_lock);
    log_history[log_history_next] = *event;
    log_history_next = (log_history_next + 1) % RM_LOG_HISTORY_SIZE;
    if (log_history_count < RM_LOG_HISTORY_SIZE)
        log_history_count++;
    pthread_mutex_unlock(&log_history_lock);
}

static void
write_event_to(FILE *stream, const rm_log_event_t *event)
{
    fprintf(stream,
        "{\"ts_ns\":%" PRIu64 ",\"seq\":%" PRIu64
        ",\"level\":\"%s\",\"module\":\"%s\",\"event_code\":%u"
        ",\"lcore\":%u,\"port\":",
        event->timestamp_ns, event->sequence,
        rm_log_level_name((rm_log_level_t)event->level),
        rm_log_module_name((rm_log_module_t)event->module),
        event->event_code, event->lcore_id);
    if (event->port_id == RM_LOG_PORT_NONE)
        fputs("null", stream);
    else
        fprintf(stream, "%u", event->port_id);
    fputs(",\"message\":\"", stream);
    write_escaped_message(stream, event->message);
    fputs("\"}\n", stream);
}

static void
rotate_log_file_if_needed(void)
{
    struct stat info;
    char source[PATH_MAX];
    char target[PATH_MAX];

    if (log_file == NULL || log_file_max_bytes == 0 ||
        stat(log_file_path, &info) != 0 || info.st_size < (off_t)log_file_max_bytes)
        return;

    fclose(log_file);
    log_file = NULL;

    // Calculate effective path length to avoid truncation warnings
    // Reserve enough space for ".<MAX_UINT>\0" which is at most 12 chars ("." + 10 digits + "\0")
    size_t base_path_len = strnlen(log_file_path, sizeof(log_file_path));
    if (base_path_len > sizeof(log_file_path) - 12) {
        base_path_len = sizeof(log_file_path) - 12;
    }


    for (unsigned i = log_rotate_files; i > 1; i--) {
        snprintf(source, sizeof(source), "%.*s.%u", (int)base_path_len, log_file_path, i - 1);
        snprintf(target, sizeof(target), "%.*s.%u", (int)base_path_len, log_file_path, i);
        rename(source, target);
    }
    snprintf(target, sizeof(target), "%.*s.1", (int)base_path_len, log_file_path);
    rename(log_file_path, target);
    log_file = fopen(log_file_path, "a");
}

static void *
log_consumer(void *unused)
{
    rm_log_event_t *event;

    (void)unused;
    while (atomic_load_explicit(&log_running, memory_order_acquire) ||
        (log_ring != NULL && !rte_ring_empty(log_ring))) {
        if (log_ring != NULL && rte_ring_dequeue(log_ring, (void **)&event) == 0) {
            store_history(event);
            if (log_stderr_enabled)
                write_event_to(stderr, event);
            rotate_log_file_if_needed();
            if (log_file != NULL) {
                write_event_to(log_file, event);
                fflush(log_file);
            }
            rte_mempool_put(log_pool, event);
        } else {
            usleep(1000);
        }
    }
    fflush(stderr);
    return NULL;
}

int
rm_log_init(void)
{
    int ret;
    const char *configured_log_path;

    pthread_mutex_lock(&log_state_lock);
    if (atomic_load_explicit(&log_initialized, memory_order_acquire)) {
        pthread_mutex_unlock(&log_state_lock);
        return 0;
    }

    log_ring = rte_ring_create("RM_LOG_RING", RM_LOG_RING_SIZE,
        rte_socket_id(), RING_F_SC_DEQ);
    if (log_ring == NULL) {
        pthread_mutex_unlock(&log_state_lock);
        return -1;
    }

    log_pool = rte_mempool_create("RM_LOG_POOL", RM_LOG_POOL_SIZE,
        sizeof(rm_log_event_t), 0, 0, NULL, NULL, NULL, NULL,
        rte_socket_id(), 0);
    if (log_pool == NULL) {
        rte_ring_free(log_ring);
        log_ring = NULL;
        pthread_mutex_unlock(&log_state_lock);
        return -1;
    }

    log_history_next = 0;
    log_history_count = 0;
    load_log_config();
    configured_log_path = getenv("RM_LOG_FILE");
    if (configured_log_path == NULL || configured_log_path[0] == '\0')
        configured_log_path = RM_LOG_DEFAULT_FILE;
    if (string_equals_ignore_case(configured_log_path, "none") ||
        string_equals_ignore_case(configured_log_path, "off"))
        configured_log_path = "";
    snprintf(log_file_path, sizeof(log_file_path), "%s", configured_log_path);
    log_file = log_file_path[0] == '\0' ? NULL : fopen(log_file_path, "a");
    if (log_file == NULL)
        if (log_file_path[0] != '\0')
            fprintf(stderr, "RM warning: unable to open log file %s\n", log_file_path);
    atomic_init(&log_sequence, 0);
    atomic_init(&log_accepted, 0);
    atomic_init(&log_dropped, 0);
    atomic_init(&log_suppressed, 0);
    for (unsigned i = 0; i < RM_LOG_RATE_BUCKETS; i++)
        atomic_init(&log_rate_last_ns[i], 0);
    atomic_store_explicit(&log_running, true, memory_order_release);
    ret = pthread_create(&log_thread, NULL, log_consumer, NULL);
    if (ret != 0) {
        atomic_store_explicit(&log_running, false, memory_order_release);
        rte_mempool_free(log_pool);
        rte_ring_free(log_ring);
        log_pool = NULL;
        log_ring = NULL;
        if (log_file != NULL) {
            fclose(log_file);
            log_file = NULL;
        }
        pthread_mutex_unlock(&log_state_lock);
        return -1;
    }

    atomic_store_explicit(&log_initialized, true, memory_order_release);
    pthread_mutex_unlock(&log_state_lock);
    return 0;
}

void
rm_log_shutdown(void)
{
    pthread_mutex_lock(&log_state_lock);
    if (!atomic_load_explicit(&log_initialized, memory_order_acquire)) {
        pthread_mutex_unlock(&log_state_lock);
        return;
    }

    atomic_store_explicit(&log_initialized, false, memory_order_release);
    atomic_store_explicit(&log_running, false, memory_order_release);
    while (log_active_writers != 0)
        pthread_cond_wait(&log_state_cond, &log_state_lock);
    pthread_mutex_unlock(&log_state_lock);
    pthread_join(log_thread, NULL);
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
    rte_mempool_free(log_pool);
    rte_ring_free(log_ring);
    log_pool = NULL;
    log_ring = NULL;
}

static bool
log_writer_acquire(void)
{
    bool enabled;

    pthread_mutex_lock(&log_state_lock);
    enabled = atomic_load_explicit(&log_initialized, memory_order_acquire);
    if (enabled)
        log_active_writers++;
    pthread_mutex_unlock(&log_state_lock);
    return enabled;
}

static void
log_writer_release(void)
{
    pthread_mutex_lock(&log_state_lock);
    log_active_writers--;
    if (log_active_writers == 0)
        pthread_cond_signal(&log_state_cond);
    pthread_mutex_unlock(&log_state_lock);
}

void
rm_log_write(rm_log_level_t level, rm_log_module_t module,
    uint32_t event_code, uint16_t port_id, const char *fmt, ...)
{
    rm_log_event_t *event;
    va_list args;

    if (fmt == NULL || !log_writer_acquire())
        return;
    if (!rm_log_level_enabled(level)) {
        log_writer_release();
        return;
    }
    if (!rate_limit_allows(event_code))
    {
        log_writer_release();
        return;
    }
    if (rte_mempool_get(log_pool, (void **)&event) != 0) {
        atomic_fetch_add_explicit(&log_dropped, 1, memory_order_relaxed);
        rm_metrics_add(RM_METRIC_LOG_DROPPED, 1);
        log_writer_release();
        return;
    }

    memset(event, 0, sizeof(*event));
    event->sequence = atomic_fetch_add_explicit(
        &log_sequence, 1, memory_order_relaxed) + 1;
    event->timestamp_ns = realtime_ns();
    event->lcore_id = rte_lcore_id();
    event->event_code = event_code;
    event->port_id = port_id;
    event->level = (uint8_t)level;
    event->module = (uint8_t)module;

    va_start(args, fmt);
    vsnprintf(event->message, sizeof(event->message), fmt, args);
    va_end(args);

    if (rte_ring_enqueue(log_ring, event) != 0) {
        rte_mempool_put(log_pool, event);
        atomic_fetch_add_explicit(&log_dropped, 1, memory_order_relaxed);
        rm_metrics_add(RM_METRIC_LOG_DROPPED, 1);
    } else {
        atomic_fetch_add_explicit(&log_accepted, 1, memory_order_relaxed);
    }
    log_writer_release();
}

size_t
rm_log_read_recent(rm_log_event_t *out, size_t capacity,
    uint64_t *newest_sequence)
{
    size_t count;
    size_t start;
    size_t i;

    if (out == NULL || capacity == 0)
        return 0;

    pthread_mutex_lock(&log_history_lock);
    count = log_history_count < capacity ? log_history_count : capacity;
    start = (log_history_next + RM_LOG_HISTORY_SIZE - count) %
        RM_LOG_HISTORY_SIZE;
    for (i = 0; i < count; i++)
        out[i] = log_history[(start + i) % RM_LOG_HISTORY_SIZE];
    if (newest_sequence != NULL)
        *newest_sequence = count == 0 ? 0 : out[count - 1].sequence;
    pthread_mutex_unlock(&log_history_lock);
    return count;
}

size_t
rm_log_read_since(uint64_t after_sequence, rm_log_event_t *out,
    size_t capacity, uint64_t *newest_sequence)
{
    size_t count = 0;
    size_t start;
    size_t i;

    if (out == NULL || capacity == 0)
        return 0;
    pthread_mutex_lock(&log_history_lock);
    start = (log_history_next + RM_LOG_HISTORY_SIZE - log_history_count) %
        RM_LOG_HISTORY_SIZE;
    for (i = 0; i < log_history_count && count < capacity; i++) {
        rm_log_event_t *event = &log_history[(start + i) % RM_LOG_HISTORY_SIZE];
        if (event->sequence > after_sequence)
            out[count++] = *event;
    }
    if (newest_sequence != NULL)
        *newest_sequence = count != 0 ? out[count - 1].sequence :
            (log_history_count == 0 ? 0 :
                log_history[(log_history_next + RM_LOG_HISTORY_SIZE - 1) %
                    RM_LOG_HISTORY_SIZE].sequence);
    pthread_mutex_unlock(&log_history_lock);
    return count;
}

int
rm_log_get_stats(rm_log_stats_t *out)
{
    if (out == NULL)
        return -1;

    memset(out, 0, sizeof(*out));
    out->accepted = atomic_load_explicit(&log_accepted, memory_order_relaxed);
    out->dropped = atomic_load_explicit(&log_dropped, memory_order_relaxed);
    out->suppressed = atomic_load_explicit(&log_suppressed, memory_order_relaxed);
    pthread_mutex_lock(&log_history_lock);
    out->history_count = log_history_count;
    if (log_history_count != 0) {
        out->oldest_sequence = log_history[
            (log_history_next + RM_LOG_HISTORY_SIZE - log_history_count) %
            RM_LOG_HISTORY_SIZE].sequence;
        out->newest_sequence = log_history[
            (log_history_next + RM_LOG_HISTORY_SIZE - 1) %
            RM_LOG_HISTORY_SIZE].sequence;
    }
    pthread_mutex_unlock(&log_history_lock);
    return 0;
}

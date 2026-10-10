#ifndef RM_LOG_H
#define RM_LOG_H

#include <stddef.h>
#include <stdint.h>

#define RM_LOG_MESSAGE_SIZE 160
#define RM_LOG_PORT_NONE UINT16_MAX
#define RM_LOG_DEFAULT_FILE "./rm.log"
#define RM_LOG_DEFAULT_FILE_MAX_BYTES (10ULL * 1024ULL * 1024ULL)
#define RM_LOG_DEFAULT_ROTATE_FILES 5U

typedef enum rm_log_level {
    RM_LOG_LEVEL_DEBUG = 0,
    RM_LOG_LEVEL_INFO,
    RM_LOG_LEVEL_WARN,
    RM_LOG_LEVEL_ERROR
} rm_log_level_t;

#define RM_LOG_LEVEL_COUNT ((unsigned)(RM_LOG_LEVEL_ERROR + 1))

typedef enum rm_log_module {
    RM_LOG_MODULE_APP = 0,
    RM_LOG_MODULE_ANN,
    RM_LOG_MODULE_GET,
    RM_LOG_MODULE_CONTROL,
    RM_LOG_MODULE_ARP,
    RM_LOG_MODULE_TABLE,
    RM_LOG_MODULE_SECURITY,
    RM_LOG_MODULE_COUNT
} rm_log_module_t;

typedef enum rm_log_event_code {
    RM_EVENT_APP_STARTED = 1000,
    RM_EVENT_APP_STOPPING,
    RM_EVENT_PACKET_BAD_CHECKSUM = 2000,
    RM_EVENT_PACKET_MALFORMED,
    RM_EVENT_RESOURCE_EXHAUSTED,
    RM_EVENT_ANN_NO_SOURCE_PATH,
    RM_EVENT_ANN_PROXY_NOT_FOUND,
    RM_EVENT_ANN_POLICY_DENIED,
    RM_EVENT_GET_SID_NOT_FOUND = 3000,
    RM_EVENT_GET_ACL_DENIED,
    RM_EVENT_GET_POLICY_DENIED,
    RM_EVENT_GET_SID_LIST_RETURNED,
    RM_EVENT_CONTROL_UNKNOWN = 4000,
    RM_EVENT_CONTROL_CONFIG_APPLIED,
    RM_EVENT_CONTROL_CONFIG_PERSIST_FAILED,
    RM_EVENT_PROXY_REGISTERED,
    RM_EVENT_TABLE_INITIALIZED = 5000,
    RM_EVENT_TABLE_INITIALIZATION_FAILED,
    RM_EVENT_ARP_UNRESOLVED = 6000,
    RM_EVENT_ARP_MALFORMED,
    RM_EVENT_LOG_QUEUE_DROPPED = 9000
} rm_log_event_code_t;

typedef struct rm_log_event {
    uint64_t sequence;
    uint64_t timestamp_ns;
    uint32_t lcore_id;
    uint32_t event_code;
    uint16_t port_id;
    uint8_t level;
    uint8_t module;
    char message[RM_LOG_MESSAGE_SIZE];
} rm_log_event_t;

typedef struct rm_log_stats {
    uint64_t accepted;
    uint64_t dropped;
    uint64_t suppressed;
    uint64_t newest_sequence;
    uint64_t oldest_sequence;
    size_t history_count;
} rm_log_stats_t;

int rm_log_init(void);
void rm_log_shutdown(void);
void rm_log_write(rm_log_level_t level, rm_log_module_t module,
    uint32_t event_code, uint16_t port_id, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));
size_t rm_log_read_recent(rm_log_event_t *out, size_t capacity,
    uint64_t *newest_sequence);
size_t rm_log_read_since(uint64_t after_sequence, rm_log_event_t *out,
    size_t capacity, uint64_t *newest_sequence);
int rm_log_get_stats(rm_log_stats_t *out);
int rm_log_level_parse(const char *text, rm_log_level_t *out);
int rm_log_level_enabled(rm_log_level_t level);
const char *rm_log_level_name(rm_log_level_t level);
const char *rm_log_module_name(rm_log_module_t module);

#define RM_LOG_DEBUG(module, code, port, ...) \
    rm_log_write(RM_LOG_LEVEL_DEBUG, module, code, port, __VA_ARGS__)
#define RM_LOG_INFO(module, code, port, ...) \
    rm_log_write(RM_LOG_LEVEL_INFO, module, code, port, __VA_ARGS__)
#define RM_LOG_WARN(module, code, port, ...) \
    rm_log_write(RM_LOG_LEVEL_WARN, module, code, port, __VA_ARGS__)
#define RM_LOG_ERROR(module, code, port, ...) \
    rm_log_write(RM_LOG_LEVEL_ERROR, module, code, port, __VA_ARGS__)

#endif

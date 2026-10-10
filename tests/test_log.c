#include <assert.h>
#include <string.h>

#include "telemetry/rm_log.h"

static void
test_level_parser(void)
{
    rm_log_level_t level;

    assert(rm_log_level_parse("debug", &level) == 0);
    assert(level == RM_LOG_LEVEL_DEBUG);
    assert(rm_log_level_parse("INFO", &level) == 0);
    assert(level == RM_LOG_LEVEL_INFO);
    assert(rm_log_level_parse("warning", &level) == 0);
    assert(level == RM_LOG_LEVEL_WARN);
    assert(rm_log_level_parse("error", &level) == 0);
    assert(level == RM_LOG_LEVEL_ERROR);
    assert(rm_log_level_parse("verbose", &level) != 0);
    assert(rm_log_level_parse(NULL, &level) != 0);
    assert(rm_log_level_parse("info", NULL) != 0);
}

static void
test_names_and_empty_history(void)
{
    rm_log_event_t events[4];
    rm_log_stats_t stats;
    uint64_t newest = 123;

    assert(strcmp(rm_log_level_name(RM_LOG_LEVEL_WARN), "warn") == 0);
    assert(strcmp(rm_log_module_name(RM_LOG_MODULE_GET), "get") == 0);
    assert(rm_log_read_since(0, events, 4, &newest) == 0);
    assert(newest == 0);
    assert(rm_log_get_stats(&stats) == 0);
    assert(rm_log_get_stats(NULL) != 0);
    assert(stats.accepted == 0);
    assert(stats.dropped == 0);
    assert(stats.suppressed == 0);
    assert(stats.history_count == 0);
}

int
main(void)
{
    test_level_parser();
    test_names_and_empty_history();
    return 0;
}

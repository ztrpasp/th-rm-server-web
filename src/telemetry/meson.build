telemetry_inc = include_directories('../../include')

telemetry_sources = files(
    'rm_metrics.c',
    'rm_log.c',
    'rm_monitor.c',
    'rm_web.c',
)

lib_telemetry = static_library('telemetry', telemetry_sources,
    include_directories: telemetry_inc,
    dependencies: [dpdk_dep, threads_dep],
    install: false
)

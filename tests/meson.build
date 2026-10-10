tests_inc = include_directories('../include')

metrics_status_test = executable(
  'metrics-status-test',
  files('test_metrics_status.c', '../src/tables/rm_status.c'),
  include_directories: tests_inc,
  dependencies: [dpdk_dep, threads_dep],
  link_with: [lib_telemetry],
  install: false,
)

test('metrics-status', metrics_status_test, suite: 'unit')

log_test = executable(
  'log-test',
  files('test_log.c'),
  include_directories: tests_inc,
  dependencies: [dpdk_dep, threads_dep],
  link_with: [lib_telemetry],
  install: false,
)

test('log', log_test, suite: 'unit')

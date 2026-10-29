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

monitor_test = executable(
  'monitor-test',
  files('test_monitor.c'),
  include_directories: tests_inc,
  dependencies: [dpdk_dep, threads_dep],
  link_with: [lib_telemetry],
  install: false,
)

test('monitor', monitor_test, suite: 'unit')

# rte_CoLoR.c 直接编进来；它用到的 cli_print 由测试文件提供空实现
ann_format_test = executable(
  'ann-format-test',
  files('test_ann_format.c', '../src/packets/rte_CoLoR.c',
    '../src/vendor/display_text.c'),
  include_directories: tests_inc,
  dependencies: [dpdk_dep, threads_dep],
  link_with: [lib_telemetry],
  install: false,
)

test('ann-format', ann_format_test, suite: 'unit')

# 不依赖 DPDK：SHA-1（校验 SID 名称）与展示文本清洗
sid_text_test = executable(
  'sid-text-test',
  files('test_sid_text.c', '../src/vendor/sha1.c', '../src/vendor/display_text.c'),
  include_directories: tests_inc,
  install: false,
)

test('sid-text', sid_text_test, suite: 'unit')

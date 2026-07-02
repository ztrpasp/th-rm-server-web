app_inc = include_directories('../../include')

app_sources = files('main.c','init.c')

executable('rm', app_sources,
  include_directories: app_inc,
  dependencies: [ dpdk_dep ],
  link_with: [
    lib_packets,
    lib_process,
    lib_tables,
    lib_vendor,
    lib_commandline
  ],
  link_args: link_args,
  install: true
)

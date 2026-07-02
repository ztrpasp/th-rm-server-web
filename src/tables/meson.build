tables_inc = include_directories('../../include')

tables_sources = files(
    'rm_as_graph.c',
    'rm_control.c',
    'rm_ip_mac.c',
    'rm_status.c',
    'rm_info.c',
    'rm_as_table.c',
    'rm_br_table.c',
    'rm_proxy_table.c',
    'rm_acl_table.c',
    'rm_sid_table.c',
    'rm_tables.c',
    'rm_routing_table.c',
)

lib_tables = static_library('tables', tables_sources,
    include_directories: tables_inc,
    install: false
)

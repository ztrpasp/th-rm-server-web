process_inc = include_directories('../../include')

process_sources = files(
    'rm_pkt_process.c',
    'rm_send.c',
    'send.c'
)

lib_process = static_library('process', process_sources,
    include_directories: process_inc,
    install: false
)
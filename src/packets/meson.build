packets_inc = include_directories('../../include')

packets_sources = files(
    'rte_CoLoR.c',
    'ann.c',
    'arp.c',
    'ctrl.c',
    'get.c',
)

lib_packets = static_library('packets', packets_sources,
    include_directories: packets_inc,
    install: false
)
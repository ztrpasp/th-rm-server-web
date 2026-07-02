vendor_inc = include_directories('../../include')

vendor_sources = files(
    'cuckoo_hash.c',
    'jsw_rbtree.c',
    'lookup3.c',
    'rte_array.c',
    'utils.c'
)

lib_vendor = static_library('vendor', vendor_sources,
    include_directories: vendor_inc,
    install: false
)
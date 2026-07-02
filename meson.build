project('ts','c',
    version : '3.0.0',
    default_options: [
    'buildtype=release',
    'default_library=static',
    'c_args=-mssse3 -w',
    'c_link_args=-mssse3',
    # 'b_sanitize=address',
  ],
)

dpdk_dep = dependency('libdpdk', required: true,
method:'pkg-config',static:true)

link_args = ['-static']

# 所有的头文件都放在include文件夹下
inc = include_directories('include')


subdir('src/packets')
subdir('src/process')
subdir('src/tables')
subdir('src/vendor')
subdir('src/commandline')
subdir('src/app')

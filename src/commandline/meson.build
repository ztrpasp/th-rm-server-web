commandline_inc = include_directories('../../include')

commandline_sources = files(
    'cli.c',
    'cmd_parser.c',
)

lib_commandline = static_library('commandline', commandline_sources,
    include_directories: commandline_inc,
    install: false
)

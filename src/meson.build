sources = [
    'main.c',
    'chunk.c',
    'compiler.c',
    'debug.c',
    'memory.c',
    'scanner.c',
    'value.c',
    'obj.c',
    'vm.c',
    'table.c',
]

want_cc_flags = [
    '-std=c11',
    '-Wall',
    '-Wextra',
    '-Werror',
    '-Wno-unused-parameter',
]

got_cc_flags = []
cc = meson.get_compiler('c')

deps = [cc.find_library('m')]

foreach flag : want_cc_flags
    if cc.has_multi_arguments(flag)
        got_cc_flags += flag
    endif
endforeach

inc_dirs = [
    include_directories('.'),
]

executable(
    'aloxotl',
    sources: sources,
    c_args: got_cc_flags,
    dependencies: deps,
    include_directories: inc_dirs,
    install: true,
)

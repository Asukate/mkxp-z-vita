project('mkxp-z', 'c', 'cpp', version: '2.4.2', meson_version: '>=0.56.0', default_options: ['cpp_std=c++14', 'buildtype=release'])

host_system = host_machine.system()
if host_system != 'vita'
    error('HardRPG builds for PS Vita. Use scripts/build-vita.sh with VitaSDK.')
endif
xxd = find_program('xxd', native: true)
sh_prog = find_program('sh', native: true)
git_hash = run_command('git', 'rev-parse', '--short', 'HEAD', check: true).stdout().strip()
compilers = {'cpp': meson.get_compiler('cpp')}
global_sources = []
global_dependencies = []
global_include_dirs = []
global_link_args = []
global_args = ['-DMKXPZ_BUILD_MESON',
    '-DMKXPZ_VERSION="@0@"'.format(meson.project_version()),
    '-DMKXPZ_GIT_HASH="@0@"'.format(git_hash),
    '-DHAVE_NANOSLEEP', '-DGLES2_HEADER', '-DMKXPZ_INIT_GL_LATER',
    '-Wno-non-virtual-dtor', '-Wno-reorder', '-Wno-uninitialized',
    '-Wno-unknown-pragmas', '-Wno-stringop-truncation']
build_static = true
miniffi = get_option('use_miniffi')
if miniffi
    global_args += '-DMKXPZ_MINIFFI'
endif
if get_option('workdir_current')
    global_args += '-DWORKDIR_CURRENT'
endif
if get_option('cxx11_experimental')
    global_args += '-DMKXPZ_EXP_FS'
endif
if get_option('vita_diagnostics')
    global_args += '-DMKXPZ_VITA_DIAGNOSTICS'
endif
if get_option('vita_skip_event_mutex')
    global_args += '-DMKXPZ_SKIP_EVENT_MUTEX'
endif
if get_option('vita_skip_post_event_log')
    global_args += '-DMKXPZ_SKIP_POST_EVENT_LOG'
endif
subdir('src')
subdir('binding')
subdir('shader')
subdir('assets')
global_include_dirs += include_directories('src', 'binding')
cache_identity = custom_target('shader-cache-build-id',
    output: 'shader-cache-build-id.h',
    command: [find_program('python3', native: true),
              files('scripts/shader-cache-fingerprint.py'),
              meson.current_source_dir(), '@OUTPUT@',
              ' '.join(global_args + get_option('cpp_args'))],
    build_always_stale: true)
global_sources += cache_identity
global_include_dirs += include_directories('.')
executable('mkxp-z', sources: global_sources,
    dependencies: global_dependencies, include_directories: global_include_dirs,
    link_args: global_link_args, cpp_args: global_args, install: false)

physfs = dependency('physfs', version: '>=2.1', static: build_static)
openal = dependency('openal', static: build_static, method: 'pkg-config')
theora = dependency('theora', static: build_static)
vorbisfile = dependency('vorbisfile', static: build_static)
vorbis = dependency('vorbis', static: build_static)
ogg = dependency('ogg', static: build_static)
sdl2 = dependency('SDL2', static: build_static)
freetype = dependency('freetype2', static: build_static)
pixman = dependency('pixman-1', static: build_static)
png = dependency('libpng', static: build_static)
zlib = dependency('zlib', static: build_static)
uchardet = dependency('uchardet', static: build_static)

bz2 = compilers['cpp'].find_library('bz2', dirs: get_option('vita_libdir'), static: true)
iconv = compilers['cpp'].find_library('iconv', dirs: get_option('vita_libdir'), static: true)
global_dependencies += compilers['cpp'].find_library('charset', dirs: get_option('vita_libdir'), static: true)

# If OpenSSL is present, you get HTTPS support
if get_option('enable-https') == true
    openssl = dependency('openssl', required: false, static: build_static)
    if openssl.found() == true
        global_dependencies += openssl
        global_args += '-DMKXPZ_SSL'
    else
        warning('Could not locate OpenSSL. HTTPS will be disabled.')
    endif
endif

global_link_args += ['-static-libgcc', '-static-libstdc++']
global_args += '-DAL_LIBTYPE_STATIC'

alcdev_struct = 'ALCdevice_struct'
if openal.type_name() == 'pkgconfig'
    if openal.version().version_compare('>=1.20.1')
        alcdev_struct = 'ALCdevice'
    endif
endif

global_args += '-DMKXPZ_ALCDEVICE=' + alcdev_struct


global_include_dirs += include_directories('.',
    'audio',
    'crypto',
    'display', 'display/gl', 'display/libnsgif', 'display/libnsgif/utils',
    'etc',
    'filesystem', 'filesystem/ghc',
    'input',
    'net',
    'system',
    'util', 'util/sigslot', 'util/sigslot/adapter'
)

# SDL2 precedes OpenAL so real surface symbols win over legacy aliases.
global_dependencies += [sdl2, openal, zlib, bz2, pixman, physfs, theora, vorbisfile, vorbis, ogg, freetype, png, iconv, uchardet]
if host_system == 'vita'
    # SDL2's Vita backend presents the GLES entry points, while vitaGL owns
    # the actual SGX543/GXM context and swap chain.
    vita_libdir = [get_option('vita_libdir'), get_option('vita_sdk_libdir')]
    if get_option('vita_libdir') == '' or get_option('vita_sdk_libdir') == ''
        error('Vita dependency and SDK library directories must be supplied by scripts/build-vita.sh')
    endif
    vita_gl = compilers['cpp'].find_library('vitaGL', dirs: vita_libdir, static: true, required: true)
    vita_mathneon = compilers['cpp'].find_library('mathneon', dirs: vita_libdir, static: true, required: true)
    vita_appmgr = compilers['cpp'].find_library('SceAppMgr_stub', dirs: vita_libdir, static: true, required: true)
    vita_apputil = compilers['cpp'].find_library('SceAppUtil_stub', dirs: vita_libdir, static: true, required: true)
    vita_ctrl = compilers['cpp'].find_library('SceCtrl_stub', dirs: vita_libdir, static: true, required: true)
    vita_common_dialog = compilers['cpp'].find_library('SceCommonDialog_stub', dirs: vita_libdir, static: true, required: true)
    vita_dmac = compilers['cpp'].find_library('SceKernelDmacMgr_stub', dirs: vita_libdir, static: true, required: true)
    vita_modulemgr = compilers['cpp'].find_library('SceKernelModulemgr_stub', dirs: vita_libdir, static: true, required: true)
    vita_shacc = compilers['cpp'].find_library('SceShaccCg_stub', dirs: vita_libdir, static: true, required: true)
    vita_shark = compilers['cpp'].find_library('vitashark', dirs: vita_libdir, static: true, required: true)
    vita_threadmgr = compilers['cpp'].find_library('SceKernelThreadMgr_stub', dirs: vita_libdir, static: true, required: true)
    global_dependencies += [vita_gl, vita_mathneon, vita_appmgr, vita_apputil, vita_ctrl, vita_common_dialog, vita_dmac, vita_modulemgr, vita_shacc, vita_shark, vita_threadmgr]
    if get_option('native_launcher')
        global_link_args += '-Wl,--wrap=sceCommonDialogUpdate'
        global_dependencies += [
            compilers['cpp'].find_library('SceIme_stub', dirs: vita_libdir, static: true, required: true),
            compilers['cpp'].find_library('SceTouch_stub', dirs: vita_libdir, static: true, required: true),
            compilers['cpp'].find_library('SceSysmodule_stub', dirs: vita_libdir, static: true, required: true),
        ]
    endif

    # SDL keeps its vitaGL references weak so no-GL SDL clients can link
    # without the renderer. Make the production executable pull the real
    # renderer and its lifecycle entry points into the static image.
    global_link_args += ['-Wl,-u,vglInit', '-Wl,-u,vglSwapBuffers', '-Wl,-u,vglGetProcAddress']
endif

if get_option('shared_fluid') == true
    fluidsynth = dependency('fluidsynth', static: build_static)
    add_project_arguments('-DSHARED_FLUID', language: 'cpp')
    global_dependencies += fluidsynth
endif

if get_option('cjk_fallback_font') == true
    add_project_arguments('-DMKXPZ_CJK_FONT', language: 'cpp')
endif

main_source = files(
    'main.cpp',
    'config.cpp',
    'eventthread.cpp',
    'settingsmenu.cpp',
    'sharedstate.cpp',

    'audio/alstream.cpp',
    'audio/audio.cpp',
    'audio/audiostream.cpp',
    'audio/fluid-fun.cpp',
    'audio/midisource.cpp',
    'audio/sdlsoundsource.cpp',
    'audio/soundemitter.cpp',
    'audio/vorbissource.cpp',
    'theoraplay/theoraplay.c',

    'crypto/rgssad.cpp',

    'display/autotiles.cpp',
    'display/autotilesvx.cpp',
    'display/bitmap.cpp',
    'display/font.cpp',
    'display/graphics.cpp',
    'display/plane.cpp',
    'display/sprite.cpp',
    'display/tilemap.cpp',
    'display/tilemapvx.cpp',
    'display/viewport.cpp',
    'display/window.cpp',
    'display/windowvx.cpp',

    'display/libnsgif/libnsgif.c',
    'display/libnsgif/lzw.c',

    'display/gl/gl-debug.cpp',
    'display/gl/gl-fun.cpp',
    'display/gl/gl-meta.cpp',
    'display/gl/glstate.cpp',
    'display/gl/scene.cpp',
    'display/gl/shader.cpp',
    'display/gl/texpool.cpp',
    'display/gl/tileatlas.cpp',
    'display/gl/tileatlasvx.cpp',
    'display/gl/tilequad.cpp',
    'display/gl/vertex.cpp',

    'util/iniconfig.cpp',
    'util/win-consoleutils.cpp',

    'etc/etc.cpp',
    'etc/table.cpp',

    'filesystem/filesystem.cpp',
    'filesystem/filesystemImpl.cpp',

    'input/input.cpp',
    'input/keybindings.cpp',

    'net/LUrlParser.cpp',
    'net/net.cpp',

    'system/systemImpl.cpp'
)

if host_system == 'vita'
    main_source += files('vita_stubs.cpp', 'vita_posix_compat.c', 'vita_livearea.cpp', 'input/vita_input.cpp', 'vita_error_report.cpp', 'vita_resume.cpp', 'vita_runtime_log.c')
    global_link_args += '-Wl,--wrap=_write_r'
    if get_option('native_launcher') and not get_option('proto_launcher')
        error('native_launcher requires proto_launcher')
    endif
    if get_option('proto_launcher')
        main_source += files('vita_proto_launcher.cpp')
        global_args += '-DMKXPZ_PROTO_LAUNCHER'
        if get_option('native_launcher')
            main_source += files('native_launcher/model.cpp', 'native_launcher/ui.cpp', 'native_launcher/view.cpp', 'native_launcher/runtime.cpp', 'native_launcher/text_input.cpp')
            global_args += '-DMKXPZ_NATIVE_LAUNCHER'
        endif
    endif
    if get_option('vita_diagnostics')
        main_source += files('vita_diagnostic.cpp', 'vita_uid_auditor_bridge.c')
        global_link_args += [
            '-Wl,-Map,mkxp-z.map',
            '-Wl,--wrap=sceKernelCreateSema', '-Wl,--wrap=sceKernelDeleteSema',
            '-Wl,--wrap=sceKernelCreateMutex', '-Wl,--wrap=sceKernelDeleteMutex',
            '-Wl,--wrap=sceKernelCreateThread', '-Wl,--wrap=sceKernelDeleteThread',
            '-Wl,--wrap=sceKernelCreateLwMutex', '-Wl,--wrap=sceKernelDeleteLwMutex',
            '-Wl,--wrap=sceKernelCreateEventFlag', '-Wl,--wrap=sceKernelDeleteEventFlag',
            '-Wl,--wrap=sceKernelCreateMsgPipe', '-Wl,--wrap=sceKernelDeleteMsgPipe',
            '-Wl,--wrap=sceKernelAllocMemBlock', '-Wl,--wrap=sceKernelFreeMemBlock',
            '-Wl,--wrap=sem_init', '-Wl,--wrap=sem_destroy',
            '-Wl,--wrap=pthread_mutex_init', '-Wl,--wrap=pthread_mutex_destroy',
            '-Wl,--wrap=pthread_cond_init', '-Wl,--wrap=pthread_cond_destroy',
        ]
    endif
    global_args += ['-D_GNU_SOURCE=1']
endif

global_sources += main_source

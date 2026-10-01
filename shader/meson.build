
embedded_shaders = [
    'common.h',
    'transSimple.frag',
    'trans.frag',
    'hue.frag',
    'sprite.frag',
    'plane.frag',
    'gray.frag',
    'bitmapBlit.frag',
    'flatColor.frag',
    'simple.frag',
    'simpleColor.frag',
    'simpleAlpha.frag',
    'simpleAlphaUni.frag',
    'tilemap.frag',
    'flashMap.frag',
    'bicubic.frag',
    'lanczos3.frag',
    'minimal.vert',
    'simple.vert',
    'simpleColor.vert',
    'sprite.vert',
    'tilemap.vert',
    'tilemapvx.vert',
    'blur.frag',
    'blurH.vert',
    'blurV.vert',
    'simpleMatrix.vert',
    'kglInvert.frag',
    'kglCompressAlpha.frag',
    'kglSubtract.frag',
    'kglShadowH.frag',
    'kglShadowV.frag',
]

# xBRZ shader is GPLv3.
if get_option('enable-https') == true
    embedded_shaders += 'xbrz.frag'
endif

embedded_shaders_f = files(embedded_shaders)

count = 0
foreach file : embedded_shaders_f
    shader_sym = '___shader_' + embedded_shaders[count].underscorify()
    global_sources += custom_target(embedded_shaders[count],
        input: file,
        output: '@0@.xxd'.format(embedded_shaders[count]),
        command: [
            sh_prog, '-c',
            'xxd -i "$1" | sed -e "s/^unsigned char [A-Za-z0-9_]*/unsigned char ' + shader_sym + '/" -e "s/^unsigned int [A-Za-z0-9_]*/unsigned int ' + shader_sym + '_len/"',
            'sh', '@INPUT@'
        ],
        capture: true,
        depend_files: embedded_shaders_f[count]
        )
    count += 1
endforeach

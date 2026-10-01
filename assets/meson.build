embedded_assets = [
    'icon.png',
    'gamecontrollerdb.txt'
]

if get_option('cjk_fallback_font') == true
    embedded_assets += 'wqymicrohei.ttf'
else
    embedded_assets += 'liberation.ttf'
endif

# Generic Vita font-fallback pack (SIL OFL, metric-compatible with the
# licensed Windows families games commonly request). Registered as
# always-present font sets and consulted via the built-in substitution
# table in src/display/font.cpp only when the requested family is absent
# from the game's own Fonts/ and from per-game fontSub.
embedded_assets += 'liberation-serif.ttf'
embedded_assets += 'liberation-mono.ttf'

embedded_assets_f = files(embedded_assets)

count = 0
foreach file : embedded_assets_f
    asset_sym = '___assets_' + embedded_assets[count].underscorify()
    global_sources += custom_target(embedded_assets[count],
    input: file,
    output: '@0@.xxd'.format(embedded_assets[count]),
    command: [
        sh_prog, '-c',
        'xxd -i "$1" | sed -e "s/^unsigned char [A-Za-z0-9_]*/unsigned char ' + asset_sym + '/" -e "s/^unsigned int [A-Za-z0-9_]*/unsigned int ' + asset_sym + '_len/"',
        'sh', '@INPUT@'
    ],
    capture: true,
    depend_files: embedded_assets_f[count]
    )
    count += 1
endforeach

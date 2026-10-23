#!/usr/bin/env python3
"""Host regression probes for ZIP UI responsiveness and encrypted archive lookup."""
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
WORK = Path(sys.argv[1]).resolve()
WORK.mkdir(parents=True, exist_ok=True)
FLAGS = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'physfs'], text=True))

def compile(name, sources, extra=()):
    out = WORK / name
    subprocess.check_call([os.environ.get('CXX', 'g++'), '-std=c++14', '-O2',
        '-I'+str(ROOT/'src'), '-I'+str(ROOT/'src/util'),
        *map(lambda x: str(ROOT/x), sources), *extra, *FLAGS, '-pthread', '-o', str(out)])
    return out

def word(value):
    return struct.pack('<I', value & 0xffffffff)

def next_key(key):
    return (key * 7 + 3) & 0xffffffff

def encrypt(data, key):
    result = bytearray()
    for offset in range(0, len(data), 4):
        block = data[offset:offset+4]
        result.extend(a ^ b for a, b in zip(block, word(key)))
        key = next_key(key)
    return bytes(result)

FILES = [('Graphics/Characters/Hero.png', b'hero'),
         ('Case/Foo.txt', b'upper'), ('Case/foo.txt', b'lower'),
         ('Exact/child.bin', b'child'), ('exact', b'file'),
         ('Graphics/日本.png', b'unicode'), ('Graphics/É.png', b'accent')]

def archive_v1(files=FILES):
    out = bytearray(b'RGSSAD\0\1')
    key = 0xdeadcafe
    for name, payload in files:
        name = name.encode()
        out.extend(word(len(name) ^ key)); key = next_key(key)
        for byte in name:
            out.append(byte ^ (key & 255)); key = next_key(key)
        out.extend(word(len(payload) ^ key)); key = next_key(key)
        out.extend(encrypt(payload, key))
    return out

def archive_v3():
    names = [name.encode() for name, _ in FILES]
    base = 0x12345
    key = base * 9 + 3
    offset = 12 + sum(16+len(n) for n in names) + 4
    out = bytearray(b'RGSSAD\0\3' + word(base)); data = bytearray()
    for i, ((_, payload), name) in enumerate(zip(FILES, names)):
        magic = 0x76543210+i
        for value in [offset, len(payload), magic, len(name)]: out.extend(word(value ^ key))
        out.extend(b ^ word(key)[i % 4] for i, b in enumerate(name))
        data.extend(encrypt(payload, magic)); offset += len(payload)
    out.extend(word(key)); out.extend(data)
    return out

LAUNCHER = shlex.split(subprocess.check_output(
    ['pkg-config', '--cflags', '--libs', 'libpng', 'libturbojpeg', 'libwebp', 'libcurl'], text=True))
lifecycle = compile('zip-lifecycle', ['tests/native-zip-lifecycle.cpp',
    'src/native_launcher/model.cpp', 'src/native_launcher/ui.cpp',
    'src/native_launcher/home.cpp', 'src/native_launcher/art_fetch.cpp',
    'src/native_launcher/image.cpp', 'src/native_launcher/text_input.cpp',
    'src/native_launcher/welcome.cpp', 'src/native_launcher/i18n.cpp'],
    ['-Wl,--wrap=PHYSFS_mount', '-Wl,--wrap=PHYSFS_mountIo', *LAUNCHER])
casefold = compile('rgss-casefold', ['tests/rgss-casefold.cpp', 'src/crypto/rgssad.cpp'])
# The Vita build keeps RGSS1/2 entry tables in ux0:/data/hardrpg/cache;
# on the desktop that is a relative path, so this runs in its own folder.
casefold_cached = compile('rgss-casefold-cached', ['tests/rgss-casefold.cpp', 'src/crypto/rgssad.cpp'],
                          ['-D__vita__', '-lz'])
with tempfile.TemporaryDirectory(prefix='archive-feedback-', dir=WORK) as temporary:
    parent = Path(temporary)
    for wrapped in [False, True]:
        root = parent / ('wrapped' if wrapped else 'plain')
        (root/'games').mkdir(parents=True); (root/'rtp').mkdir()
        prefix = 'Wrapper/' if wrapped else ''
        with zipfile.ZipFile(root/'games/Fixture.zip', 'w', zipfile.ZIP_DEFLATED) as z:
            z.writestr(prefix+'RPG_RT.ini', '[RPG_RT]\nGameTitle=Fixture\n')
            z.writestr(prefix+'RPG_RT.ldb', b'\x0bLcfDataBase\0')
            z.writestr(prefix+'RPG_RT.lmt', b'\x0aLcfMapTree\0')
            for n in range(30): z.writestr(prefix+f'Images/Folder{n}/sample.bin', b'x'*4000)
        with zipfile.ZipFile(root/'rtp/RPGXP.zip', 'w') as z:
            z.writestr('Graphics/System/Window.png', b'image')
            z.writestr('Audio/SE/Cursor.ogg', b'audio')
        for engine in ['2000', '2003']:
            pack = root/'rtp'/engine; pack.mkdir()
            with zipfile.ZipFile(pack/(engine+'.zip'), 'w') as z:
                z.writestr('System/Window.png', b'image')
                z.writestr('Music/Track.ogg', b'audio')
        subprocess.check_call([str(lifecycle), str(root)])
    for extension, data in [('rgssad', archive_v1()), ('rgss2a', archive_v1()), ('rgss3a', archive_v3())]:
        archive = parent / ('Fixture.'+extension); archive.write_bytes(data)
        subprocess.check_call([str(casefold), str(archive)])
    # The cached table is used only while the archive is unchanged: the same
    # path, size and modification time, the same first entry and the same end.
    cache_root = parent / 'cache-run'; (cache_root/'ux0:').mkdir(parents=True)
    index_dir = cache_root/'ux0:/data/hardrpg/cache/rgssad-v1'
    archive = cache_root / 'Fixture.rgssad'
    def check(files, mtime):
        archive.write_bytes(archive_v1(files)); os.utime(archive, (mtime, mtime))
        subprocess.check_call([str(casefold_cached), str(archive)], cwd=cache_root)
    check(FILES, 1_600_000_000)
    indexes = list(index_dir.glob('*.idx'))
    assert len(indexes) == 1, indexes
    check(FILES, 1_600_000_000)  # read from the table
    # Same size and time, other order: the first entry differs, so it rescans.
    check(list(reversed(FILES)), 1_600_000_000)
    # Same first entry, other order of the rest, newer time: rescans.
    check(FILES[:1] + list(reversed(FILES[1:])), 1_600_000_100)
    # A damaged table is ignored and replaced.
    damaged = bytearray(indexes[0].read_bytes()); damaged[20] ^= 0xff
    indexes[0].write_bytes(damaged)
    check(FILES[:1] + list(reversed(FILES[1:])), 1_600_000_100)
    assert indexes[0].read_bytes() != bytes(damaged)
print('ZIP and RGSS archive regression probes passed')

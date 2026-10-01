#!/usr/bin/env python3
"""Build-time identity for Vita program binaries; no device-side ELF hashing."""
import hashlib, os, sys
from pathlib import Path
root, output = map(Path, sys.argv[1:3])
sdk = Path(os.environ['VITASDK'])
deps = Path(os.environ['VITA_DEP_PREFIX'])
h = hashlib.sha256()
paths = [root/'meson.build', Path(__file__)]
for directory in ('src', 'binding', 'shader'):
    paths += sorted(p for p in (root/directory).rglob('*')
                    if p.is_file() and p.suffix in ('.h', '.hpp', '.c', '.cpp', '.vert', '.frag'))
paths += [deps/'lib'/name for name in ('libvitaGL.a', 'libvitashark.a')]
paths += [sdk/'arm-vita-eabi/lib/libSceShaccCg_stub.a', deps/'include/vitaGL.h']
for path in paths:
    data = path.read_bytes()
    h.update(path.name.encode()+b'\0'+len(data).to_bytes(8,'little')+data)
h.update('\0'.join(sys.argv[3:]).encode())
text = '#define MKXPZ_SHADER_CACHE_BUILD "'+h.hexdigest()+'"\n'
if not output.exists() or output.read_text() != text:
    output.write_text(text)

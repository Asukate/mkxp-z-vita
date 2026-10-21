#!/usr/bin/env python3
"""Identity of the Vita program-binary format; no device-side ELF hashing."""
import hashlib, os, sys
from pathlib import Path
root, output = map(Path, sys.argv[1:3])
sdk = Path(os.environ['VITASDK'])
deps = Path(os.environ['VITA_DEP_PREFIX'])
h = hashlib.sha256()
# Only what can change a linked program binary: the shader sources, the code that
# assembles them and binds attributes (shader.cpp/.h), and the GL/compiler libraries.
# Every cache entry also stores the exact sources, so an unrelated engine change
# keeps the cache and the next boot skips the shader compiler.
paths = [Path(__file__)]
paths += sorted(p for p in (root/'shader').rglob('*') if p.is_file())
paths += [root/'src/display/gl'/name for name in ('shader.cpp', 'shader.h', 'program-cache.h')]
paths += [deps/'lib'/name for name in ('libvitaGL.a', 'libvitashark.a')]
paths += [sdk/'arm-vita-eabi/lib/libSceShaccCg_stub.a', deps/'include/vitaGL.h']
for path in paths:
    data = path.read_bytes()
    h.update(path.name.encode()+b'\0'+len(data).to_bytes(8,'little')+data)
h.update('\0'.join(sys.argv[3:]).encode())
text = '#define MKXPZ_SHADER_CACHE_BUILD "'+h.hexdigest()+'"\n'
if not output.exists() or output.read_text() != text:
    output.write_text(text)

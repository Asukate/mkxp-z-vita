#!/usr/bin/env python3
"""Exercise memory-mode validation on a real EasyRPG-enabled launcher VPK."""
import argparse
import copy
from pathlib import Path
import subprocess
import struct
import zipfile

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('vpk', type=Path)
parser.add_argument('out', type=Path)
a = parser.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(a.vpk) as source:
    sfo = bytearray(source.read('sce_sys/param.sfo'))
    _, _, keys, values, count = struct.unpack_from('<5I', sfo)
    for i in range(count):
        key, fmt, size, capacity, offset = struct.unpack_from('<HHIII', sfo, 20 + 16*i)
        name = sfo[keys+key:].split(b'\0', 1)[0]
        if name == b'ATTRIBUTE2':
            assert fmt == 0x404 and size == 4
            address = values + offset
            break
    else:
        raise AssertionError('missing ATTRIBUTE2')
    for mode in (0, 4, 8, 12):
        struct.pack_into('<I', sfo, address, mode)
        fixture = a.out / ('memory-'+str(mode)+'.vpk')
        with zipfile.ZipFile(fixture, 'w') as target:
            for entry in source.infolist():
                target.writestr(copy.copy(entry), sfo if entry.filename == 'sce_sys/param.sfo' else source.read(entry))
        result = subprocess.run(['python3', str(ROOT/'scripts/verify-launcher-payload.py'),
            str(fixture)], text=True, capture_output=True)
        if mode == 12:
            assert result.returncode == 0, result.stderr
        else:
            assert result.returncode != 0, 'unsafe EasyRPG memory mode accepted: '+str(mode)
            assert 'EasyRPG requires ATTRIBUTE2=12' in result.stderr, result.stderr
print('Actual VPK memory modes 0/4/8 rejected, 12 accepted')

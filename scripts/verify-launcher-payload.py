#!/usr/bin/env python3
"""Verify the launcher VPK's nonbinary payload against its release manifest."""

import hashlib
import pathlib
import subprocess
import struct
import sys
import zipfile


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(f"usage: {sys.argv[0]} <launcher.vpk> [title-id]", file=sys.stderr)
        return 2
    root = pathlib.Path(__file__).resolve().parent.parent
    manifest = root / "launcher/release-payload.sha256"
    expected = {}
    for line in manifest.read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        expected[name] = digest
    with zipfile.ZipFile(sys.argv[1]) as vpk:
        names = set(vpk.namelist())
        if len(names) != len(vpk.namelist()):
            print("duplicate entries in VPK", file=sys.stderr)
            return 1
        sfo = vpk.read('sce_sys/param.sfo')
        magic, version, keys, values, count = struct.unpack_from('<5I', sfo)
        if magic != 0x46535000 or version != 0x101:
            raise ValueError('invalid SFO header')
        metadata = {}
        for i in range(count):
            key, fmt, size, capacity, offset = struct.unpack_from('<HHIII', sfo, 20 + 16 * i)
            name = sfo[keys + key:].split(b'\0', 1)[0].decode()
            if fmt == 0x204:
                metadata[name] = sfo[values + offset:values + offset + size].rstrip(b'\0').decode()
        if metadata.get('TITLE') != 'HardRPG':
            raise ValueError('VPK title must be HardRPG')
        if metadata.get('APP_VER') != '00.10':
            raise ValueError('VPK app version must match HardRPG 0.1 Alpha')
        if len(sys.argv) == 3 and metadata.get('TITLE_ID') != sys.argv[2]:
            raise ValueError('VPK Title ID does not match requested package')
        if not vpk.read('eboot.bin'):
            raise ValueError('empty eboot.bin')
        wanted = set(expected) | {"eboot.bin", "sce_sys/param.sfo", "stub/Data/Scripts.rvdata2"}
        if names != wanted:
            print(f"VPK contents differ: missing={sorted(wanted - names)} extra={sorted(names - wanted)}", file=sys.stderr)
            return 1
        for name, digest in expected.items():
            actual = hashlib.sha256(vpk.read(name)).hexdigest()
            if actual != digest:
                print(f"release payload mismatch: {name}: {actual}", file=sys.stderr)
                return 1
        picker = subprocess.run(
            ["ruby", "-rzlib", "-e",
             "x=Marshal.load(STDIN.read); "
             "abort 'invalid picker entries' unless x.is_a?(Array) && x.size==2 && "
             "x.each_with_index.all? { |e,i| e.is_a?(Array) && e.size==3 && e[0]==i+1 && "
             "e[1]==%w[library.rb picker.rb][i] }; STDOUT.binmode; "
             "STDOUT.write(x.map { |e| Zlib.inflate(e[2]) }.join)"],
            input=vpk.read("stub/Data/Scripts.rvdata2"), capture_output=True,
        )
        source = b''.join((root / 'launcher' / name).read_bytes() for name in ['library.rb', 'picker.rb'])
        if picker.returncode or picker.stdout != source:
            print("release payload mismatch: picker source", file=sys.stderr)
            return 1
    print("release launcher payload matches")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

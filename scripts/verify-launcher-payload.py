#!/usr/bin/env python3
"""Verify the launcher VPK's nonbinary payload against its release manifest."""

import json
import argparse
import hashlib
import pathlib
import re
import subprocess
import struct
import sys
import zipfile


def verify_livearea_image(data: bytes, name: str) -> None:
    """Reject system artwork that the Vita package installer cannot accept."""
    dimensions = {
        "sce_sys/icon0.png": (128, 128),
        "sce_sys/pic0.png": (960, 544),
        "sce_sys/livearea/contents/bg0.png": (840, 500),
        "sce_sys/livearea/contents/startup.png": (280, 158),
    }
    if data[:8] != b"\x89PNG\r\n\x1a\n" or len(data) < 33:
        raise ValueError(f"invalid LiveArea PNG: {name}")
    width, height, depth, color, compression, filtering, interlace = struct.unpack_from(
        ">IIBBBBB", data, 16
    )
    if (width, height) != dimensions[name] or depth != 8 or color != 3:
        raise ValueError(f"LiveArea artwork needs the correct dimensions and an 8-bit indexed palette: {name}")
    if len(data) > 420 * 1024 or compression or filtering or interlace:
        raise ValueError(f"unsupported LiveArea PNG encoding or size: {name}")


def developer_home_paths(binary):
    # These exact assertion filenames are embedded in VitaSDK's upstream libc.
    sdk_paths = {
        b'/home/runner/work/autobuilds/autobuilds/buildscripts/build/newlib-prefix/src/newlib/newlib/libc/stdlib/' + name
        for name in (b'dtoa.c', b'gdtoa-gethex.c', b'mprec.c')
    }
    return [p for p in re.findall(rb'(?:/home/|/Users/|[A-Za-z]:\\Users\\)[^\x00\r\n]+', binary)
            if p not in sdk_paths]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vpk')
    parser.add_argument('title_id', nargs='?')
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    manifest = root / "launcher/release-payload.sha256"
    expected = {}
    for line in manifest.read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        expected[name] = digest
    with zipfile.ZipFile(args.vpk) as vpk:
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
            elif fmt == 0x404 and size == 4:
                metadata[name] = struct.unpack_from('<I', sfo, values + offset)[0]
        if metadata.get('TITLE') != 'HardRPG':
            raise ValueError('VPK title must be HardRPG')
        release = (root / 'src/native_launcher/release.h').read_text()
        app_version = re.search(r'^#define HARDRPG_APP_VER "([0-9]{2}\.[0-9]{2})"$', release, re.M)
        native = "launcher-backend.txt" in names
        from importlib.util import spec_from_file_location, module_from_spec
        overlay_spec = spec_from_file_location('livearea_version', root/'scripts/build-livearea.py')
        overlay = module_from_spec(overlay_spec); overlay_spec.loader.exec_module(overlay)
        label = overlay.release_version(root/'src/native_launcher/release.h') if native else '0.2.0 Alpha'
        expected['sce_sys/livearea/contents/template.xml'] = hashlib.sha256(overlay.render(
            (root/'launcher/livearea/template.xml').read_bytes(), label)).hexdigest()
        expected_version = app_version[1] if native and app_version else '00.20'
        if not app_version or metadata.get('APP_VER') != expected_version:
            raise ValueError('VPK app version must match release metadata')
        if args.title_id and metadata.get('TITLE_ID') != args.title_id:
            raise ValueError('VPK Title ID does not match requested package')
        executable = vpk.read('eboot.bin')
        if not executable:
            raise ValueError('empty eboot.bin')
        # Source/assertion strings survive debug stripping in static libraries.
        if developer_home_paths(executable):
            raise ValueError('VPK contains a developer home path; rebuild in the /source and /work CI layout')
        native = "launcher-backend.txt" in names
        if native:
            if vpk.read("launcher-backend.txt") != (root / "launcher/native-backend.txt").read_bytes():
                raise ValueError("invalid native backend marker")
            if b"HardRPG native launcher backend" not in executable:
                raise ValueError("native launcher package needs a native-enabled engine")
            expected.pop("stub/Game.ini")
            expected["font.ttf"] = hashlib.sha256((root / "assets/liberation.ttf").read_bytes()).hexdigest()
        picker_names = {"launcher-backend.txt"} if native else {"stub/Data/Scripts.rvdata2"}
        if 'easyrpg.self' in names or 'runtime/easyrpg-manifest.json' in names:
            if metadata.get('ATTRIBUTE2', 0) & 12 != 12:
                raise ValueError('EasyRPG requires ATTRIBUTE2=12 extended memory flags')
            if not native:
                raise ValueError('EasyRPG requires the native launcher')
            from importlib.util import spec_from_file_location, module_from_spec
            spec = spec_from_file_location('easyrpg_state', root/'scripts/easyrpg-state.py')
            state = module_from_spec(spec); spec.loader.exec_module(state)
            backend = json.loads(vpk.read('runtime/easyrpg-manifest.json'))
            if backend.get('schema') != 1 or backend.get('sources') != state.REVISIONS:
                raise ValueError('EasyRPG source revisions differ')
            if backend.get('inputs') != state.inputs():
                raise ValueError('EasyRPG runtime inputs differ')
            binary = vpk.read('easyrpg.self')
            if not binary or b'HardRPG EasyRPG backend' not in binary:
                raise ValueError('EasyRPG runtime needs the HardRPG entry point')
            if hashlib.sha256(binary).hexdigest() != backend['runtime_sha256']:
                raise ValueError('EasyRPG runtime hash differs')
            if developer_home_paths(binary):
                raise ValueError('EasyRPG runtime contains a developer home path')
            picker_names |= {'easyrpg.self','runtime/easyrpg-manifest.json'}
            for name, digest in backend['licenses'].items():
                if pathlib.Path(name).name != name:
                    raise ValueError('invalid EasyRPG license path')
                expected['licenses/easyrpg/'+name] = digest
        if metadata.get('ATTRIBUTE2', 0) & 12 != 12:
            raise ValueError('the engine requires ATTRIBUTE2=12 extended memory flags')
        wanted = set(expected) | {"eboot.bin", "sce_sys/param.sfo"} | picker_names
        if names != wanted:
            print(f"VPK contents differ: missing={sorted(wanted - names)} extra={sorted(names - wanted)}", file=sys.stderr)
            return 1
        for name, digest in expected.items():
            if name.startswith("sce_sys/") and name.endswith(".png"):
                verify_livearea_image(vpk.read(name), name)
            actual = hashlib.sha256(vpk.read(name)).hexdigest()
            if actual != digest:
                print(f"release payload mismatch: {name}: {actual}", file=sys.stderr)
                return 1
        if not native:
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

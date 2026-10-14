#!/usr/bin/env python3
"""Record and verify the EasyRPG runtime and its exact build inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
INPUTS = ['backends/easyrpg/main.cpp', 'src/native_launcher/easyrpg_handoff.h',
          'src/util/json5pp.hpp', 'deps/patches/easyrpg/0.8.1.1-hardrpg.patch',
          'scripts/build-easyrpg-vita.sh']
REVISIONS = {'Player': '78328fa29f465315291e161130e6682f69410370',
             'liblcf': '92c4450a1bc1acb58bd02bbb99b57e5036919cdf'}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs():
    return {name: digest(ROOT/name) for name in INPUTS}


def record(ws, prefix, source):
    if source is None or digest(source/'src/platform/psvita/main.cpp') != digest(ROOT/'backends/easyrpg/main.cpp'):
        raise ValueError('staged EasyRPG entry point differs from current build inputs')
    sources = {}
    for name, revision in REVISIONS.items():
        source = ws/'upstream'/name
        head = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
        dirty = subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True)
        if head != revision or dirty:
            raise ValueError(f'changed upstream source: {source}')
        sources[name] = head
    runtime = ws/'runtime'
    notices = runtime/'licenses'
    notices.mkdir(exist_ok=True)
    player = ws/'upstream/Player'
    for source, dest in {
        player/'COPYING': 'EasyRPG-GPL-3.0.txt',
        player/'docs/AUTHORS.md': 'EasyRPG-AUTHORS.md',
        player/'README.md': 'EasyRPG-NOTICES.md',
        player/'resources/ttyp0/LICENSE': 'EasyRPG-ttyp0.txt',
        player/'resources/shinonome/korean/COPYING': 'EasyRPG-Baekmuk.txt',
        player/'resources/wenquanyi/doc/COPYING': 'EasyRPG-WenQuanYi.txt',
        player/'resources/wenquanyi/README': 'EasyRPG-WenQuanYi-README.txt',
        player/'resources/shinonome/README': 'EasyRPG-Shinonome.txt',
        player/'src/midisynth.h': 'EasyRPG-FmMidi-BSD.txt',
        ws/'upstream/liblcf/COPYING': 'liblcf-MIT.txt',
    }.items():
        shutil.copyfile(source, notices/dest)
    # Full headers preserve the embedded FmMidi and dr_wav notices.
    for pattern in ['*fmmidi*', '*dr_wav*']:
        for source in player.glob('src/**/'+pattern):
            if source.suffix in ('.h', '.hpp'):
                shutil.copyfile(source, notices/('EasyRPG-'+source.name+'.txt'))
    manifest = {
        'schema': 1, 'player_version': '0.8.1.1', 'sources': sources,
        'inputs': inputs(), 'runtime_sha256': digest(runtime/'easyrpg.bin'),
        'licenses': {p.name: digest(p) for p in sorted(notices.iterdir())},
        'dependency_archives': {str(p.relative_to(prefix)): digest(p)
                                for p in sorted((prefix/'lib').glob('*.a'))},
    }
    (runtime/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')


def verify(ws):
    runtime = ws/'runtime'
    manifest = json.loads((runtime/'manifest.json').read_text())
    if manifest.get('schema') != 1 or manifest.get('sources') != REVISIONS:
        raise ValueError('invalid EasyRPG source revisions')
    if manifest.get('inputs') != inputs():
        raise ValueError('EasyRPG build inputs changed; rebuild the runtime')
    if manifest['runtime_sha256'] != digest(runtime/'easyrpg.bin'):
        raise ValueError('EasyRPG runtime changed after build')
    for name, expected in manifest['licenses'].items():
        if Path(name).name != name or digest(runtime/'licenses'/name) != expected:
            raise ValueError('EasyRPG license payload changed')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['record', 'verify'])
    parser.add_argument('workspace', type=Path)
    parser.add_argument('prefix', type=Path, nargs='?')
    parser.add_argument('--source', type=Path)
    args = parser.parse_args()
    try:
        if args.action == 'record':
            if args.prefix is None: raise ValueError('record requires a dependency prefix')
            record(args.workspace.resolve(), args.prefix.resolve(), args.source)
        else:
            verify(args.workspace.resolve())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'EasyRPG state: {error}\n')

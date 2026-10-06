#!/usr/bin/env python3
"""Reject mixed dependency profiles and packaging of stale engine binaries."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def digest(paths):
    value = hashlib.sha256()
    for path in sorted(set(paths)):
        if path.is_file() and '__pycache__' not in path.parts:
            value.update(str(path.relative_to(ROOT)).encode() + b'\0')
            value.update(path.read_bytes())
    return value.hexdigest()


def inputs(*names):
    result = []
    for name in names:
        path = ROOT / name
        result.extend(path.rglob('*') if path.is_dir() else [path])
    return result


def state(kind, ws, profile):
    sdk = Path(os.environ['VITASDK']).resolve()
    compiler = subprocess.check_output([str(sdk / 'bin/arm-vita-eabi-gcc'), '--version'], text=True)
    result = {'profile': profile, 'sdk': str(sdk), 'compiler': compiler.splitlines()[0]}
    version = sdk / 'version_info.txt'
    result['sdk_version'] = version.read_text() if version.exists() else 'unavailable'
    if kind == 'dependencies':
        result['inputs_sha256'] = digest(inputs('deps', 'linux/toolchain-vita.cmake',
            'scripts/bootstrap-vita.sh', 'scripts/ruby-merge-archive.sh', 'scripts/ruby-install-headers.sh',
            ))
    else:
        options = json.loads((ws / f'build/mkxp-z-parity-{profile}/meson-info/intro-buildoptions.json').read_text())
        result['native_launcher'] = next(o['value'] for o in options if o['name'] == 'native_launcher')
        result['revision'] = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
        result['inputs_sha256'] = digest(inputs('src', 'binding', 'shader', 'assets', 'launcher',
            'scripts', 'linux', 'meson.build', 'meson_options.txt'))
        dep_state = ws / 'dependencies-state.json'
        result['dependencies_sha256'] = hashlib.sha256(dep_state.read_bytes()).hexdigest()
        elf = ws / f'build/mkxp-z-parity-{profile}/mkxp-z'
        result['elf_sha256'] = hashlib.sha256(elf.read_bytes()).hexdigest()
        # Record every installed archive, not only the source recipe.
        result['archives'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in sorted((ws / 'prefix/lib').glob('*.a'))}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=['dependencies', 'engine'])
    parser.add_argument('workspace', type=Path)
    parser.add_argument('profile', choices=['real-vita', 'vita3k'])
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    ws = args.workspace.resolve()
    file = ws / f'{args.kind}-state.json'
    try:
        expected = state(args.kind, ws, args.profile)
        if args.verify or (args.kind == 'dependencies' and file.exists()):
            if json.loads(file.read_text()) != expected:
                parser.exit(1, f'{args.kind} inputs changed: use a fresh workspace for dependency changes, '
                               'or rebuild the engine before packaging.\n')
        else:
            if args.kind == 'dependencies' and (ws / 'prefix/lib').exists() and any((ws / 'prefix/lib').glob('*.a')):
                parser.exit(1, 'Unrecorded dependency archives: choose a fresh --ws directory.\n')
            ws.mkdir(parents=True, exist_ok=True)
            file.write_text(json.dumps(expected, indent=2, sort_keys=True) + '\n')
    except (OSError, KeyError, ValueError, subprocess.SubprocessError) as exc:
        parser.exit(1, f'Cannot verify {args.kind} build state: {exc}\n')


if __name__ == '__main__':
    main()

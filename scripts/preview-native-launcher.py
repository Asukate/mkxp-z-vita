#!/usr/bin/env python3
"""Build and preview the native HardRPG interface in a separate host library."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--work', type=Path, required=True)
parser.add_argument('--demo', action='store_true', help='create metadata-only demo games')
parser.add_argument('--data-root', type=Path, help='use a copied test library')
parser.add_argument('--browse-root', type=Path, action='append', default=[])
parser.add_argument('--keys', default='', help='comma-separated Vita button names')
parser.add_argument('--screenshot', type=Path)
parser.add_argument('--interactive', action='store_true')
args = parser.parse_args()
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)
binary = work / 'hardrpg-native'
sources = [*ROOT.glob('src/native_launcher/*'), ROOT/'src/vita_proto_launcher.cpp',
           ROOT/'src/vita_proto_launcher.h', ROOT/'src/util/json5pp.hpp', ROOT/'scripts/build-native-launcher.sh']
if not binary.exists() or any(p.stat().st_mtime > binary.stat().st_mtime for p in sources if p.is_file()):
    subprocess.run([str(ROOT/'scripts/build-native-launcher.sh'), str(binary)], check=True)
data = args.data_root.resolve() if args.data_root else work/'hardrpg'
if args.demo:
    for title, version in [('Ao Oni',1),('BLACK SOULS',3),('BLACK SOULS II',3),('Hylics',3),
                           ("The Witch’s House",2),('To the Moon',1),('Pokémon Infinite Fusion',1),('Red Hood Woods',3)]:
        directory = data/'games'/title
        directory.mkdir(parents=True, exist_ok=True)
        ini = directory/'Game.ini'
        if not ini.exists():
            ini.write_text(f'[Game]\nTitle={title}\nLibrary=RGSS{version}01.dll\n')
cmd = [str(binary),'--root',str(data),'--font',str(ROOT/'assets/liberation.ttf'),
       '--storage',str(data.parent)+'/', '--actions',args.keys]
for path in args.browse_root:
    cmd.extend(['--storage',str(path.resolve()).rstrip('/')+'/'])
if args.screenshot:
    args.screenshot.parent.mkdir(parents=True,exist_ok=True)
    cmd.extend(['--snapshot',str(args.screenshot.resolve())])
if args.interactive or not args.screenshot:
    cmd.append('--interactive')
subprocess.run(cmd,check=True,cwd=ROOT)

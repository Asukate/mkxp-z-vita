#!/usr/bin/env python3
"""Build an original tile-map probe using user-provided VX Ace player scripts."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
parser.add_argument('--stock-dir', type=Path, required=True)
parser.add_argument('--engine', choices=('xp','vx','ace'), default='ace')
args = parser.parse_args()
args.output = args.output.resolve()
root = Path(__file__).resolve().parent.parent
game = args.output / ('HardRPG Tap Walk Probe ' + args.engine.upper())
(game / 'Data').mkdir(parents=True, exist_ok=True)
if args.engine == 'xp':
    originals = [next(args.stock_dir.glob('*-'+name+'.rb')) for name in
                 ('Game_Character_1','Game_Character_2','Game_Character_3','Game_Player')]
else:
    originals = [args.stock_dir / name for name in
                 (('027.rb','030.rb') if args.engine == 'vx' else ('029.rb','030.rb','031.rb'))]
scripts = originals + [root / 'tests/fixtures/tap_walk_world.rb',
                      root / 'tests/fixtures/tap_walk_legacy_world.rb',
                      root / 'launcher/vita_tap_to_walk.rb',
                      root / 'tests/fixtures/tap_walk_probe.rb']
extension, library = {'xp':('rxdata','RGSS102E.dll'), 'vx':('rvdata','RGSS202E.dll'),
                      'ace':('rvdata2','RGSS301.dll')}[args.engine]
subprocess.run(['ruby', '-rzlib', '-e',
    'a=[[0,"Probe Engine",Zlib.deflate("$hardrpg_probe_engine = :"+ARGV[1])]]; a+=ARGV[2..-1].each_with_index.map {|p,i| [i+1,File.basename(p),Zlib.deflate(File.binread(p))]}; File.binwrite(ARGV[0],Marshal.dump(a))',
    str(game / ('Data/Scripts.'+extension)), args.engine, *map(str, scripts)], check=True)
mapdata = {'width': 20, 'height': 16,
           'walls': [[7, y] for y in range(1, 9)] + [[14, 5], [13, 7], [15, 7], [14, 6], [14, 8]]}
(game / 'map.json').write_text(json.dumps(mapdata))
subprocess.run(['ruby', '-rjson', '-e',
    'File.binwrite(ARGV[1],Marshal.dump(JSON.parse(File.read(ARGV[0]))))',
    str(game / 'map.json'), str(game / 'Data/Map001.rvdata2')], check=True)
(game / 'Game.ini').write_text('[Game]\nTitle=HardRPG Tap Walk Probe '+args.engine.upper()+
    '\nLibrary='+library+'\nScripts=Data/Scripts.'+extension+'\nRTP=\n')
manifest = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in scripts}
(args.output / 'source-hashes.json').write_text(json.dumps(manifest, indent=2)+'\n')
(args.output / 'engine.json').write_text(json.dumps({'engine':args.engine,
    'game_folder':game.name,'script_extension':extension,'library':library},indent=2)+'\n')
archive = args.output / 'HardRPG-Tap-Walk-Probe.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for p in sorted(game.rglob('*')):
        if p.is_file():
            z.write(p, p.relative_to(args.output))
print(archive)

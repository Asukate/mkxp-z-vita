#!/usr/bin/env python3
"""Fast checks for build recipes and the packaged launcher source payload."""
import ast
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent

def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)

for directory in ['scripts', 'ci']:
    for path in (ROOT / directory).glob('*.sh'):
        run('bash', '-n', str(path))
    for path in (ROOT / directory).glob('*.py'):
        ast.parse(path.read_text(), filename=str(path))
for path in (ROOT / 'launcher').rglob('*.rb'):
    run('ruby', '-c', str(path))
run('ruby', '-c', str(ROOT / 'scripts/build-picker.rb'))
for path in (ROOT / 'launcher').rglob('*.json'):
    json.loads(path.read_text())
with tempfile.TemporaryDirectory() as temp:
    stub = Path(temp) / 'stub'
    run('ruby', str(ROOT / 'scripts/build-picker.rb'), str(stub))
    for line in (ROOT / 'launcher/release-payload.sha256').read_text().splitlines():
        sha, name = line.split(maxsplit=1)
        if name.startswith('stub/'):
            path = Path(temp) / name
        elif name.startswith('sce_sys/'):
            path = ROOT / 'launcher/livearea' / Path(name).name
        elif name.startswith('licenses/'):
            if name == 'licenses/GPL-2.0.txt':
                path = ROOT / 'COPYING'
            elif name == 'licenses/libnsgif.txt':
                path = ROOT / 'src/display/libnsgif/COPYING'
            else:
                path = ROOT / 'LICENSES' / Path(name).name
        elif name == 'THIRD-PARTY-NOTICES.md':
            path = ROOT / name
        else:
            path = ROOT / 'launcher' / name
        assert hashlib.sha256(path.read_bytes()).hexdigest() == sha, name
print('Source syntax and launcher manifest verified.')

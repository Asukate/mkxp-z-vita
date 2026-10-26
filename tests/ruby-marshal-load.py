#!/usr/bin/env python3
"""Check that the patched Ruby keeps Marshal.load links in a chunked table.

Upstream keeps them in an st_table that needs two contiguous blocks of tens
of MB for a large RPG Maker map; on the Vita's fragmented heap that fails
(Hylics map 28 entered twice). With --ruby, a host Ruby built from the
patched tree also runs tests/ruby-marshal-load.rb.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ruby', type=Path, help='Host Ruby built from the patched tree')
parser.add_argument('--scratch', type=Path, help='Writable directory for extracted source')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
scratch = args.scratch or root / 'build' / 'tests'
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='ruby-marshal-', dir=scratch) as directory:
    tmp = Path(directory)
    subprocess.run(['tar', '--use-compress-program=zstd', '-xf',
        str(root / 'deps/vendor/ruby-vita-a2d396e-tree.tar.zst'), '-C', str(tmp), 'marshal.c'], check=True)
    subprocess.run(['git', 'init', '-q', str(tmp)], check=True)
    subprocess.run(['git', 'apply', '--include=marshal.c',
        str(root / 'deps/ruby-vita-a2d396e-clean.patch')], cwd=tmp, check=True)
    source = (tmp / 'marshal.c').read_text()
    load = source[source.index('struct load_arg {'):]
    assert 'struct load_data_table data;' in load, 'load_arg.data is not the chunked table'
    assert not re.search(r'st_\w+\(arg->data\b', load), 'Marshal.load still uses an st_table for links'
    assert 'LOAD_DATA_CHUNK_BITS 12' in source
if args.ruby:
    subprocess.run([str(args.ruby.resolve()), str(root / 'tests/ruby-marshal-load.rb')], check=True, timeout=120)
print('Marshal.load chunked link table checks passed')

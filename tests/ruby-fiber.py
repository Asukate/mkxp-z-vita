#!/usr/bin/env python3
"""Test the patched pthread backend's failure recovery and resource lifecycle."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ruby', type=Path, help='Host Ruby built with --with-coroutine=pthread')
parser.add_argument('--scratch', type=Path, help='Writable directory for extracted source and test binaries')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
scratch = args.scratch or root / 'build' / 'tests'
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='ruby-fiber-', dir=scratch) as directory:
    tmp = Path(directory)
    subprocess.run(['tar', '--use-compress-program=zstd', '-xf',
        str(root / 'deps/vendor/ruby-vita-a2d396e-tree.tar.zst'), '-C', str(tmp)], check=True)
    subprocess.run(['git', 'init', '-q', str(tmp)], check=True)
    subprocess.run(['git', 'apply', str(root / 'deps/ruby-vita-a2d396e-clean.patch')], cwd=tmp, check=True)
    backend = tmp / 'coroutine/pthread'
    exe = tmp / 'fiber-context'
    wraps = ['pthread_attr_init', 'pthread_attr_destroy', 'pthread_attr_setstack',
             'pthread_cond_init', 'pthread_create']
    subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread', '-I', str(backend),
        str(root / 'tests/ruby-fiber-context.c'), str(backend / 'Context.c'),
        *['-Wl,--wrap=' + name for name in wraps], '-o', str(exe)], check=True)
    for mode in range(5):
        subprocess.run([str(exe), str(mode)], check=True, timeout=30)
    if args.ruby:
        shim = tmp / 'thread-limit.so'
        subprocess.run(['cc', '-std=gnu11', '-shared', '-fPIC',
            str(root / 'tests/ruby-fiber-thread-limit.c'), '-ldl', '-pthread', '-o', str(shim)], check=True)
        result = subprocess.run([str(args.ruby.resolve()), str(root / 'tests/ruby-fiber-lifecycle.rb')],
            env={**os.environ, 'LD_PRELOAD': str(shim)}, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        print(result.stdout, end='')
        print(result.stderr, end='')
        result.check_returncode()
        assert 'created=2300 joined=2300 live=0 peak=2 failures=0' in result.stderr
print('Ruby pthread failure recovery and 2000-context lifecycle checks passed')

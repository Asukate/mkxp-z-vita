#!/usr/bin/env python3
"""Check presentation timing, stall retention, resets and border placement."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as temp:
    binary = Path(temp) / 'performance-metrics'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++14', '-Wall', '-Wextra',
                    '-Werror', '-I', str(ROOT), str(ROOT/'tests/performance-metrics.cpp'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: 30/60 FPS, five-second stall retention and expiry, clock/reset handling, border placement')

#!/usr/bin/env python3
"""Reject a Vita C++ threading combination that cannot initialize static guards."""
import subprocess
import sys

if len(sys.argv) != 3:
    raise SystemExit('usage: vita-launcher-threading.py ARM_NM ENGINE_ELF')
symbols = subprocess.check_output([sys.argv[1], sys.argv[2]], text=True)
rows = [line.split() for line in symbols.splitlines()]
def defined(name):
    return any(len(row) == 3 and row[2] == name and row[1].upper() != 'U' for row in rows)
if defined('pthread_cancel') and not defined('pthread_once'):
    raise SystemExit('Unsafe Vita C++ static guards: threading enabled by pthread_cancel, but pthread_once is unresolved')
print('Vita C++ static-guard threading linkage passed')

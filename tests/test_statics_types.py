#!/usr/bin/env python3
"""Check reset-slot declarations against the unchanged imported definitions.

Compile declaration pairs, not just pointer sizes: equal size does not make
void* and FTSENT** (or different callback signatures) compatible C types.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
core = (ROOT / 'src/static_reset.c').read_text()
bridge = ROOT / 'commands/ls_static_slot.c'
owned = core + (bridge.read_text() if bridge.exists() else '')
ls = (ROOT / 'upstream/netbsd/bin/ls/ls.c').read_text()
printing = (ROOT / 'upstream/netbsd/bin/ls/print.c').read_text()
preamble = '#include <sys/types.h>\n#include <fts.h>\n#include "ls.h"\n'
command = (shlex.split(os.environ.get('CC', 'cc')) +
           shlex.split(os.environ.get('CPPFLAGS', '')) +
           shlex.split(os.environ.get('CFLAGS', '')) +
           ['-std=c99', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
            '-Iinclude', '-Icompat/netbsd/include', '-Ilibc/include',
            '-Iupstream/netbsd/bin/ls', '-fsyntax-only', '-x', 'c', '-'])
failed = False
for original, renamed, source in [
        ('array', 'cb_ls_printcol_array', printing),
        ('sortfcn', 'cb_ls_sortfcn', ls),
        ('printfcn', 'cb_ls_printfcn', ls)]:
    actual = re.findall(r'^\s*static [^;\n]*\b' + original + r'\b[^;\n]*;',
                        source, re.M)
    declared = re.findall(r'^extern [^;]*\b' + renamed + r'\b[^;]*;', owned, re.M)
    if len(actual) != 1 or len(declared) != 1:
        sys.exit(f'{renamed}: expected one definition and one declaration')
    expected = re.sub(r'\bstatic\b', 'extern', actual[0])
    expected = re.sub(r'\b' + original + r'\b', renamed, expected)
    result = subprocess.run(command, input=preamble + expected + '\n' +
                            declared[0] + '\n', cwd=ROOT, text=True,
                            capture_output=True)
    if result.returncode:
        failed = True
        print(result.stderr, file=sys.stderr, end='')
    else:
        print(f'{renamed}: compatible with pinned source')
sys.exit(1 if failed else 0)

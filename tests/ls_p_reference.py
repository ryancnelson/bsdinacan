#!/usr/bin/env python3
"""Bounded pinned-FTS initial-buffer experiment, NOT native NetBSD ls acceptance.

Pass a directory containing fts.c and fts.h downloaded at the repository pin.
The probe verifies hashes, builds untouched fts.c using small host adapters,
and runs the exact printpath function extracted from pinned print.c. It tests
whether a supplied allocation prefix survives initial root preview. No network
access or imported source changes occur. This is an optional investigation tool.
"""
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
reference = Path(sys.argv[1])
hashes = {
    'fts.c': '24157b290edfbbdb9fa63e5c05de7632b7427fae1eb465deba4d6af1b21be55c',
    'fts.h': '8d2d89cefd95f03004a09f77d3b3c0c11dffd1b804379a3010972cf9edb8fa62',
}
for name, expected in hashes.items():
    if hashlib.sha256((reference / name).read_bytes()).hexdigest() != expected:
        sys.exit('pinned reference hash mismatch: ' + name)
source = (ROOT / 'upstream/netbsd/bin/ls/print.c').read_text()
start = source.index('static int\nprintpath(')
end = source.index('\nvoid\n', start)
with tempfile.TemporaryDirectory(prefix='ls-p-reference-') as scratch:
    work = Path(scratch)
    for name in hashes:
        shutil.copyfile(reference / name, work / name)
    (work / 'sys').mkdir()
    (work / 'sys/cdefs.h').write_text(
        '#define __BEGIN_DECLS\n#define __END_DECLS\n#define __RENAME(x)\n')
    (work / 'namespace.h').write_text('/* standalone probe: no libc hiding */\n')
    (work / 'nbtool_config.h').write_text('''#include <sys/types.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include <stdlib.h>
#define _DIAGASSERT assert
#ifndef __RENAME
#define __RENAME(x)
#endif
int reallocarr(void *, size_t, size_t);
''')
    (work / 'probe.c').write_text('''#include "nbtool_config.h"
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include "fts.h"
static int f_fullpath = 1;
''' + source[start:end] + '''
int main(int argc, char **argv) {
    FTS *tree;
    FTSENT *p;
    if (argc < 2) return 2;
    tree = fts_open(argv + 1, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
    if (tree == NULL) { perror("fts_open"); return 3; }
    for (p = fts_children(tree, 0); p != NULL; p = p->fts_link) {
        if (p->fts_path != tree->fts_path) return 4;
        if (strcmp(p->fts_path, getenv("REFERENCE_PREFIX")) != 0) return 5;
        printpath(p); putchar('\\n');
    }
    return fts_close(tree) == 0 ? 0 : 6;
}
''')
    (work / 'alloc.c').write_text('''#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
void *reference_realloc(void *p, size_t size) {
    void *result = realloc(p, size);
    if (result != NULL && p == NULL) {
        const char *prefix = getenv("REFERENCE_PREFIX");
        if (prefix == NULL || strlen(prefix) >= size) abort();
        memset(result, 0, size);
        memcpy(result, prefix, strlen(prefix));
    }
    return result;
}
int reallocarr(void *ptr, size_t n, size_t size) {
    void *old, *replacement;
    if (size && n > SIZE_MAX / size) return ENOMEM;
    memcpy(&old, ptr, sizeof(old));
    replacement = realloc(old, n * size);
    if (replacement == NULL) return ENOMEM;
    memcpy(ptr, &replacement, sizeof(replacement));
    return 0;
}
''')
    cc = shlex.split(os.environ.get('CC', 'cc'))
    subprocess.run(cc + ['-std=c99', '-D_DEFAULT_SOURCE', '-DHAVE_NBTOOL_CONFIG_H=1',
                        '-Drealloc=reference_realloc', '-I.', '-c', 'fts.c',
                        '-o', 'fts.o'], cwd=work, check=True)
    subprocess.run(cc + ['-std=c99', '-D_DEFAULT_SOURCE', '-I.', 'probe.c',
                        'alloc.c', 'fts.o', '-o', 'probe'], cwd=work, check=True)
    (work / 'file').write_text('file\n')
    (work / 'dir').mkdir()
    operands = ['file', str(work / 'file'), 'dir', str(work / 'dir')]
    for prefix in ('ALLOC-A', 'ALLOC-B'):
        result = subprocess.run([str(work / 'probe')] + operands, cwd=work,
                                env=dict(os.environ, REFERENCE_PREFIX=prefix),
                                capture_output=True, timeout=30)
        expected = ''.join(prefix + '/' + name + '\n' for name in operands)
        if (result.returncode, result.stdout.decode(), result.stderr) != (0, expected, b''):
            sys.exit('reference experiment failed: ' + repr(result))
        print('status=0 prefix=' + prefix)
        print(result.stdout.decode(), end='')

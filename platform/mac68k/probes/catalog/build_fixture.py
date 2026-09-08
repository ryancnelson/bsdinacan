#!/usr/bin/env python3
"""Build twice, require identical real HFS bytes, and emit the guest expectations."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=False)
source = Path(__file__).resolve().parent
builder = out / 'fixture-builder'
subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                '-I/Retro68-build/toolchain/include', str(source / 'fixture_builder.c'),
                '/Retro68-build/toolchain/lib/libhfs.a', '-o', str(builder)], check=True)
env = dict(os.environ, TZ='UTC')
first = subprocess.check_output([str(builder), str(out / 'CatalogFixture.dsk')], env=env)
second = subprocess.check_output([str(builder), str(out / 'repeat.dsk')], env=env)
a = (out / 'CatalogFixture.dsk').read_bytes()
b = (out / 'repeat.dsk').read_bytes()
assert a == b and first == second, 'HFS fixture not deterministic'
entries = []
for line in first.decode().splitlines():
    name, cnid, parent, directory, data, resource, flags, kind, creator = line.split('\t')
    entries.append(dict(name=name, id=int(cnid), parent_id=int(parent), directory=int(directory),
                        data_length=int(data), resource_length=int(resource), finder_flags=int(flags),
                        file_type=kind, creator=creator))
for e in entries:
    if e['name'] in ('Desktop DB', 'Desktop DF'):
        assert e['finder_flags'] & 0x4000
        assert e['creator'] == 'DMGR' and e['file_type'] == ('BTFL' if e['name'] == 'Desktop DB' else 'DTFL')
desktop_size = next(e['data_length'] for e in entries if e['name'] == 'Desktop DB')
assert desktop_size >= 512 and desktop_size % 512 == 0
expected = {'Empty': (1, 0, 0), 'Subdir': (1, 0, 0),
            'Subdir:Sentinel': (0, 7, 0), 'Zero': (0, 0, 0),
            'Eight': (0, 8, 0), 'Forked': (0, 10, 17),
            '1234567890123456789012345678901': (0, 5, 0),
            'Desktop DB': (0, desktop_size, 0), 'Desktop DF': (0, 0, 0)}
assert {e['name']: (e['directory'], e['data_length'], e['resource_length'])
        for e in entries} == expected, 'fixture catalog does not match required cases'
assert len({e['id'] for e in entries}) == 9 and all(e['id'] > 0 for e in entries)
# Recorded after two independent builds in the pinned amd64 Retro68 image.
assert hashlib.sha256(a).hexdigest() == '5adc9b2bb9cb9d1d7119ad45dc2d8470e221aee776a6b653d9e788e445e56f58'
manifest = dict(volume='CatalogFixture', root_id=2, sha256=hashlib.sha256(a).hexdigest(), entries=entries)
(out / 'fixture.json').write_text(json.dumps(manifest, indent=2) + '\n')
(out / 'FIXTURE.SHA256').write_text(manifest['sha256'] + '  CatalogFixture.dsk\n')
with (out / 'fixture_generated.h').open('w') as f:
    f.write('/* Generated from independently statted real HFS fixture. */\n')
    f.write('#define FIXTURE_ROOT_COUNT 8\n')
    f.write('static const struct cb_catalog_entry fixture_entries[] = {\n')
    for e in entries:
        f.write('    {%s, %d, %d, %d, %d, %d},\n' % (json.dumps(e['name'].split(':')[-1]),
                e['id'], e['parent_id'], e['data_length'], e['resource_length'], e['directory']))
    f.write('};\n')
(out / 'CatalogFixture.dsk').chmod(0o444)
(out / 'repeat.dsk').unlink()
builder.unlink()
print('PASS: two real HFS fixture builds identical, sha256=' + manifest['sha256'])

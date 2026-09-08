#!/usr/bin/env python3
"""Record the pinned MoreFiles compile blocker; success means diagnosis, not viability."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
TOOLCHAIN = Path('/Retro68-build/toolchain')


def run(command, log):
    completed = subprocess.run(command, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=120)
    log.write_bytes(completed.stdout)
    return completed.returncode, completed.stdout.decode('utf-8', errors='replace')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path,
                        help='new directory; existing evidence is never overwritten')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = ROOT / 'upstream/morefiles'
    pins = json.loads((source / 'pins.json').read_text())
    for entry in pins['files']:
        actual = hashlib.sha256((source / entry['path']).read_bytes()).hexdigest()
        if actual != entry['sha256']:
            raise RuntimeError('upstream checksum mismatch: ' + entry['path'])
    (out / 'pins.json').write_text(json.dumps(pins, indent=2) + '\n')
    cc = str(TOOLCHAIN / 'bin/m68k-apple-macos-gcc')
    flags = ['-Os', '-ffunction-sections', '-fdata-sections', '-Wall', '-Wextra']
    version, _ = run([cc, '--version'], out / 'compiler.txt')
    if version:
        raise RuntimeError('compiler unavailable')
    # A genuine SDK/compiler control distinguishes setup failure from MoreFiles failure.
    control = out / 'sdk-control.c'
    control.write_text('#include <Types.h>\n#include <Files.h>\n'
                       'OSErr catalog_control(CInfoPBPtr pb) { return PBGetCatInfoSync(pb); }\n')
    status, _ = run([cc, *flags, '-c', str(control), '-o', str(out / 'sdk-control.o')],
                    out / 'sdk-control.log')
    if status or not (out / 'sdk-control.o').is_file():
        raise RuntimeError('SDK control did not compile; cannot classify MoreFiles')
    results = {}
    texts = {}
    for filename in ['IterateDirectory.c', 'MoreFilesExtras.c']:
        command = [cc, *flags, '-c', str(source / filename), '-o', str(out / (filename + '.o'))]
        status, text = run(command, out / (filename + '.log'))
        results[filename] = {'command': command, 'exit_status': status}
        texts[filename] = text
    # This is intentionally an expected-blocker experiment, never an app-build pass.
    # A changed outcome requires human review rather than silently crediting viability.
    known = (all(item['exit_status'] == 1 for item in results.values())
             and 'XVolumeParam' in texts['IterateDirectory.c']
             and 'unknown type name' in texts['IterateDirectory.c']
             and 'Finder.h: No such file or directory' in texts['MoreFilesExtras.c']
             and not any(out.glob('*.c.o')))
    summary = {'experiment': 'REUSE-02', 'viable': False,
               'classification': 'known SDK compile blocker' if known else 'outcome changed; review required',
               'sdk_control': 'compiled', 'compilation': results,
               'link': 'not attempted: source compilation failed',
               'guest': 'not run: no probe application',
               'fixture': 'not built: compile prerequisite unmet'}
    (out / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
    print('MORE_FILES_NOT_VIABLE: ' + summary['classification'])
    return 0 if known else 1


if __name__ == '__main__':
    sys.exit(main())

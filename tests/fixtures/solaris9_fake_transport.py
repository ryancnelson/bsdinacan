#!/usr/bin/env python3
"""Offline stand-in: executes remote filesystem commands only in test tempdirs.
Guest console/monitor, time and ISO creation are simulated. Never opens sockets.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time

root = Path(os.environ['SQ_TEST_ROOT'])
scenario = os.environ.get('SQ_TEST_SCENARIO', 'success')
state_path = root / 'state.json'
state = json.loads(state_path.read_text()) if state_path.exists() else {'events': [], 'mounted': True}
mode = Path(sys.argv[0]).name


def save():
    pending = state_path.with_suffix('.tmp')
    pending.write_text(json.dumps(state))
    pending.replace(state_path)


if mode == 'ssh':
    # All network transport has been replaced: run only a local shell against
    # the temporary rig stand-in, including real mkdir/cmp/tar/cat operations.
    if scenario == 'release_failure' and 'rmdir ' in sys.argv[-1]:
        sys.exit(1)
    sys.exit(subprocess.run(['bash', '-c', sys.argv[-1]]).returncode)
if mode == 'timeout':
    os.execvp(sys.argv[2], sys.argv[2:])
if mode == 'date':
    if sys.argv[1:] == ['+%s']:
        state['clock_reads'] = state.get('clock_reads', 0) + 1
        save()
        print(1901 if scenario == 'timeout' and state['clock_reads'] >= 3 else 100)
    else:
        print('20260912T000000Z')
    sys.exit(0)
if mode == 'sha256sum':
    for name in sys.argv[1:]:
        try:
            print(hashlib.sha256(Path(name).read_bytes()).hexdigest() + '  ' + name)
        except OSError as exc:
            print(exc, file=sys.stderr)
            sys.exit(1)
    sys.exit(0)
if mode == 'mkisofs':
    target = Path(sys.argv[sys.argv.index('-o') + 1])
    target.write_bytes(b'OFFLINE ISO STAND-IN\n')
    sys.exit(7 if scenario == 'mkisofs_failure' else 0)
if mode == 'mon.py':
    command = sys.argv[1]
    state['events'].append('monitor:' + command.split()[0])
    if command.startswith('eject '):
        state['iso'] = None
    elif command.startswith('change '):
        iso = json.loads(command.split('json:', 1)[1])['file']['filename']
        if not Path(iso).is_file():
            print('monitor rejected nonexistent ISO', file=sys.stderr)
            sys.exit(1)
        state['iso'] = iso
    elif command == 'info block':
        if state.get('iso'):
            if scenario == 'wrong_drive':
                print('drive7: [not inserted]')
                print('drive8 (#block1): ' + state['iso'] + ' (raw, read-only)')
            else:
                print('drive7 (#block1): ' + state['iso'] + ' (raw, read-only)')
        else:
            print('drive7: [not inserted]')
            print('drive8: /other.iso (raw, read-only)')
    save()
    sys.exit(0)
if mode != 'console.py':
    raise SystemExit('unexpected offline tool ' + mode)
script = sys.argv[1]
match = re.search(r'SQ_NONCE=([A-Za-z0-9-]+); SQ_PHASE=([a-z]+);', script)
if not match:
    raise SystemExit('unframed console request')
nonce, phase = match.groups()
words = shlex.split(script)
command = words[words.index('-c') + 1].removesuffix(';')
state['events'].append('guest:' + phase)
status = 0
if phase == 'unmount':
    status = 1 if scenario == 'busy_unmount' else 0
    state['mounted'] = bool(status)
    body = 'SQ_UNMOUNTED' if not status else 'busy'
elif phase == 'extract':
    state['mounted'] = True
    status = 7 if scenario == 'cpio_failure' else 0
    body = 'SQ_EXTRACTED' if not status else 'cpio error'
elif phase == 'toolchain':
    body = 'gcc (GCC) 3.4.6\nGNU Make 3.81\nSunOS fixture'
elif phase == 'start':
    state['started'] = True
    save()
    if scenario == 'start_disconnect':
        sys.exit(255)
    body = 'SQ_PID=12345'
elif phase == 'poll':
    save()
    if scenario == 'poll_disconnect':
        sys.exit(255)
    if scenario == 'interrupt':
        time.sleep(0.5)
    token = re.search(r"SQ_RUN_TOKEN='?([A-Za-z0-9-]+)", command).group(1)
    if scenario == 'wrong_run':
        token = 'stale-run'
    body = 'SQ_DONE:' + token + (':1' if scenario == 'nonzero_exit' else ':0')
    if scenario == 'numeric_noise':
        body = '0'
elif phase == 'transcript':
    body = 'all core tests passed\nlauncher test passed\n'
    body += 'marker absent' if scenario == 'missing_pass' else 'SOLARIS9_CANNEDBSD_TEST=PASS'
else:
    raise SystemExit('unexpected guest phase ' + phase)
save()
# Include real console hazards: command echo, CRLF, prompt and a foreign helper
# completion marker. Only this call's framed response may authorize progress.
print('# ' + script.replace('\n', '\r\n') + '\r')
print('SQ_BEGIN_' + nonce + '\r')
print(body.replace('\n', '\r\n') + '\r')
if scenario != 'truncated_frame' or phase != 'poll':
    print('SQ_END_' + (nonce if scenario != 'wrong_nonce' or phase != 'poll' else 'old') + ':' + str(status) + '\r')
print('CB_DONE_99\r\n#')

#!/usr/bin/env python3
"""Report -P operand behavior; duplicated root output is not an acceptance oracle.

Run with the candidate executable. Existing directory-child expectations are
checked; other outputs are recorded for reference investigation, not frozen as
correct NetBSD behavior. This optional diagnostic is not part of make test.
"""
import json
from pathlib import Path
import subprocess
import sys

program = Path(sys.argv[1]).resolve()
setup = ('mkdir /tmp/p; mkdir /tmp/p/dir; '
         'echo file > /tmp/p/file; echo child > /tmp/p/dir/child; '
         'cd /tmp/p; ')
cases = [
    ('ls -1P file', None),
    ('ls -1P /tmp/p/file', None),
    ('ls -1Pd dir', None),
    ('ls -1Pd /tmp/p/dir', None),
    ('ls -1Pd file dir /tmp/p/file /tmp/p/dir', None),
    ('ls -1P file /tmp/p/dir', None),
    ('ls -1P dir', b'dir/child\n'),
    ('ls -1P /tmp/p/dir', b'/tmp/p/dir/child\n'),
]
# Verify the fixture independently so setup failure cannot look like ls output.
fixture = subprocess.run([str(program), '-c', setup + 'cat file dir/child'],
                         capture_output=True, timeout=30)
if (fixture.returncode, fixture.stdout, fixture.stderr) != (0, b'file\nchild\n', b''):
    sys.exit('fixture setup failed: ' + repr(fixture))
for command, accepted in cases:
    result = subprocess.run([str(program), '-c', setup + command],
                            capture_output=True, timeout=30)
    print(json.dumps({'command': command, 'status': result.returncode,
                      'stdout': result.stdout.decode(),
                      'stderr': result.stderr.decode()}))
    if result.returncode != 0 or result.stderr:
        sys.exit('fixture failed: ' + command)
    if accepted is not None and result.stdout != accepted:
        sys.exit('directory-child acceptance changed: ' + command)

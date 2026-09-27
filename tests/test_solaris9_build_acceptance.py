#!/usr/bin/env python3
"""Run the native wrapper's actual acceptance tail offline, without a guest.

Compilation and runtime behavior are outside this test. The shell assertions
and final PASS come verbatim from the checked-in wrapper. Only command outputs,
file(1), and ksh's print builtin are substituted; no predicate is duplicated.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
START = "output=$(./build/bsdinacan -c 'echo hello | tr a-z A-Z > /tmp/result; cat /tmp/result')"
PASS = 'SOLARIS9_CANNEDBSD_TEST=PASS'


class SolarisAcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='solaris-wc-gate-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'build').mkdir()
        binary = self.root / 'build/bsdinacan'
        binary.write_text('''#!/bin/sh
[ "$#" = 2 ] && [ "$1" = -c ] || exit 90
case "$2" in
 'echo hello | tr a-z A-Z > /tmp/result; cat /tmp/result') printf '%s\\n' "$HELLO_OUTPUT" ;;
 'false; echo $?') printf '%s\\n' "$STATUS_OUTPUT" ;;
 'echo -n hello | wc -c') printf '%s\\n' "$WC_OUTPUT"; exit "$WC_STATUS" ;;
 *) exit 91 ;;
esac
''')
        binary.chmod(0o755)
        source = (ROOT / 'tools/solaris9-build.sh').read_text()
        self.assertEqual(source.count(START), 1, 'acceptance tail moved or duplicated')
        self.tail = source[source.index(START):]
        self.assertIn(PASS, self.tail)
        self.env = dict(os.environ, HELLO_OUTPUT='HELLO', STATUS_OUTPUT='1',
                        WC_OUTPUT='       5', WC_STATUS='0')

    def run_tail(self, **overrides):
        env = dict(self.env, **overrides)
        # The actual tail uses POSIX shell syntax except ksh print. Its shim
        # prints arguments only and cannot change the acceptance comparisons.
        return subprocess.run(['sh', '-c', '''set -eu
print() { [ "$1" = -r ] && [ "$2" = -- ] || exit 92; shift 2; printf '%s\\n' "$*"; }
file() { [ "$1" = build/bsdinacan ] || exit 93; printf 'file reached\\n'; }
''' + self.tail], cwd=self.root, env=env, text=True,
                              capture_output=True, timeout=5)

    def test_current_padded_output_passes(self):
        result = self.run_tail()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.splitlines(), ['file reached', PASS])

    def test_incorrect_wc_output_rejected(self):
        for output in ['', '5', '      5', '        5', '\t5', '       4',
                       '       5 extra', '       5\nextra']:
            with self.subTest(output=repr(output)):
                result = self.run_tail(WC_OUTPUT=output)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('libc acceptance output:', result.stdout)
                self.assertNotIn(PASS, result.stdout)
                self.assertNotIn('file reached', result.stdout)

    def test_correct_text_with_failed_command_rejected(self):
        result = self.run_tail(WC_STATUS='7')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        self.assertNotIn(PASS, result.stdout)
        self.assertNotIn('file reached', result.stdout)

    def test_preceding_acceptance_checks_remain_required(self):
        for override in [dict(HELLO_OUTPUT='wrong'), dict(STATUS_OUTPUT='0')]:
            with self.subTest(override=override):
                result = self.run_tail(**override)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn(PASS, result.stdout)
                self.assertNotIn('file reached', result.stdout)


if __name__ == '__main__':
    unittest.main()

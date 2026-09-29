#!/usr/bin/env python3
"""Exercise the real statics shell gate with controlled executable results.

These controls test the gate, not cannedBSD's static-state implementation.
The actual binary still runs the five cases under make test and sanitizer builds.
"""
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GATE = ROOT / "tests/test_statics_repro.sh"
# Independent literal fixtures for the five command sessions. Match a distinct
# operand so an omitted, repeated or unexpectedly changed session is rejected.
FIXTURES = [
    ["/tmp/cat1", "hello\ncat1_status=0\nworld\ncat2_status=0\n", ""],
    ["/tmp/a1", "a1 a2 a3\nls1_status=0\nrm_status=0\na1 a2\nls2_status=0\n", ""],
    ["/tmp/nonexistent", "rm1_status=1\nrm2_status=0\n",
     "rm: /tmp/nonexistent: no such file or directory\n"],
    ["/tmp/cpa", "/tmp/cpa -> /tmp/cpa_out\ncp1_status=0\ncp2_status=0\n1\n2\n", ""],
    ["/tmp/mva", "mv1_status=0\nmv2_status=0\ntest\n", ""],
]
MOCK = r'''
import json, os, sys
from pathlib import Path
fixtures = json.loads(os.environ["GATE_FIXTURES"])
assert len(sys.argv) == 3 and sys.argv[1] == "-c"
matches = [i for i, row in enumerate(fixtures) if row[0] in sys.argv[2]]
assert len(matches) == 1, sys.argv
index = matches[0]
with open(os.environ["GATE_CALLS"], "a") as calls:
    calls.write(str(index) + "\n")
_, out, err = fixtures[index]
status = 0
if index == int(os.environ.get("GATE_INDEX", "-1")):
    mode = os.environ["GATE_MODE"]
    if mode == "status": status = 7
    elif mode == "empty": out = ""
    elif mode == "newline": out = out[:-1]
    elif mode == "nul": out += "\x00hidden"
    elif mode == "stderr": err += "unexpected diagnostic\n"
    elif mode == "inner_status": out = out.replace("status=0", "status=1", 1)
    elif mode in ("asan_warning", "asan_error"):
        prefix = next(x[9:] for x in os.environ["ASAN_OPTIONS"].split(":")
                      if x.startswith("log_path="))
        text = ("==123==WARNING: ASan is ignoring requested __asan_handle_no_return: "
                "stack type: default top: 0x12; bottom 0x10; size: 0x2 (2)\n"
                "False positive error reports may follow\n"
                "For details see https://github.com/google/sanitizers/issues/189\n")
        if mode == "asan_error": text += "AddressSanitizer: heap-use-after-free\n"
        Path(prefix + ".123").write_text(text)
    else: raise AssertionError(mode)
sys.stdout.write(out)
sys.stderr.write(err)
sys.exit(status)
'''


class StaticsGateTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="statics-gate-test-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.program = self.root / "program"
        self.program.write_text("#!" + sys.executable + "\n" + MOCK)
        self.program.chmod(0o755)
        self.calls = self.root / "calls"
        self.tmp = self.root / "tmp"
        self.tmp.mkdir()

    def run_gate(self, program=None, **extra):
        env = os.environ.copy()
        env.update(PROGRAM_PATH=str(program or self.program),
                   GATE_FIXTURES=json.dumps(FIXTURES), GATE_CALLS=str(self.calls),
                   TMPDIR=str(self.tmp), **extra)
        self.calls.write_text("")
        result = subprocess.run(["sh", str(GATE)], cwd=ROOT, env=env,
                                capture_output=True, timeout=10)
        self.assertEqual(list(self.tmp.iterdir()), [], "gate leaked its scratch directory")
        return result

    def test_nonzero_without_diagnostic_is_rejected(self):
        program = shutil.which("false")
        self.assertIsNotNone(program)
        result = self.run_gate(program)
        self.assertNotEqual(result.returncode, 0, result.stdout.decode())

    def test_success_without_fixture_output_is_rejected(self):
        program = shutil.which("true")
        self.assertIsNotNone(program)
        result = self.run_gate(program)
        self.assertNotEqual(result.returncode, 0, result.stdout.decode())

    def test_complete_fixtures_are_accepted_once_each(self):
        result = self.run_gate()
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(self.calls.read_text(), "0\n1\n2\n3\n4\n")
        self.assertIn(b"5 cases passed, 0 cases failed", result.stdout)
        self.assertIn(b"mv fastcopy remains unexercised", result.stdout)

    def test_each_fixture_requires_status_and_exact_streams(self):
        for index in range(5):
            for mode in ("status", "empty", "newline", "nul", "stderr", "inner_status"):
                with self.subTest(index=index, mode=mode):
                    result = self.run_gate(GATE_INDEX=str(index), GATE_MODE=mode)
                    self.assertNotEqual(result.returncode, 0, result.stdout.decode())
                    self.assertIn(b"4 cases passed, 1 cases failed", result.stdout)

    def test_only_exact_known_context_warning_is_accepted(self):
        result = self.run_gate(GATE_INDEX="0", GATE_MODE="asan_warning")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        result = self.run_gate(GATE_INDEX="0", GATE_MODE="asan_error")
        self.assertNotEqual(result.returncode, 0, result.stdout.decode())


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Host-only acceptance protocol tests; no emulator or real artifact needed."""
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import sys
import tarfile
import unittest

sys.dont_write_bytecode = True
SCRIPT = Path(__file__).resolve().parents[1] / 'platform/mac68k/guest.py'

class GuestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.artifact = self.root / 'artifact'
        self.artifact.mkdir()
        with tarfile.open(self.artifact / 'CannedBSD.tar.gz', 'w:gz') as archive:
            entry = tarfile.TarInfo('CannedBSD.dsk')
            entry.size = 4
            archive.addfile(entry, io.BytesIO(b'HFS!'))
        self.sha = hashlib.sha256((self.artifact / 'CannedBSD.tar.gz').read_bytes()).hexdigest()
        (self.artifact / 'SHA256SUMS').write_text(self.sha + '  CannedBSD.tar.gz\n')
        (self.artifact / 'commit.txt').write_text('a' * 40 + '\n')
        self.state = self.root / 'state'

    def stage(self):
        return guest.stage(self.artifact, self.state, 'a' * 40, None)

    def result(self, run, conclusion='ALL PASS'):
        text = 'cannedBSD System 7 / Retro68\nPASS contexts\n'
        text += 'PASS shell\n' * 7 + conclusion + '\n'
        (run / 'shared/cannedbsd-result.txt').write_text(text)

    def test_bad_checksum_rejected_without_claiming_slot(self):
        (self.artifact / 'CannedBSD.tar.gz').write_bytes(b'corrupt')
        with self.assertRaisesRegex(guest.Rejection, 'checksum'):
            self.stage()
        self.assertFalse((self.state / 'slot').exists())

    def test_wrong_commit_rejected(self):
        with self.assertRaisesRegex(guest.Rejection, 'commit'):
            guest.stage(self.artifact, self.state, 'b' * 40, None)

    def test_stale_result_rejected(self):
        run = self.stage()
        self.result(run)
        staged = json.loads((run / 'manifest.json').read_text())['staged_ns']
        os.utime(run / 'shared/cannedbsd-result.txt', ns=(staged - 1, staged - 1))
        with self.assertRaisesRegex(guest.Rejection, 'stale'):
            guest.check(self.state)
        self.assertFalse((run / 'acceptance.json').exists())

    def test_missing_and_failed_results_rejected(self):
        run = self.stage()
        with self.assertRaisesRegex(guest.Rejection, 'missing'):
            guest.check(self.state)
        self.result(run, conclusion='FAILED')
        with self.assertRaisesRegex(guest.Rejection, 'ALL PASS'):
            guest.check(self.state)

    def test_acceptance_binds_commit_archive_and_result(self):
        run = self.stage()
        self.result(run)
        receipt = guest.check(self.state)
        self.assertEqual(receipt['commit'], 'a' * 40)
        self.assertEqual(receipt['artifact_sha256'], self.sha)
        self.assertEqual(receipt['result_sha256'], hashlib.sha256((run / 'shared/cannedbsd-result.txt').read_bytes()).hexdigest())
        self.assertTrue((self.state / 'slot').exists(), 'check must not free a running guest slot')

    def test_slot_serializes_and_each_run_has_new_disk_and_shared_dir(self):
        run = self.stage()
        with self.assertRaisesRegex(guest.Rejection, 'slot'):
            self.stage()
        guest.release(self.state, lambda paths: False)
        second = self.stage()
        self.assertNotEqual(run, second)
        self.assertFalse((second / 'shared/cannedbsd-result.txt').exists())
        self.assertEqual((run / 'CannedBSD.dsk').read_bytes(), b'HFS!')
        with self.assertRaisesRegex(guest.Rejection, 'open'):
            guest.release(self.state, lambda paths: True)
        self.assertTrue((self.state / 'slot').exists())

    def test_boot_seed_is_copied_and_preserved(self):
        seed = self.root / 'seed.dsk'
        seed.write_bytes(b'boot')
        run = guest.stage(self.artifact, self.state, 'a' * 40, seed)
        (run / 'System.dsk').write_bytes(b'changed')
        self.assertEqual(seed.read_bytes(), b'boot')

    def test_duplicate_disk_member_rejected(self):
        with tarfile.open(self.artifact / 'CannedBSD.tar.gz', 'w:gz') as archive:
            for _ in range(2):
                entry = tarfile.TarInfo('CannedBSD.dsk')
                entry.size = 1
                archive.addfile(entry, io.BytesIO(b'x'))
        digest = hashlib.sha256((self.artifact / 'CannedBSD.tar.gz').read_bytes()).hexdigest()
        (self.artifact / 'SHA256SUMS').write_text(digest + '  CannedBSD.tar.gz\n')
        with self.assertRaisesRegex(guest.Rejection, 'one regular'):
            self.stage()

if __name__ == '__main__':
    if not SCRIPT.exists():
        raise SystemExit('FAIL: missing exact-artifact guest runner')
    spec = importlib.util.spec_from_file_location('guest', SCRIPT)
    guest = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(guest)
    unittest.main()

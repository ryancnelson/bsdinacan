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
        text = guest.expected_result().replace('ALL PASS', conclusion)
        (run / 'shared/cannedbsd-result.txt').write_text(text)

    def test_old_eight_pass_transcript_cannot_satisfy_expanded_suite(self):
        run = self.stage()
        old = 'cannedBSD System 7 / Retro68\nPASS contexts\n' + 'PASS shell\n' * 7 + 'ALL PASS\n'
        (run / 'shared/cannedbsd-result.txt').write_text(old)
        with self.assertRaisesRegex(guest.Rejection, 'ALL PASS'):
            guest.check(self.state)
        self.assertFalse((run / 'acceptance.json').exists())

    def test_duplicate_pass_does_not_replace_missing_probe(self):
        run = self.stage()
        text = guest.expected_result().replace('PASS libctruncateprobe', 'PASS libcmemoryprobe')
        (run / 'shared/cannedbsd-result.txt').write_text(text)
        with self.assertRaisesRegex(guest.Rejection, 'ALL PASS'):
            guest.check(self.state)
        self.assertFalse((run / 'acceptance.json').exists())

    def test_stage_records_complete_driver_transcript(self):
        run = self.stage()
        expected = (run / 'expected-result.txt').read_text()
        self.assertEqual(expected, guest.expected_result())
        self.assertEqual(sum(line.startswith('PASS ') for line in expected.splitlines()), 56)
        self.assertIn('PASS normalpollprobe\n', expected)
        for probe in ['libcmemoryprobe', 'libcgetoptprobe E 1 0 -z', 'libctruncateprobe',
                      'libcerrprobe', 'export GUEST=mac; printenv GUEST',
                      'printenv CANNEDBSD_UNSET_GUEST',
                      'export EMPTY=; printenv EMPTY', 'printenv FOO=bar',
                      'libcdirnameprobe', 'libcdirentprobe', 'libcprognameprobe', 'vfsexecprobe', 'dirname /tmp/example', 'dirname ////',
                      'dirname -- -leading/dash', 'dirname -x',
                      'dirname /a/b/c | cat', "dirname ''",
                      'libcstdiostateprobe putchar_ok', 'stdiooldtable', 'stdinprobe', 'stdincompat', 'argvprobe', 'getoptargs attached', 'getoptargs missing', 'getoptargs lifecycle', 'libcbasenameprobe', 'basename /tmp/example.txt .txt',
                      'basename foo foo', 'basename -- -foo', 'basename -x',
                      'basename /a/b/c | cat', "basename ''"]:
            self.assertIn('PASS ' + probe + '\n', expected)

    def test_autorun_stage_precreates_evidence_before_timestamp(self):
        run = guest.stage(self.artifact, self.state, 'a' * 40, None, autorun=True)
        manifest = json.loads((run / 'manifest.json').read_text())
        self.assertTrue(manifest['autorun'])
        for name in ['autorun.txt', 'result.txt', 'screen.pict', 'done.txt']:
            path = run / ('shared/cannedbsd-' + name)
            self.assertTrue(path.is_file())
            self.assertLessEqual(path.stat().st_mtime_ns, manifest['staged_ns'])
        for name in ['result.txt', 'screen.pict', 'done.txt']:
            self.assertEqual((run / ('shared/cannedbsd-' + name)).read_bytes(), b'')

    def autorun_evidence(self):
        run = guest.stage(self.artifact, self.state, 'a' * 40, None, autorun=True)
        self.result(run)
        (run / 'shared/cannedbsd-done.txt').write_bytes(b'PASS\n')
        (run / 'shared/cannedbsd-screen.pict').write_bytes(b'protocol fixture, decoder injected')
        return run

    @staticmethod
    def decoder(picture, output):
        # File protocol test only; actual PICT decoding is a separate Mac gate.
        return {'screenshot_png_sha256': 'decoded', 'screenshot_width': 630,
                'screenshot_height': 384}

    def test_autorun_inspection_and_app_closure_precede_receipt(self):
        run = self.autorun_evidence()
        receipt = guest.inspect(self.state, self.decoder)
        self.assertEqual(receipt['done_sha256'], hashlib.sha256(b'PASS\n').hexdigest())
        self.assertFalse((run / 'acceptance.json').exists())
        with self.assertRaisesRegex(guest.Rejection, 'closure'):
            guest.check(self.state, picture_validator=self.decoder)
        self.assertFalse((run / 'acceptance.json').exists())
        with self.assertRaisesRegex(guest.Rejection, 'still open'):
            guest.check(self.state, app_closed=True, picture_validator=self.decoder, is_open=lambda paths: True)
        self.assertFalse((run / 'acceptance.json').exists())
        receipt = guest.check(self.state, app_closed=True, picture_validator=self.decoder, is_open=lambda paths: False)
        self.assertTrue(receipt['app_closed'])
        self.assertTrue((run / 'acceptance.json').exists())
        self.assertTrue((self.state / 'slot').exists())

    def test_autorun_rejects_failed_incomplete_and_stale_completion(self):
        run = self.autorun_evidence()
        done = run / 'shared/cannedbsd-done.txt'
        for token in [b'', b'FAIL\n', b'PASS', b'garbage']:
            done.write_bytes(token)
            with self.assertRaises(guest.Rejection):
                guest.check(self.state, app_closed=True, picture_validator=self.decoder, is_open=lambda paths: False)
            self.assertFalse((run / 'acceptance.json').exists())
        done.write_bytes(b'PASS\n')
        stamp = json.loads((run / 'manifest.json').read_text())['staged_ns']
        os.utime(done, ns=(stamp - 1, stamp - 1))
        with self.assertRaisesRegex(guest.Rejection, 'stale'):
            guest.check(self.state, app_closed=True, picture_validator=self.decoder, is_open=lambda paths: False)

    def test_autorun_rejects_missing_stale_and_undecodable_screenshot(self):
        run = self.autorun_evidence()
        picture = run / 'shared/cannedbsd-screen.pict'
        picture.unlink(); picture.mkdir()
        with self.assertRaisesRegex(guest.Rejection, 'regular'):
            guest.check(self.state, app_closed=True, picture_validator=self.decoder, is_open=lambda paths: False)
        picture.rmdir(); picture.write_bytes(b'picture')
        stamp = json.loads((run / 'manifest.json').read_text())['staged_ns']
        os.utime(picture, ns=(stamp - 1, stamp - 1))
        with self.assertRaisesRegex(guest.Rejection, 'stale'):
            guest.check(self.state, app_closed=True, picture_validator=self.decoder, is_open=lambda paths: False)
        picture.write_bytes(b'invalid PICT')
        with self.assertRaisesRegex(guest.Rejection, 'PICT'):
            guest.check(self.state, app_closed=True, is_open=lambda paths: False)
        self.assertFalse((run / 'acceptance.json').exists())

    def test_default_stage_does_not_enable_autorun(self):
        run = self.stage()
        self.assertFalse(json.loads((run / 'manifest.json').read_text())['autorun'])
        for name in ['autorun.txt', 'done.txt', 'screen.pict']:
            self.assertFalse((run / ('shared/cannedbsd-' + name)).exists())

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

    def test_empty_placeholder_rejected_until_guest_rewrites_it(self):
        run = self.stage()
        self.assertEqual((run / 'shared/cannedbsd-result.txt').read_bytes(), b'')
        with self.assertRaisesRegex(guest.Rejection, 'stale|ALL PASS'):
            guest.check(self.state)
        self.result(run)
        self.assertEqual(guest.check(self.state)['result'], 'ALL PASS')

    def test_missing_and_failed_results_rejected(self):
        run = self.stage()
        (run / 'shared/cannedbsd-result.txt').unlink()
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
        self.assertEqual((second / 'shared/cannedbsd-result.txt').read_bytes(), b'')
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

    def test_native_config_uses_only_staged_disks_and_export(self):
        seed = self.root / 'seed.dsk'
        seed.write_bytes(b'boot')
        rom = self.root / 'Mac.ROM'
        rom.write_bytes(b'rom')
        template = self.root / 'old-prefs'
        template.write_text('disk /old/system\ndisk /old/app\nextfs /old/share\nrom /old/rom\nramsize 134217728\n')
        run = guest.stage(self.artifact, self.state, 'a' * 40, seed, template, rom)
        config = (run / 'basilisk_prefs').read_text()
        self.assertNotIn('/old/', config)
        self.assertIn('ramsize 134217728\n', config)
        self.assertIn('disk ' + str(run / 'System.dsk') + '\n', config)
        self.assertIn('disk ' + str(run / 'CannedBSD.dsk') + '\n', config)
        self.assertIn('extfs ' + str(run / 'shared') + '\n', config)
        self.assertIn('rom ' + str(rom.resolve()) + '\n', config)

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

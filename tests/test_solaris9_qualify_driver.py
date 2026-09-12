#!/usr/bin/env python3
"""Offline regression controls; temporary files only, no transport to a rig."""
import os
from pathlib import Path
import subprocess
import shutil
import signal
import time
import json
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LIB = ROOT / 'tools/lib/solaris9-qualify-lib.sh'
DRIVER = ROOT / 'tools/solaris9-qualify.sh'

class RegressionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='solaris-control-')
        self.addCleanup(self.tmp.cleanup)
        self.env = dict(os.environ, SQ_TEST_ROOT=self.tmp.name)

    def shell(self, code):
        return subprocess.run(['bash', '-c', 'set -uo pipefail\nsource "$1"\n' + code,
                               'control', str(LIB)], env=self.env,
                              text=True, capture_output=True, timeout=15)

    def test_manual_lock_blocks_runner(self):
        r = self.shell('''
sq_rsh() { bash -c "$2"; }
mkdir "$SQ_TEST_ROOT/coordinator.lock"
printf 'manual owner\\n' > "$SQ_TEST_ROOT/coordinator.lock/owner.txt"
sq_acquire_lock "$SQ_TEST_ROOT/coordinator.lock" token runner
''')
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual((Path(self.tmp.name) / 'coordinator.lock/owner.txt').read_text(), 'manual owner\n')

    def test_label_cannot_authorize_release(self):
        r = self.shell('''
sq_rsh() { bash -c "$2"; }
sq_acquire_lock "$SQ_TEST_ROOT/coordinator.lock" realtoken $'label\\nwrongtoken' || exit 99
sq_release_lock "$SQ_TEST_ROOT/coordinator.lock" wrongtoken
''')
        self.assertNotEqual(r.returncode, 99, r.stderr)
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_disconnect_is_not_completion(self):
        r = self.shell('''
sq_rsh() { return 255; }
sq_poll_build /unused 12345 "$(( $(date +%s) + 5 ))" 0 /unused/status token
''')
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_driver_timeout_preserves_lock(self):
        # Execute the driver's own cleanup definition, trap and timeout arm.
        # The transport release is the only stub; no duplicated cleanup policy.
        source = DRIVER.read_text()
        setup = source[source.index('lock_held=0'):source.index('\nsq_acquire_lock')]
        start = source.index('        log "TIMEOUT:')
        arm = source[start:source.index('        ;;', start)]
        r = subprocess.run(['bash', '-c', '''
sq_release_lock() { printf 'RELEASE_CALLED\\n'; }
log() { :; }
lock_base=unused; token=token; build_pid=123
''' + setup + '\nlock_held=1\n' + source[source.index('# From this point'):source.index('sq_swap_media \"')] + arm], text=True, capture_output=True, timeout=10)
        self.assertEqual(r.returncode, 1, r.stderr)
        self.assertNotIn('RELEASE_CALLED', r.stdout)

    def test_stage_result_is_only_name_and_hash(self):
        r = self.shell('''
mkdir "$SQ_TEST_ROOT/input" "$SQ_TEST_ROOT/rig"
printf 'source\\n' > "$SQ_TEST_ROOT/input/item"
tar -cf "$SQ_TEST_ROOT/source.tar" -C "$SQ_TEST_ROOT/input" .
sq_rsh() { (eval "$2"); }
sq_rscp() { cp "$1" "$2"; }
sha256sum() { shasum -a 256 "$@"; }
mkisofs() {
 while [ "$#" -gt 0 ]; do
  if [ "$1" = -o ]; then shift; printf 'mock ISO\\n' > "$1"; return 0; fi
  shift
 done
 return 7
}
sq_stage_iso "$SQ_TEST_ROOT/source.tar" "$SQ_TEST_ROOT/rig" proof
''')
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = r.stdout.splitlines()
        self.assertEqual(len(lines), 2, r.stdout)
        self.assertTrue(lines[0].endswith('.iso'), r.stdout)
        self.assertRegex(lines[1], r'^[0-9a-f]{64}$')


class DriverTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='solaris-driver-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.repo = self.root / 'repo'
        shutil.copytree(ROOT / 'tools', self.repo / 'tools')
        self.rig = self.root / "rig with space and 'quote"
        self.rig.mkdir()
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        fixture = ROOT / 'tests/fixtures/solaris9_fake_transport.py'
        for name in ['ssh', 'timeout', 'date', 'sha256sum', 'mkisofs']:
            shutil.copy(fixture, self.bin / name)
            (self.bin / name).chmod(0o755)
        for name in ['console.py', 'mon.py']:
            shutil.copy(fixture, self.rig / name)
        self.env = dict(os.environ, SQ_TEST_ROOT=str(self.root),
                        GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM='1',
                        PATH=str(self.bin) + os.pathsep + os.environ['PATH'],
                        SOLARIS_SSH_HOSTNAME='offline.invalid',
                        SOLARIS_SSH_HOSTKEYALIAS='offline',
                        SOLARIS_SSH_KNOWNHOSTS=str(self.root / 'unused-known-hosts'),
                        SOLARIS_RIG_DIR=str(self.rig), SOLARIS_DRIVE_ID='drive7',
                        SOLARIS_GUEST_DEV='/dev/offline', OWNER_LABEL="Ryan's\nlabel",
                        EVIDENCE_ROOT=str(self.root / 'evidence'))
        self.git('init', '-q')
        self.git('add', 'tools')
        self.git('-c', 'user.name=Offline Fixture', '-c', 'user.email=fixture@example.invalid',
                 'commit', '-qm', 'fixture')
        self.sha = self.git('rev-parse', 'HEAD').strip()
        self.git('-c', 'user.name=Offline Fixture', '-c', 'user.email=fixture@example.invalid',
                 'tag', '-am', 'annotated fixture', 'tagged')

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.repo), *args],
                                       env=self.env, text=True)

    def run_driver(self, scenario='success', ref='HEAD'):
        self.env['SQ_TEST_SCENARIO'] = scenario
        return subprocess.run(['bash', str(self.repo / 'tools/solaris9-qualify.sh'), ref],
                              env=self.env, text=True, capture_output=True, timeout=15)

    def events(self):
        p = self.root / 'state.json'
        return json.loads(p.read_text()).get('events', []) if p.exists() else []

    def assert_held(self):
        self.assertTrue((self.rig / 'coordinator.lock/token').is_file())

    def test_happy_path_actual_driver_and_annotated_commit(self):
        r = self.run_driver(ref='tagged')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertFalse((self.rig / 'coordinator.lock').exists())
        event = self.events()
        self.assertLess(event.index('guest:unmount'), event.index('monitor:eject'))
        self.assertLess(event.index('guest:extract'), event.index('guest:start'))
        self.assertEqual(event[-1], 'guest:unmount')
        runs = list((self.root / 'evidence').iterdir())
        self.assertEqual(len(runs), 1)
        self.assertEqual((runs[0] / 'commit-sha.txt').read_text().strip(), self.sha)
        self.assertIn('SOLARIS9_CANNEDBSD_TEST=PASS', (runs[0] / 'native-transcript.log').read_text())

    def test_legacy_lock_never_changes(self):
        lock = self.rig / 'coordinator.lock'
        lock.mkdir()
        (lock / 'owner.txt').write_text('manual owner')
        r = self.run_driver()
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual((lock / 'owner.txt').read_text(), 'manual owner')
        self.assertFalse((lock / 'token').exists())
        self.assertEqual(self.events(), [])

    def test_noncommit_ref_never_acquires_rig(self):
        tree = self.git('rev-parse', 'HEAD^{tree}').strip()
        r = self.run_driver(ref=tree)
        self.assertNotEqual(r.returncode, 0)
        self.assertFalse((self.rig / 'coordinator.lock').exists())
        self.assertEqual(self.events(), [])

    def test_stage_failure_releases_before_any_guest_use(self):
        r = self.run_driver('mkisofs_failure')
        self.assertNotEqual(r.returncode, 0)
        self.assertFalse((self.rig / 'coordinator.lock').exists())
        self.assertEqual(self.events(), [])

    def test_release_failure_cannot_return_success(self):
        r = self.run_driver('release_failure')
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.events()[-1], 'guest:unmount')
        self.assert_held()

    def test_mount_failure_never_ejects(self):
        r = self.run_driver('busy_unmount')
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(self.events(), ['guest:unmount'])
        self.assert_held()

    def test_wrong_drive_cannot_authorize_extract(self):
        r = self.run_driver('wrong_drive')
        self.assertNotEqual(r.returncode, 0)
        self.assertNotIn('guest:extract', self.events())
        self.assert_held()

    def test_cpio_failure_never_starts_build(self):
        r = self.run_driver('cpio_failure')
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('guest:extract', self.events())
        self.assertNotIn('guest:start', self.events())
        self.assert_held()

    def test_uncertain_start_and_completion_keep_ownership(self):
        for scenario in ['start_disconnect', 'poll_disconnect', 'timeout',
                         'wrong_nonce', 'truncated_frame', 'wrong_run', 'numeric_noise']:
            with self.subTest(scenario=scenario):
                # Independent rigs/evidence per run; never erase a held lock.
                fresh = DriverTests('test_uncertain_start_and_completion_keep_ownership')
                fresh.setUp()
                try:
                    r = fresh.run_driver(scenario)
                    self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
                    fresh.assert_held()
                    event = fresh.events()
                    self.assertIn('guest:start', event)
                    self.assertNotIn('guest:transcript', event)
                    self.assertNotIn('outcome: PASS', r.stdout)
                finally:
                    fresh.doCleanups()

    def test_completed_failed_build_cleans_up_without_passing(self):
        for scenario in ['nonzero_exit', 'missing_pass']:
            with self.subTest(scenario=scenario):
                fresh = DriverTests('test_completed_failed_build_cleans_up_without_passing')
                fresh.setUp()
                try:
                    r = fresh.run_driver(scenario)
                    self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
                    self.assertFalse((fresh.rig / 'coordinator.lock').exists())
                    self.assertEqual(fresh.events()[-1], 'guest:unmount')
                    self.assertNotIn('outcome: PASS', r.stdout)
                finally:
                    fresh.doCleanups()

    def test_interrupt_during_poll_retains_lock(self):
        self.env['SQ_TEST_SCENARIO'] = 'interrupt'
        proc = subprocess.Popen(['bash', str(self.repo / 'tools/solaris9-qualify.sh'), 'HEAD'],
                                env=self.env, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, start_new_session=True)
        try:
            deadline = time.monotonic() + 8
            while 'guest:poll' not in self.events() and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertIn('guest:poll', self.events())
            proc.send_signal(signal.SIGTERM)
            out, err = proc.communicate(timeout=5)
            self.assertNotEqual(proc.returncode, 0, out + err)
            self.assert_held()
            self.assertNotIn('outcome: PASS', out)
        finally:
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.communicate()

if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
"""Offline exact framing, ownership and generated guest shell controls."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LIB = ROOT / 'tools/lib/solaris9-qualify-lib.sh'
spec = importlib.util.spec_from_file_location('sq_protocol', ROOT / 'tools/lib/solaris9-protocol.py')
protocol = importlib.util.module_from_spec(spec)
spec.loader.exec_module(protocol)


class ParserTests(unittest.TestCase):
    def test_echo_crlf_and_foreign_markers(self):
        raw = b"# printf 'SQ_BEGIN_run'\r\nSQ_BEGIN_run\r\nvalue\r\nSQ_END_run:0\r\nCB_DONE_42\r\n# "
        self.assertEqual(protocol.frame(raw, 'run'), b'value\n')

    def test_bad_frames(self):
        for raw in [b'SQ_BEGIN_run\nvalue', b'SQ_BEGIN_other\nSQ_END_other:0',
                    b'SQ_BEGIN_run\nSQ_BEGIN_run\nSQ_END_run:0',
                    b'SQ_END_run:0\nSQ_BEGIN_run',
                    b'SQ_BEGIN_run\nSQ_END_run:0\nSQ_END_run:0',
                    b'SQ_BEGIN_run\nSQ_END_run:7', b'SQ_BEGIN_run\nSQ_END_run:00',
                    b'SQ_BEGIN_run\nSQ_END_run:256']:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                protocol.frame(raw, 'run')

    def test_exact_drive_and_path(self):
        protocol.block(b'drive7 (#block3): /source.iso (raw, read-only)\n', 'drive7', '/source.iso')
        protocol.block(b'drive7: [not inserted]\n', 'drive7', '--empty')
        for raw in [b'drive7: [not inserted]\ndrive8: /source.iso (raw)\n',
                    b'drive70: /source.iso (raw)\n',
                    b'drive7: /source.iso.old (raw)\n',
                    b'drive7: /other.iso (raw)\n  /source.iso (raw)\n',
                    b'drive7: /source.iso (raw)\ndrive7: /source.iso (raw)\n']:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                protocol.block(raw, 'drive7', '/source.iso')


class ShellTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='solaris-shell-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.env = dict(os.environ, SQ_TEST_ROOT=str(self.root))

    def shell(self, code):
        return subprocess.run(['bash', '-c', 'set -uo pipefail\nsource "$1"\n' + code,
                               'test', str(LIB)], env=self.env, text=True,
                              capture_output=True, timeout=10)

    def test_atomic_contenders_and_exact_release(self):
        r = self.shell('''
sq_rsh() { bash -c "$2"; }
for i in 1 2 3 4 5 6 7 8; do
 (sq_acquire_lock "$SQ_TEST_ROOT/lock" "token-$i" "owner-$i" && echo "$i" > "$SQ_TEST_ROOT/winner-$i") &
done
wait
set -- "$SQ_TEST_ROOT"/winner-*
[ "$#" = 1 ] || exit 11
token=$(cat "$SQ_TEST_ROOT/lock/token")
printf '%s\\nextra\\n' "$token" > "$SQ_TEST_ROOT/lock/token"
if sq_release_lock "$SQ_TEST_ROOT/lock" "$token"; then exit 12; fi
printf '%s\\n' "$token" > "$SQ_TEST_ROOT/lock/token"
sq_release_lock "$SQ_TEST_ROOT/lock" "$token" || exit 13
[ ! -e "$SQ_TEST_ROOT/lock" ] || exit 14
''')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_collision_preserves_prior_remote_archive(self):
        r = self.shell('''
sq_rsh() { eval "$2"; }
sq_rscp() { cp "$1" "$2"; }
mkdir "$SQ_TEST_ROOT/rig" "$SQ_TEST_ROOT/rig/qualify-same"
printf 'prior archive\\n' > "$SQ_TEST_ROOT/rig/qualify-same/source.tar.gz"
printf 'different archive\\n' > "$SQ_TEST_ROOT/new.tar"
if sq_stage_iso "$SQ_TEST_ROOT/new.tar" "$SQ_TEST_ROOT/rig" same; then exit 11; fi
[ "$(cat "$SQ_TEST_ROOT/rig/qualify-same/source.tar.gz")" = 'prior archive' ] || exit 12
''')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def capture(self, helper, body):
        r = self.shell('''
sq_guest() { printf '%s' "$3" > "$SQ_TEST_ROOT/command"; printf '%s\\n' ''' + body + '''; }
''' + helper)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return (self.root / 'command').read_text()

    def test_generated_extract_checks_actual_command_status(self):
        # Run the actual generated command with only its host-sensitive commands
        # substituted. /mnt and every destination are inside this test's tempdir.
        for failure in ['mount', 'find', 'cpio', 'collision', 'none']:
            with self.subTest(failure=failure):
                dest = self.root / ('extract-' + failure)
                mount = self.root / ('mnt-' + failure)
                mount.mkdir()
                if failure == 'collision':
                    dest.mkdir()
                    (dest / 'sentinel').write_text('unchanged')
                self.env['SQ_DEST'] = str(dest)
                command = self.capture('sq_mount_and_extract unused /dev/fake "$SQ_DEST"', 'SQ_EXTRACTED')
                command = command.replace('/mnt', str(mount))
                self.env['SQ_FAIL'] = failure
                r = subprocess.run(['bash', '-c', '''
mount() { [ "$SQ_FAIL" != mount ]; }
find() { printf '.\\n'; [ "$SQ_FAIL" != find ]; }
cpio() { printf 'called' > "$SQ_TEST_ROOT/cpio-called"; [ "$SQ_FAIL" != cpio ]; }
''' + command], env=self.env, text=True, capture_output=True, timeout=5)
                if failure == 'none':
                    self.assertEqual(r.returncode, 0, r.stderr)
                    self.assertEqual(r.stdout, 'SQ_EXTRACTED\n')
                else:
                    self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
                    self.assertNotIn('SQ_EXTRACTED', r.stdout)
                if failure in ['mount', 'find', 'collision']:
                    self.assertFalse((self.root / 'cpio-called').exists())
                if failure == 'collision':
                    self.assertEqual((dest / 'sentinel').read_text(), 'unchanged')
                (self.root / 'cpio-called').unlink(missing_ok=True)

    def test_generated_unmount_checks_table_and_postcondition(self):
        cases = [('missing', None, False), ('malformed', 'bad\n', False),
                 ('no-root', 'source /mnt hsfs ro 1\n', False),
                 ('busy', 'root / ufs rw 1\nsource /mnt hsfs ro 1\n', False),
                 ('still-mounted', 'root / ufs rw 1\nsource /mnt hsfs ro 1\n', False),
                 ('clear', 'root / ufs rw 1\n', True),
                 ('mounted', 'root / ufs rw 1\nsource /mnt hsfs ro 1\n', True)]
        for name, table, success in cases:
            with self.subTest(name=name):
                mnttab = self.root / 'mnttab'
                if table is not None:
                    mnttab.write_text(table)
                else:
                    mnttab.unlink(missing_ok=True)
                command = self.capture('sq_unmount unused', 'SQ_UNMOUNTED')
                command = command.replace('/etc/mnttab', str(mnttab))
                self.env['SQ_CASE'] = name
                r = subprocess.run(['bash', '-c', '''
umount() {
 [ "$SQ_CASE" != busy ] || return 1
 [ "$SQ_CASE" != still-mounted ] || return 0
 printf 'root / ufs rw 1\\n' > "$SQ_TEST_ROOT/mnttab"
}
''' + command], env=self.env, text=True, capture_output=True, timeout=5)
                self.assertEqual(r.returncode == 0, success, r.stdout + r.stderr)
                self.assertEqual('SQ_UNMOUNTED' in r.stdout, success)


if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
"""Stage an exact CI artifact and validate a manually launched guest run."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tarfile
import tempfile
import time

class Rejection(Exception):
    """The available evidence cannot support guest acceptance."""


def expected_result():
    """Use the same command list compiled into the guest and Linux tests."""
    lines = ['cannedBSD System 7 / Retro68', 'PASS contexts']
    for case in Path(__file__).with_name('acceptance_cases.def').read_text().splitlines():
        match = re.fullmatch(r'CB_MAC_CASE\(("(?:[^"\\]|\\.)*"), ("(?:[^"\\]|\\.)*"), [0-9]+\)', case)
        if match is None:
            raise Rejection('invalid checked-in Mac acceptance case')
        lines.append('PASS ' + json.loads(match[1]))
    return '\n'.join(lines + ['ALL PASS', ''])


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


def write_json(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def stage(artifact, state, expected_commit, boot_seed, native_template=None, rom=None, autorun=False):
    artifact, state = Path(artifact).resolve(), Path(state).resolve()
    native_settings = None
    if native_template is not None or rom is not None:
        if native_template is None or rom is None or boot_seed is None:
            raise Rejection('native configuration needs --native-template, --rom, and --boot-seed')
        rom = Path(rom).resolve()
        if not rom.is_file() or any('\n' in str(path) or '\r' in str(path) for path in (state, rom)):
            raise Rejection('native configuration needs an existing ROM and single-line paths')
        native_settings = '\n'.join(
            line for line in Path(native_template).read_text().splitlines()
            if not line.split() or line.split()[0] not in ('disk', 'extfs', 'rom'))
    commit = (artifact / 'commit.txt').read_text().strip()
    if not re.fullmatch('[0-9a-f]{40}', expected_commit) or commit != expected_commit:
        raise Rejection('commit.txt does not match the expected full commit SHA')
    sums = (artifact / 'SHA256SUMS').read_text()
    match = re.fullmatch(r'([0-9a-f]{64}) [ *]CannedBSD\.tar\.gz\n?', sums)
    archive_path = artifact / 'CannedBSD.tar.gz'
    if match is None or digest(archive_path) != match[1]:
        raise Rejection('artifact checksum mismatch or unsupported SHA256SUMS format')
    archive_sha = match[1]
    # Extract only the expected regular HFS disk, never archive paths or links.
    with tarfile.open(archive_path, 'r:gz') as archive:
        disks = [member for member in archive.getmembers() if member.name == 'CannedBSD.dsk']
        if len(disks) != 1 or not disks[0].isfile() or not 0 < disks[0].size <= 512 * 1024 * 1024:
            raise Rejection('archive must contain exactly one regular CannedBSD.dsk (1..512 MiB)')
        state.mkdir(parents=True, exist_ok=True)
        slot = state / 'slot'
        try:
            slot.mkdir()
        except FileExistsError as error:
            raise Rejection('guest slot occupied; inspect status and shut down the guest before release') from error
        try:
            run = Path(tempfile.mkdtemp(prefix='run-', dir=state))
            (run / 'shared').mkdir()
            # Native extfs can open an existing result more reliably than HCreate.
            # This empty placeholder predates staging and can never pass check.
            (run / 'shared/cannedbsd-result.txt').write_bytes(b'')
            if autorun:
                for name in ('autorun.txt', 'screen.pict', 'done.txt'):
                    (run / ('shared/cannedbsd-' + name)).write_bytes(b'')
            (run / 'expected-result.txt').write_text(expected_result())
            shutil.copyfile(archive_path, run / 'CannedBSD.tar.gz')
            if digest(run / 'CannedBSD.tar.gz') != archive_sha:
                raise Rejection('artifact changed during staging (checksum mismatch)')
            with tarfile.open(run / 'CannedBSD.tar.gz', 'r:gz') as staged_archive:
                with staged_archive.extractfile('CannedBSD.dsk') as source, (run / 'CannedBSD.dsk').open('wb') as dest:
                    shutil.copyfileobj(source, dest)
            if boot_seed is not None:
                shutil.copyfile(Path(boot_seed).resolve(), run / 'System.dsk')
            if native_settings is not None:
                (run / 'basilisk_prefs').write_text(
                    'disk ' + str(run / 'System.dsk') + '\n'
                    'disk ' + str(run / 'CannedBSD.dsk') + '\n'
                    'extfs ' + str(run / 'shared') + '\n'
                    'rom ' + str(rom) + '\n' + native_settings + '\n')
            manifest = {'commit': commit, 'artifact_sha256': archive_sha,
                        'disk_sha256': digest(run / 'CannedBSD.dsk'),
                        'staged_ns': time.time_ns(), 'run_directory': str(run),
                        'boot_copy': boot_seed is not None, 'autorun': bool(autorun)}
            write_json(run / 'manifest.json', manifest)
            write_json(slot / 'active.json', {'run_directory': str(run)})
            return run
        except BaseException:
            shutil.rmtree(slot)
            raise


def active(state):
    state = Path(state).resolve()
    try:
        run = Path(json.loads((state / 'slot/active.json').read_text())['run_directory'])
    except FileNotFoundError as error:
        raise Rejection('no staged run in the guest slot') from error
    if run.parent != state or not run.name.startswith('run-'):
        raise Rejection('invalid run directory in slot')
    return run, json.loads((run / 'manifest.json').read_text())


def validate_picture(picture, output):
    """Decode the actual guest PICT; a nonempty file alone is not evidence."""
    data = picture.read_bytes()
    if len(data) < 526 or data[522:526] != b'\x00\x11\x02\xff':
        raise Rejection('guest screenshot is not an extended version-2 PICT')
    top, left, bottom, right = struct.unpack_from('>4h', data, 514)
    width, height = right - left, bottom - top
    if not 1 <= width <= 8192 or not 1 <= height <= 8192:
        raise Rejection('invalid guest screenshot bounds')
    # The driver already uses this pinned local OpenCV runtime for matching.
    try:
        import cv2
    except ImportError as error:
        raise Rejection('autorun screenshot validation requires the local OpenCV runtime') from error
    with tempfile.TemporaryDirectory(prefix='picture-', dir=output.parent) as temporary:
        decoded = Path(temporary) / 'screen.png'
        try:
            conversion = subprocess.run(['/usr/bin/sips', '-s', 'format', 'png',
                                         str(picture), '--out', str(decoded)],
                                        capture_output=True, timeout=15, check=False)
        except (OSError, subprocess.TimeoutExpired) as error:
            raise Rejection('guest screenshot decoder failed') from error
        if conversion.returncode != 0:
            raise Rejection('guest screenshot cannot be decoded')
        image = cv2.imread(str(decoded), cv2.IMREAD_GRAYSCALE)
        validate_pixels(image, width, height)
        decoded.replace(output)
    return {'screenshot_width': width, 'screenshot_height': height,
            'screenshot_png_sha256': digest(output)}


def validate_pixels(image, width, height):
    if image is None or image.shape != (height, width):
        raise Rejection('guest screenshot decode dimensions do not match PICT')
    if int((image < 128).sum()) < 100 or int((image > 240).sum()) < 100:
        raise Rejection('guest screenshot is blank or has insufficient visible content')


def fresh_file(run, manifest, name, maximum):
    path = run / 'shared' / name
    if not path.is_file() or path.is_symlink():
        raise Rejection('missing regular ' + name)
    stat = path.stat()
    if stat.st_mtime_ns <= manifest['staged_ns']:
        raise Rejection('stale ' + name)
    if not 0 < stat.st_size <= maximum:
        raise Rejection('empty or oversized ' + name)
    return path


def inspect(state, picture_validator=validate_picture):
    run, manifest = active(state)
    result = run / 'shared/cannedbsd-result.txt'
    if not result.is_file() or result.is_symlink():
        raise Rejection('missing regular guest result; screen output alone is not automated acceptance')
    stat = result.stat()
    if stat.st_mtime_ns <= manifest['staged_ns']:
        raise Rejection('stale guest result (not newer than staging)')
    if stat.st_size > 65536:
        raise Rejection('guest result exceeds 64 KiB')
    evidence = result.read_bytes()
    lines = evidence.decode('ascii').splitlines()
    if lines != expected_result().splitlines():
        raise Rejection('guest result does not contain complete ALL PASS evidence')
    if digest(run / 'CannedBSD.tar.gz') != manifest['artifact_sha256']:
        raise Rejection('staged artifact checksum changed')
    receipt = dict(manifest, result_sha256=hashlib.sha256(evidence).hexdigest(),
                   result_mtime_ns=stat.st_mtime_ns, checked_ns=time.time_ns(),
                   result='ALL PASS', verification='fresh shared directory and host mtime')
    if manifest.get('autorun', False):
        done = fresh_file(run, manifest, 'cannedbsd-done.txt', 16)
        if done.read_bytes() != b'PASS\n':
            raise Rejection('autorun completion is not PASS')
        picture = fresh_file(run, manifest, 'cannedbsd-screen.pict', 8 * 1024 * 1024)
        receipt.update(picture_validator(picture, run / 'cannedbsd-screen.png'))
        receipt.update(done_sha256=digest(done), screenshot_sha256=digest(picture))
    return receipt


def check(state, app_closed=False, picture_validator=validate_picture, is_open=None):
    run, manifest = active(state)
    if manifest.get('autorun', False) and not app_closed:
        raise Rejection('autorun requires observed CannedBSD window closure before acceptance')
    if manifest.get('autorun', False):
        paths = [run / 'CannedBSD.dsk']
        if (run / 'System.dsk').exists():
            paths.append(run / 'System.dsk')
        if (is_open or disks_open)(paths):
            raise Rejection('autorun guest disks are still open; shutdown is incomplete')
    receipt = inspect(state, picture_validator)
    if manifest.get('autorun', False):
        receipt.update(app_closed=True, guest_disks_closed=True)
    write_json(run / 'acceptance.json', receipt)
    return receipt


def disks_open(paths):
    if shutil.which('lsof') is None:
        raise Rejection('lsof is required to verify the guest disks are closed')
    result = subprocess.run(['lsof', '-t', '--'] + [str(path) for path in paths],
                            capture_output=True, text=True, check=False)
    if result.returncode not in (0, 1) or result.stderr.strip():
        raise Rejection('cannot verify disk ownership with lsof: ' + result.stderr.strip())
    return result.returncode == 0


def release(state, is_open=disks_open):
    run, _ = active(state)
    paths = [run / 'CannedBSD.dsk']
    if (run / 'System.dsk').exists():
        paths.append(run / 'System.dsk')
    if is_open(paths):
        raise Rejection('guest disk is still open; complete guest shutdown before releasing the slot')
    # Run directories and results remain as evidence, even after failed runs.
    shutil.rmtree(Path(state) / 'slot')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    prepare = sub.add_parser('stage')
    prepare.add_argument('--artifact', type=Path, required=True)
    prepare.add_argument('--state', type=Path, required=True)
    prepare.add_argument('--commit', required=True, help='expected full Woodpecker commit SHA')
    prepare.add_argument('--boot-seed', type=Path, help='copy a known-good shutdown boot disk into this run')
    prepare.add_argument('--native-template', type=Path, help='existing Basilisk preferences; disk/extfs/rom entries replaced')
    prepare.add_argument('--rom', type=Path, help='ROM to use with --native-template and --boot-seed')
    prepare.add_argument('--autorun', action='store_true', help='precreate guest autorun marker and all evidence files')
    check_parser = sub.add_parser('check')
    check_parser.add_argument('--state', type=Path, required=True)
    check_parser.add_argument('--app-closed', action='store_true', help='controller observed CannedBSD window closed (autorun only)')
    for command in ('inspect', 'status', 'release'):
        sub.add_parser(command).add_argument('--state', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == 'stage':
            run = stage(args.artifact, args.state, args.commit, args.boot_seed, args.native_template, args.rom, args.autorun)
            print(run)
            if args.boot_seed is not None:
                print('Boot disk: ' + str(run / 'System.dsk'))
            print('Mount disk: ' + str(run / 'CannedBSD.dsk'))
            if args.native_template is not None:
                print('Native configuration: ' + str(run / 'basilisk_prefs'))
            print('Set extfs: ' + str(run / 'shared'))
        elif args.command == 'check':
            print(json.dumps(check(args.state, args.app_closed), indent=2))
        elif args.command == 'inspect':
            print(json.dumps(inspect(args.state), indent=2))
        elif args.command == 'status':
            _, manifest = active(args.state)
            print(json.dumps(manifest, indent=2))
        else:
            release(args.state)
            print('Guest slot released; run evidence retained.')
    except (Rejection, OSError, ValueError, tarfile.TarError) as error:
        parser.exit(1, 'REJECTED: ' + str(error) + '\n')

if __name__ == '__main__':
    main()

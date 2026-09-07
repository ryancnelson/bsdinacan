#!/usr/bin/env python3
"""Stage an exact CI artifact and validate a manually launched guest run."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import time

class Rejection(Exception):
    """The available evidence cannot support guest acceptance."""


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


def stage(artifact, state, expected_commit, boot_seed):
    artifact, state = Path(artifact).resolve(), Path(state).resolve()
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
            shutil.copyfile(archive_path, run / 'CannedBSD.tar.gz')
            if digest(run / 'CannedBSD.tar.gz') != archive_sha:
                raise Rejection('artifact changed during staging (checksum mismatch)')
            with tarfile.open(run / 'CannedBSD.tar.gz', 'r:gz') as staged_archive:
                with staged_archive.extractfile('CannedBSD.dsk') as source, (run / 'CannedBSD.dsk').open('wb') as dest:
                    shutil.copyfileobj(source, dest)
            if boot_seed is not None:
                shutil.copyfile(Path(boot_seed).resolve(), run / 'System.dsk')
            manifest = {'commit': commit, 'artifact_sha256': archive_sha,
                        'disk_sha256': digest(run / 'CannedBSD.dsk'),
                        'staged_ns': time.time_ns(), 'run_directory': str(run),
                        'boot_copy': boot_seed is not None}
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


def check(state):
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
    if (not lines or lines[-1] != 'ALL PASS' or 'PASS contexts' not in lines
            or sum(line.startswith('PASS ') for line in lines) < 8
            or any(line.startswith('FAIL') for line in lines)):
        raise Rejection('guest result does not contain complete ALL PASS evidence')
    if digest(run / 'CannedBSD.tar.gz') != manifest['artifact_sha256']:
        raise Rejection('staged artifact checksum changed')
    receipt = dict(manifest, result_sha256=hashlib.sha256(evidence).hexdigest(),
                   result_mtime_ns=stat.st_mtime_ns, checked_ns=time.time_ns(),
                   result='ALL PASS', verification='fresh shared directory and host mtime')
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
    for command in ('check', 'status', 'release'):
        sub.add_parser(command).add_argument('--state', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == 'stage':
            run = stage(args.artifact, args.state, args.commit, args.boot_seed)
            print(run)
            print('Mount disk: ' + str(run / 'CannedBSD.dsk'))
            print('Set extfs: ' + str(run / 'shared'))
        elif args.command == 'check':
            print(json.dumps(check(args.state), indent=2))
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

#!/usr/bin/env python3
"""Fail-closed parsing of console frames and one exact QEMU block entry."""
import re
import sys


def frame(raw, nonce):
    lines = raw.replace(b'\r\n', b'\n').replace(b'\r', b'\n').split(b'\n')
    begin = b'SQ_BEGIN_' + nonce.encode('ascii')
    end = b'SQ_END_' + nonce.encode('ascii') + b':'
    starts = [i for i, line in enumerate(lines) if line == begin]
    ends = [i for i, line in enumerate(lines) if line.startswith(end)]
    if len(starts) != 1 or len(ends) != 1 or starts[0] >= ends[0]:
        raise ValueError('missing, duplicate or out-of-order console frame')
    status = lines[ends[0]][len(end):]
    if not re.fullmatch(rb'(0|[1-9][0-9]{0,2})', status) or int(status) > 255:
        raise ValueError('invalid guest command status')
    if status != b'0':
        raise ValueError('guest command exited ' + status.decode())
    return b'\n'.join(lines[starts[0] + 1:ends[0]]) + b'\n'


def block(raw, drive, expected):
    # HMP emits either "drive7:" or "drive7 (#block1):". Match that
    # entry's first line only; another drive/continuation cannot satisfy it.
    pattern = re.compile(r'^' + re.escape(drive) + r'(?: \([^\r\n]*\))?: (.*)$')
    entries = [m.group(1) for line in raw.decode().splitlines()
               if (m := pattern.fullmatch(line))]
    if len(entries) != 1:
        raise ValueError('missing or duplicate requested block entry')
    value = entries[0]
    if expected == '--empty':
        if value.strip() != '[not inserted]':
            raise ValueError('requested drive is not empty')
    elif not re.fullmatch(re.escape(expected) + r' \(raw(?:, [^\r\n()]*)?\)', value):
        raise ValueError('requested drive does not contain exact source path')


def main():
    try:
        if sys.argv[1] == 'frame' and len(sys.argv) == 3:
            sys.stdout.buffer.write(frame(sys.stdin.buffer.read(), sys.argv[2]))
        elif sys.argv[1] == 'block' and len(sys.argv) == 4:
            block(sys.stdin.buffer.read(), sys.argv[2], sys.argv[3])
        else:
            raise ValueError('invalid protocol parser invocation')
    except (ValueError, UnicodeError) as exc:
        print('solaris9 protocol: ' + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"

program=${PROGRAM_PATH:-$(make -s print-program)}

if [ ! -x "$program" ]; then
    echo "FAIL: $program is not executable" >&2
    exit 1
fi

output=$("$program" -c 'echo launcher')
if [ "$output" != launcher ]; then
    echo "FAIL: bsdinacan output was <$output>" >&2
    exit 1
fi

# A failed early stage must leave the shell's inherited stdin usable.
if ! output=$(printf 'still-open\n' | "$program" -c \
    'cat < /missing | cat; cat' 2>/dev/null) || [ "$output" != still-open ]; then
    echo 'FAIL: pipeline failure damaged stdin' >&2
    exit 1
fi

echo "launcher test passed"

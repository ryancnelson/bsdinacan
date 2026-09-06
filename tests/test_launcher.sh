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

echo "launcher test passed"

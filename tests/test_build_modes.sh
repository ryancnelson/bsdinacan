#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"

make clean >/dev/null
make sanitize >/dev/null
make test >/dev/null

program=$(make --no-print-directory -s print-program)
if [ ! -x "$program" ]; then
    echo "FAIL: selected plain executable does not exist: $program" >&2
    exit 1
fi
if ldd "$program" | grep -q 'libasan'; then
    echo "FAIL: plain make test reused sanitizer artifact: $program" >&2
    exit 1
fi

echo "build-mode isolation passed"

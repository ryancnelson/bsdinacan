#!/bin/sh
set -eu
scratch=$(mktemp -d "${TMPDIR:-/tmp}/cannedbsd-sanitizer.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
cat > "$scratch/control.c" <<'SOURCE'
#include <limits.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    volatile int value = argc > 1 ? INT_MAX : 0;
    (void)argv;
    value += 1;
    puts("control completed");
    return 0;
}
SOURCE
# Intentional word splitting: compiler/flags have the same shell semantics
# as the Makefile's compilation commands. The control is never shipped.
${CC:-clang} -std=c99 -Wall -Wextra -Werror -O1 ${SANITIZER_FLAGS:?} \
    "$scratch/control.c" -o "$scratch/control"
UBSAN_OPTIONS=halt_on_error=0 "$scratch/control" > "$scratch/clean.out" 2> "$scratch/clean.err"
test "$(cat "$scratch/clean.out")" = 'control completed'
test ! -s "$scratch/clean.err"
status=0
UBSAN_OPTIONS=halt_on_error=0 "$scratch/control" overflow > "$scratch/bad.out" 2> "$scratch/bad.err" || status=$?
if [ "$status" -eq 0 ]; then
    echo 'FAIL: undefined behavior was reported but did not fail the process' >&2
    cat "$scratch/bad.err" >&2
    exit 1
fi
grep -q 'runtime error: signed integer overflow' "$scratch/bad.err"
if grep -q 'control completed' "$scratch/bad.out"; then
    echo 'FAIL: execution continued after undefined behavior' >&2
    exit 1
fi
echo 'sanitizer fatal-error policy passed'

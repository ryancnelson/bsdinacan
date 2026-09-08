#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
program=${PROGRAM_PATH:-$(make -s print-program)}

case_dir=$(mktemp -d "${TMPDIR:-/tmp}/cannedbsd-echo.XXXXXX")
trap 'rm -rf "$case_dir"' EXIT
trap 'exit 1' HUP INT TERM

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

check_sanitizer_reports() {
    local report
    for report in "$case_dir"/asan.*; do
        [ -f "$report" ] || continue
        # The existing ucontext backend emits this precise ASan warning.
        # Keep sanitizer output separate from command stderr, and reject all
        # other reports (including errors on cases expecting exit status 1).
        if ! awk '
            NR == 1 && /^==[0-9]+==WARNING: ASan is ignoring requested __asan_handle_no_return: stack type: default top: 0x[0-9a-f]+; bottom 0x[0-9a-f]+; size: 0x[0-9a-f]+ \([0-9]+\)$/ { next }
            NR == 2 && $0 == "False positive error reports may follow" { next }
            NR == 3 && $0 == "For details see https://github.com/google/sanitizers/issues/189" { next }
            { bad = 1 }
            END { exit bad || NR != 3 }
        ' "$report"; then
            cat "$report" >&2
            fail "unexpected sanitizer diagnostic"
        fi
        rm -f "$report"
    done
}

check_case() {
    local cmd="$1"
    local exp_status="$2"
    local exp_stdout="$3"
    local exp_stderr="$4"
    local name="$5"

    printf "%b" "$exp_stdout" > "$case_dir/exp_stdout"
    printf "%b" "$exp_stderr" > "$case_dir/exp_stderr"

    set +e
    ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}log_path=$case_dir/asan" \
        "$program" -c "$cmd" > "$case_dir/out" 2> "$case_dir/err"
    local status=$?
    set -e
    check_sanitizer_reports

    if [ "$status" -ne "$exp_status" ]; then
        fail "$name: expected status $exp_status, got $status"
    fi

    if ! cmp -s "$case_dir/exp_stdout" "$case_dir/out"; then
        echo "$name stdout mismatch:" >&2
        diff -u "$case_dir/exp_stdout" "$case_dir/out" >&2 || true
        fail "$name: stdout mismatch"
    fi

    if ! cmp -s "$case_dir/exp_stderr" "$case_dir/err"; then
        echo "$name stderr mismatch:" >&2
        diff -u "$case_dir/exp_stderr" "$case_dir/err" >&2 || true
        fail "$name: stderr mismatch"
    fi
}

# This matrix is derived directly from reading the pinned upstream source's
# control flow (upstream/netbsd/bin/echo/echo.c): it never calls getopt(3),
# recognizes only a literal first "-n" argument, never treats "--" or "-e"
# as special, and never interprets backslash sequences. There is no
# usage/invalid-option error path at all, unlike dirname/basename.

check_case 'netbsdecho' 0 "\n" "" "no arguments"
check_case 'netbsdecho ""' 0 "\n" "" "single empty argument"
check_case "netbsdecho ' '" 0 " \n" "" "single space argument"

check_case 'netbsdecho -n foo bar' 0 "foo bar" "" "-n suppresses the trailing newline"

check_case 'netbsdecho -- foo' 0 "-- foo\n" "" "-- is an ordinary operand, not an end-of-options marker"
check_case 'netbsdecho -e foo' 0 "-e foo\n" "" "-e is an ordinary operand, not an escape flag"

# The two source characters backslash-n inside the single-quoted operand
# must reach argv, and echo's own control flow, completely literally: this
# project's inner shell only strips backslash escaping within single
# quotes. printf's own %b then turns only the *trailing* "\n" this test
# supplies into a real newline, matching echo's own unconditional final
# putchar('\n').
check_case "netbsdecho 'a\\nb'" 0 "a\\\\nb\n" "" "backslash sequences are printed literally, not interpreted"

check_case 'netbsdecho one two three' 0 "one two three\n" "" "multiple operands are space-joined"

echo 'echo behavioral matrix passed'

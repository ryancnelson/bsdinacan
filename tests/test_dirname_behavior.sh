#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
program=${PROGRAM_PATH:-$(make -s print-program)}

case_dir=$(mktemp -d "${TMPDIR:-/tmp}/cannedbsd-dirname.XXXXXX")
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

    # Expectations below are fixed fixtures with explicit newline escapes.
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

check_case 'dirname /tmp/example' 0 "/tmp\n" "" "ordinary path"
check_case 'dirname usr/bin/' 0 "usr\n" "" "trailing slash path"
check_case 'dirname plain' 0 ".\n" "" "plain name"
check_case 'dirname /' 0 "/\n" "" "root"
check_case 'dirname ////' 0 "/\n" "" "repeated root slashes"
check_case 'dirname /foo////bar' 0 "/foo\n" "" "repeated middle slashes"
check_case 'dirname ""' 0 ".\n" "" "empty argument"

check_case 'dirname' 1 "" "usage: dirname path\n" "zero arguments"
check_case 'dirname a b' 1 "" "usage: dirname path\n" "too many arguments"
check_case 'dirname -x' 1 "" "dirname: illegal option -- x\nusage: dirname path\n" "invalid option"

check_case 'dirname -- -leading/dash' 0 "-leading\n" "" "dash-leading path"

check_case 'dirname /foo/bar; dirname /baz/qux' 0 "/foo\n/baz\n" "" "repeated invocations"

check_case 'dirname /a/b/c | cat' 0 "/a/b\n" "" "pipeline"

echo 'dirname behavioral matrix passed'

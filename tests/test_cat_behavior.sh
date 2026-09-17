#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
program=${PROGRAM_PATH:-$(make -s print-program)}

case_dir=$(mktemp -d "${TMPDIR:-/tmp}/cannedbsd-cat.XXXXXX")
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

# Each check_case is a fresh boot (a new `$program -c` process).
#
# -n/-b were excluded here until FORMAT-01 landed width-qualified %d/%s
# support in the internal printf/fprintf formatter (cat.c's line-
# numbering path uses fprintf(stdout, "%6d\t", ...) and "%6s\t"). Now
# exercised below -- see notes/iterations/CAT-01.md section 7 and
# notes/iterations/FORMAT-01.md for the finding and fix.

# 1. Basic single-file concatenation, byte-exact
check_case 'echo hello > /tmp/a; cat /tmp/a' \
    0 "hello\n" "" "basic single-file concatenation"

# 2. Multiple files concatenated in argument order
check_case 'echo one > /tmp/a; echo two > /tmp/b; cat /tmp/a /tmp/b' \
    0 "one\ntwo\n" "" "multi-file concatenation in argument order"

# 3. Stdin operand "-"
check_case 'echo piped | cat -' \
    0 "piped\n" "" "stdin operand -"

# 4. -n numbers every output line
check_case 'echo a > /tmp/n; echo b >> /tmp/n; cat -n /tmp/n' \
    0 "     1\ta\n     2\tb\n" "" "-n numbers every line"

# 5. -b numbers only non-blank lines, leaving blank lines unnumbered
check_case 'echo a > /tmp/nb; echo >> /tmp/nb; echo b >> /tmp/nb; cat -b /tmp/nb' \
    0 "     1\ta\n\n     2\tb\n" "" "-b numbers only non-blank lines"

# 6. -s squeezes multiple adjacent blank lines into one
check_case 'echo a > /tmp/sq; echo >> /tmp/sq; echo >> /tmp/sq; echo >> /tmp/sq; echo b >> /tmp/sq; cat -s /tmp/sq' \
    0 "a\n\nb\n" "" "-s squeezes adjacent blank lines"

# 7. -e marks end of line with $
check_case 'echo a > /tmp/e; echo b >> /tmp/e; cat -e /tmp/e' \
    0 "a\$\nb\$\n" "" "-e appends end-of-line marker"

# 8. -B bsize accepts a custom buffer size and still concatenates correctly
check_case 'echo bsz > /tmp/bs; cat -B 4096 /tmp/bs' \
    0 "bsz\n" "" "-B custom buffer size"

# 9. -u unbuffered output still produces correct content
check_case 'echo unb > /tmp/u; cat -u /tmp/u' \
    0 "unb\n" "" "-u unbuffered execution"

# 10. -f regular-file gate passes an actual regular file through untouched
check_case 'echo regular > /tmp/reg; cat -f /tmp/reg' \
    0 "regular\n" "" "-f regular file gate passes a real regular file"

# 11. Missing file: diagnostic, exit 1, and processing continues to later operands
check_case 'echo ok > /tmp/g; cat /tmp/g /tmp/gone' \
    1 "ok\n" "cat: /tmp/gone: no such file or directory\n" \
    "missing file reports and continues"

# 12. -l is outside the accepted matrix: FCNTL-01's honest ENOSYS on lock
#     commands makes cat -l fail loudly rather than fake an exclusive lock.
check_case 'echo x > /tmp/lf; cat -l /tmp/lf' \
    1 "" "cat: stdout: function not implemented\n" \
    "-l outside accepted matrix fails honestly"

echo 'cat behavioral matrix passed'

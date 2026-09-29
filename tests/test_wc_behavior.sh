#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
program=${PROGRAM_PATH:-$(make -s print-program)}

case_dir=$(mktemp -d "${TMPDIR:-/tmp}/cannedbsd-wc.XXXXXX")
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

# 1. wc -c on file (exercising regular file fstat fast path)
check_case \
    'echo -n "hello world" > /tmp/wc1; wc -c /tmp/wc1' \
    0 \
    "      11 /tmp/wc1\n" \
    "" \
    "wc -c file byte count via fstat"

# 2. wc -l on file
check_case \
    'echo "line1" > /tmp/wc2; echo "line2" >> /tmp/wc2; echo "line3" >> /tmp/wc2; wc -l /tmp/wc2' \
    0 \
    "       3 /tmp/wc2\n" \
    "" \
    "wc -l file line count"

# 3. wc -w on file
check_case \
    'echo "the quick brown fox" > /tmp/wc3; echo "jumps over lazy dog" >> /tmp/wc3; wc -w /tmp/wc3' \
    0 \
    "       8 /tmp/wc3\n" \
    "" \
    "wc -w file word count"

# 4. wc default (no flags: lines, words, bytes) on file
check_case \
    'echo "hello world" > /tmp/wc4; wc /tmp/wc4' \
    0 \
    "       1       2      12 /tmp/wc4\n" \
    "" \
    "wc default lines words bytes on file"

# 5. wc -l from pipeline (stdin)
check_case \
    'echo "alpha" > /tmp/wc5; echo "beta" >> /tmp/wc5; cat /tmp/wc5 | wc -l' \
    0 \
    "       2\n" \
    "" \
    "wc -l pipeline from cat"

# 6. wc -w from pipeline (stdin)
check_case \
    'echo "one two three four five" | wc -w' \
    0 \
    "       5\n" \
    "" \
    "wc -w pipeline from echo"

# 7. wc -c from pipeline (stdin, exercising non-stat read loop path)
check_case \
    'echo -n "cannedbsd" | wc -c' \
    0 \
    "       9\n" \
    "" \
    "wc -c pipeline byte count"

# 8. wc multiple files with total line
check_case \
    'echo "a b" > /tmp/f1; echo "c d e" > /tmp/f2; wc -w /tmp/f1 /tmp/f2' \
    0 \
    "       2 /tmp/f1\n       3 /tmp/f2\n       5 total\n" \
    "" \
    "wc -w multiple files with total"

# 9. wc -L longest line length
check_case \
    'echo "short" > /tmp/wcL; echo "much longer line" >> /tmp/wcL; echo "tiny" >> /tmp/wcL; wc -L /tmp/wcL' \
    0 \
    "      16 /tmp/wcL\n" \
    "" \
    "wc -L longest line length"

# 10. wc missing file error handling (continues with exit 1)
check_case \
    'echo "exists" > /tmp/wc_ok; wc -l /tmp/wc_missing /tmp/wc_ok; echo $?' \
    0 \
    "       1 /tmp/wc_ok\n       1 total\n1\n" \
    "wc: /tmp/wc_missing: no such file or directory\n" \
    "wc missing file error reporting"

# 11. wc invalid option usage diagnostic
check_case \
    'wc -z; echo $?' \
    0 \
    "1\n" \
    "wc: illegal option -- z\nusage: wc [-c | -m] [-Llw] [file ...]\n" \
    "wc invalid option usage diagnostic"

# 12. wc empty file
check_case \
    'echo -n "" > /tmp/wc_empty; wc /tmp/wc_empty' \
    0 \
    "       0       0       0 /tmp/wc_empty\n" \
    "" \
    "wc on empty file"

echo 'wc behavioral matrix passed'

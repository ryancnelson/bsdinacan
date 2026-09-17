#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
program=${PROGRAM_PATH:-$(make -s print-program)}

case_dir=$(mktemp -d "${TMPDIR:-/tmp}/cannedbsd-mkdir.XXXXXX")
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

# 1. Basic single directory creation and verification via ls
check_case \
    'mkdir /tmp/d1; ls /tmp' \
    0 \
    "d1\n" \
    "" \
    "single directory creation"

# 2. Multiple directory operands
check_case \
    'mkdir /tmp/alpha /tmp/beta; ls /tmp' \
    0 \
    "beta\nalpha\n" \
    "" \
    "multiple directory operands"

# 3. Existing directory error handling (EEXIST)
check_case \
    'mkdir /tmp/d1; mkdir /tmp/d1; echo $?' \
    0 \
    "1\n" \
    "mkdir: /tmp/d1: file exists\n" \
    "existing directory creation failure"

# 4. Nested directory creation inside an existing parent
check_case \
    'mkdir /tmp/parent; mkdir /tmp/parent/sub; ls /tmp/parent' \
    0 \
    "sub\n" \
    "" \
    "nested directory creation inside parent"

# 5. Missing parent failure without -p
check_case \
    'mkdir /tmp/missing/child; echo $?' \
    0 \
    "1\n" \
    "mkdir: /tmp/missing/child: no such file or directory\n" \
    "missing parent without -p fails with ENOENT"

# 6. mkdir -p creates full directory hierarchy
check_case \
    'mkdir -p /tmp/level1/level2/level3; ls /tmp; ls /tmp/level1; ls /tmp/level1/level2' \
    0 \
    "level1\nlevel2\nlevel3\n" \
    "" \
    "mkdir -p intermediate component creation"

# 7. mkdir -p idempotency on existing directory
check_case \
    'mkdir /tmp/existing; mkdir -p /tmp/existing; echo $?' \
    0 \
    "0\n" \
    "" \
    "mkdir -p idempotent on existing directory"

# 8. Missing operand usage diagnostic
check_case \
    'mkdir' \
    1 \
    "" \
    "usage: mkdir [-p] [-m mode] dirname ...\n" \
    "missing operand usage diagnostic"

# 9. mkdir -m honest unsupported diagnostic
check_case \
    'mkdir -m 0755 /tmp/dmode; echo $?' \
    0 \
    "1\n" \
    "mkdir: Cannot set file mode \`0755': invalid argument\n" \
    "mkdir -m unsupported file mode error"

# 10. File creation, move, and inspection inside newly created directory
check_case \
    'mkdir /tmp/myfolder; echo content > /tmp/myfolder/item.txt; cat /tmp/myfolder/item.txt; ls /tmp/myfolder' \
    0 \
    "content\nitem.txt\n" \
    "" \
    "file creation and listing inside new directory"

echo 'mkdir behavioral matrix passed'

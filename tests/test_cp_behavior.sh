#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
program=${PROGRAM_PATH:-$(make -s print-program)}

case_dir=$(mktemp -d "${TMPDIR:-/tmp}/cannedbsd-cp.XXXXXX")
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

# 1. Basic copy of a single file
check_case 'echo hello > /tmp/a; cp /tmp/a /tmp/b; cat /tmp/b; cat /tmp/a' \
    0 "hello\nhello\n" "" "single file copy preserves source and writes destination"

# 2. Target overwrite replaces existing destination
check_case 'echo one > /tmp/a; echo two > /tmp/b; cp /tmp/a /tmp/b; cat /tmp/b' \
    0 "one\n" "" "target overwrite replaces destination contents"

# 3. Verbose flag -v output
check_case 'echo test > /tmp/src; cp -v /tmp/src /tmp/dst; cat /tmp/dst' \
    0 "/tmp/src -> /tmp/dst\ntest\n" "" "verbose flag -v output"

# 4. Multi-file copy into existing directory (/home/user)
check_case 'echo f1 > /tmp/f1; echo f2 > /tmp/f2; cp /tmp/f1 /tmp/f2 /home/user; cat /home/user/f1; cat /home/user/f2' \
    0 "f1\nf2\n" "" "multi-file copy into existing directory"

# 5. Recursive directory copy (-r)
check_case 'echo nested > /home/user/f; cp -r /home/user /tmp/d; cat /tmp/d/f' \
    0 "nested\n" "" "recursive directory copy -r"

# 6. Recursive directory copy (-R)
check_case 'echo nested > /home/user/f; cp -R /home/user /tmp/d2; cat /tmp/d2/f' \
    0 "nested\n" "" "recursive directory copy -R"

# 7. Force overwrite flag (-f)
check_case 'echo initial > /tmp/dst; echo updated > /tmp/src; cp -f /tmp/src /tmp/dst; cat /tmp/dst' \
    0 "updated\n" "" "force flag -f overwrites destination"

# 8. Missing source operand
check_case 'cp /tmp/nonexistent /tmp/dst' \
    1 "" "cp: /tmp/nonexistent: no such file or directory\n" \
    "missing source file error diagnostic"

# 9. Insufficient arguments (usage)
check_case 'cp /tmp/onlyone' \
    1 "" "usage: cp [-R [-H | -L | -P]] [-f | -i] [-alNpv] src target\n       cp [-R [-H | -L | -P]] [-f | -i] [-alNpv] src1 ... srcN directory\n" \
    "too few arguments usage diagnostic"

# 10. Multi-source to non-directory target
check_case 'echo a > /tmp/a; echo b > /tmp/b; cp /tmp/a /tmp/b /tmp/c' \
    1 "" "usage: cp [-R [-H | -L | -P]] [-f | -i] [-alNpv] src target\n       cp [-R [-H | -L | -P]] [-f | -i] [-alNpv] src1 ... srcN directory\n" \
    "multi-source with non-directory target usage diagnostic"

# 11. Identical source and destination
check_case 'echo a > /tmp/a; cp /tmp/a /tmp/a' \
    1 "" "cp: /tmp/a and /tmp/a are identical (not copied).\n" \
    "identical source and destination diagnostic"

# 12. Directory source without recursive flag
check_case 'cp /home/user /tmp/dst' \
    1 "" "cp: /home/user is a directory (not copied).\n" \
    "directory without -r flag diagnostic"

echo "cp behavioral matrix passed"

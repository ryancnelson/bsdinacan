#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
program=${PROGRAM_PATH:-$(make -s print-program)}

case_dir=$(mktemp -d "${TMPDIR:-/tmp}/cannedbsd-mv.XXXXXX")
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

# 1. Basic same-mount rename of a single file
check_case 'echo hello > /tmp/a; mv /tmp/a /tmp/b; cat /tmp/b; ls /tmp' \
    0 "hello\nb\n" "" "same-mount file rename"

# 2. Target overwrite
check_case 'echo one > /tmp/a; echo two > /tmp/b; mv /tmp/a /tmp/b; cat /tmp/b; ls /tmp' \
    0 "one\nb\n" "" "target overwrite replaces existing destination"

# 3. Verbose flag -v
check_case 'echo test > /tmp/src; mv -v /tmp/src /tmp/dst; cat /tmp/dst' \
    0 "/tmp/src -> /tmp/dst\ntest\n" "" "verbose flag -v output"

# 4. Multi-file move into existing directory (/home/user), leaving /tmp
# empty -- one combined `ls` call with both directories as operands (real
# ls sorts by name and headers each operand) rather than two separate `ls`
# invocations in the same script: pinned ls.c's own file-scope `static int
# output` (its "have I already printed something" flag) is never reset
# between invocations sharing this runtime's single process image, so two
# back-to-back `ls` calls in one script do not behave like two independent
# processes the way real BSD assumes -- see notes/iterations/LS-02.md.
check_case 'echo f1 > /tmp/f1; echo f2 > /tmp/f2; mv /tmp/f1 /tmp/f2 /home/user; ls /home/user /tmp' \
    0 "/home/user:\nf1 f2\n\n/tmp:\n" "" "multi-file move into existing directory"

# 5. Directory rename (same mount)
check_case 'echo nested > /home/user/content; mv /home/user /home/renamed; cat /home/renamed/content; ls /home' \
    0 "nested\nrenamed\n" "" "directory rename"

# 6. Missing source operand
check_case 'mv /tmp/nonexistent /tmp/dst' \
    1 "" "mv: rename /tmp/nonexistent to /tmp/dst: no such file or directory\n" \
    "missing source file error diagnostic"

# 7. Insufficient arguments (usage)
check_case 'mv /tmp/onlyone' \
    1 "" "usage: mv [-fhiv] source target\n       mv [-fiv] source ... directory\n" \
    "too few arguments usage diagnostic"

# 8. Extra arguments with non-directory destination
check_case 'echo a > /tmp/a; echo b > /tmp/b; echo c > /tmp/c; mv /tmp/a /tmp/b /tmp/c' \
    1 "" "usage: mv [-fhiv] source target\n       mv [-fiv] source ... directory\n" \
    "multiple sources with non-directory target usage diagnostic"

echo 'mv behavioral matrix passed'

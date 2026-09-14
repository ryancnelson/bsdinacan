#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
program=${PROGRAM_PATH:-$(make -s print-program)}

case_dir=$(mktemp -d "${TMPDIR:-/tmp}/cannedbsd-ls.XXXXXX")
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

# Each check_case is a fresh boot (a new `$program -c` process), so /tmp
# starts empty every time; fixtures are built inline in the same command.

check_case 'ls /tmp' 0 "" "" "empty directory"

check_case 'echo hi > /tmp/onlyfile; ls /tmp' 0 "onlyfile\n" "" "single entry"

# RAMFS's child list is prepend-ordered (most recently created first), not
# alphabetical -- that order is what "stable deterministic ordering" means
# here: this command does not sort. Run the listing twice back to back to
# prove the SAME order comes back both times, not just that some order
# exists once.
check_case 'echo a > /tmp/a; echo b > /tmp/b; echo c > /tmp/c; ls /tmp; ls /tmp' \
    0 "c\nb\na\nc\nb\na\n" "" "many entries, stable ordering across repeated listings"

check_case 'echo hi > /tmp/plainfile; ls /tmp/plainfile' 0 "/tmp/plainfile\n" "" \
    "named file operand"

# A pre-existing non-directory operand of a DIFFERENT node type (an
# executable command node under /bin, not a regular file) -- proves the
# ENOTDIR fallback isn't special-cased to CB_NODE_REGULAR alone.
check_case 'ls /bin/wc' 0 "/bin/wc\n" "" "non-directory operand (executable node)"

check_case 'ls /tmp/nonexistent' 1 "" \
    "ls: /tmp/nonexistent: no such file or directory\n" \
    "missing operand: ENOENT diagnostic and exit status"

check_case 'ls /tmp /bin' 1 "" "usage: ls [file]\n" "too many operands"

check_case 'ls /tmp | cat' 0 "" "" "pipeline on an empty directory"

# No operand defaults to ".", which resolves to the boot-time root; its
# three children in RAMFS's own deterministic prepend order. Documents the
# default-path behavior, not a claim about shell cwd tracking generally.
check_case 'ls' 0 "home\ntmp\nbin\n" "" "no operand defaults to the current directory"

echo 'ls behavioral matrix passed'

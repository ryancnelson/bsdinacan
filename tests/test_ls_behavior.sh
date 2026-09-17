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

run_case() {
    local cmd="$1"
    ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}log_path=$case_dir/asan" \
        "$program" -c "$cmd" > "$case_dir/out" 2> "$case_dir/err"
    status=$?
    check_sanitizer_reports
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
    run_case "$cmd"
    set -e

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

# LS-02 replaced LS-01's cannedBSD-owned single-column stand-in with the
# pinned, unchanged NetBSD ls.c/print.c/cmp.c/util.c -- every case below
# documents the pinned source's REAL behavior (verified by running it, not
# assumed from the man page), not the old owned command's. Each check_case
# is a fresh boot (a new `$program -c` process), so /tmp starts empty every
# time; fixtures are built inline in the same command.

check_case 'ls -1 /tmp' 0 "" "" "empty directory, one-per-line"

check_case 'echo hi > /tmp/onlyfile; ls -1 /tmp' 0 "onlyfile\n" "" "single entry"

# Real ls sorts by name by default -- created out of alphabetical order
# (zeta, alpha, mid) to prove this is a real sort, not creation order
# passed through unchanged (LS-01's own stand-in never sorted at all).
check_case 'echo 1 > /tmp/zeta; echo 2 > /tmp/alpha; echo 3 > /tmp/mid; ls -1 /tmp' \
    0 "alpha\nmid\nzeta\n" "" "default listing is sorted by name"

check_case 'echo 1 > /tmp/zeta; echo 2 > /tmp/alpha; echo 3 > /tmp/mid; ls -1r /tmp' \
    0 "zeta\nmid\nalpha\n" "" "-r reverses the default sort"

# -f disables sorting (real RAMFS prepend/creation order) and synthesizes
# "." and ".." first, via FTS_SEEDOT -- the fts_children()/FTS-CHILDREN-01
# path, not the sorted fts_read() default path above.
check_case 'echo 1 > /tmp/zeta; echo 2 > /tmp/alpha; echo 3 > /tmp/mid; ls -f1 /tmp' \
    0 ".\n..\nmid\nalpha\nzeta\n" "" "-f disables sorting and shows dot entries"

# Real ls prints the fts_name (basename), not the operand path, in its
# ENOENT diagnostic -- ls.c's own display() loop reads cur->fts_name
# directly (see notes/iterations/LS-02.md).
check_case 'ls /tmp/nonexistent' 1 "" \
    "ls: nonexistent: no such file or directory\n" \
    "missing operand: ENOENT diagnostic and exit status"

# A non-directory operand of a DIFFERENT node type (an executable command
# node under /bin, not a regular file) prints just the operand as given,
# same as a real file -- proves the ENOTDIR fallback isn't special-cased
# to CB_NODE_REGULAR alone.
check_case 'ls /bin/wc' 0 "wc\n" "" "non-directory operand (executable node)"

# Real ls accepts multiple operands (LS-01's stand-in rejected this as a
# usage error); more than one directory operand gets a "name:" header per
# directory, blank-line separated, no header before the first section only
# when there is just one -- here there are two, so both get headers.
check_case 'ls /tmp /home' 0 "/tmp:\n\n/home:\nuser\n" "" \
    "multiple directory operands get per-directory headers"

check_case 'ls -1 /tmp | cat' 0 "" "" "pipeline on an empty directory"

# No operand defaults to ".", which resolves to the boot-time root; its
# three children in real sorted order (bin, home, tmp), not RAMFS's raw
# creation order (see the -f case above for that).
check_case 'ls -1' 0 "bin\nhome\ntmp\n" "" "no operand defaults to the current directory"

# ls -t sorts by FS-STAT-01's real per-file mtime, newest first -- two
# writes issued back to back in the same command land within the same
# millisecond (measured: stable across repeated runs, not assumed), so
# this exercises cmp.c's own documented tie-break (ascending name) rather
# than a strict ordering that would need real elapsed time this fast
# in-process environment cannot reliably produce between two statements.
check_case 'echo 1 > /tmp/a; echo 2 > /tmp/b; ls -t1 /tmp' 0 "a\nb\n" "" \
    "-t on a real mtime tie falls back to ascending name order"

# -l's mode/nlink/uid/gid/size columns are all real, deterministic values
# (RAMFS's actual per-node mode bits from the creating open() call, the
# fixed uid=gid=0 FS-STAT-01 already committed to, and the real byte size)
# -- only the date column is inherently non-deterministic (real wall-clock
# time), so this checks everything except that column via a pattern
# rather than check_case's exact byte comparison.
run_case 'echo hi > /tmp/onlyfile; ls -l /tmp'
if [ "$status" -ne 0 ]; then
    fail "-l: expected status 0, got $status"
fi
if [ "$(head -n1 "$case_dir/out")" != "total 0" ]; then
    fail "-l: missing 'total 0' header"
fi
if ! sed -n '2p' "$case_dir/out" | grep -Eq \
    '^-rw-rw-rw-  1 0  0  3 [A-Za-z]{3} [ 0-9][0-9] [0-9:]+ onlyfile$'; then
    echo "-l line was:" >&2
    sed -n '2p' "$case_dir/out" >&2
    fail "-l: entry line does not match the expected mode/nlink/uid/gid/size/name shape"
fi

# -h (humanize_number) is honestly unimplemented (ENOSYS), documented
# outside the accepted matrix in UPSTREAM.md -- only reachable when paired
# with -l/-s, matching real ls's own semantics (bare -h with no sizing
# context does nothing, exercised implicitly by every other case above
# never passing -h).
check_case 'echo hi > /tmp/onlyfile; ls -lh /tmp' 1 "" \
    "ls: humanize_number: function not implemented\n" \
    "-h (humanize_number) fails honestly, outside the accepted matrix"

echo 'ls behavioral matrix passed'

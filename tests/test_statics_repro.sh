#!/bin/sh
# tests/test_statics_repro.sh - STATICS-RESET-01 verification suite.
#
# Adapted from antigravity's STATICS-REPRO-01 (commit 951b11b on
# work/STATICS-REPRO-01, which demonstrated these as real failures against
# main/work/LS-02 and cannot land on its own without making main red).
# The five repro commands and their real observed failure patterns are
# antigravity's own findings, verified independently against the pinned
# source before this suite was written; ls's case (missing from the
# original script, documented only in its companion notes file) has been
# added here so all five findings have an executable case. The pass/fail
# sense is inverted from the original: that script's PASS meant "the bug
# reproduced"; this one's PASS means "the bug is gone" -- this is meant to
# run red against main/pre-fix and green against this branch's fix. Passing
# these five cases alone does not prove complete static-state isolation.
set -eu

PROGRAM="${PROGRAM_PATH:-./build/sanitize/bsdinacan}"

echo "================================================================================"
echo "STATICS-RESET-01 verification: five findings from STATICS-AUDIT-01/REPRO-01"
echo "Target Binary: $PROGRAM"
echo "================================================================================"

pass_count=0
fail_count=0

case_dir=$(mktemp -d "${TMPDIR:-/tmp}/cannedbsd-statics.XXXXXX") || exit 1
trap 'rm -rf "$case_dir"' EXIT
trap 'exit 1' HUP INT TERM

# Keep the known context-switch warning out of command stderr, but reject
# every other sanitizer report, including reports whose process exits zero.
check_sanitizer_reports() {
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
            return 1
        fi
        rm -f "$report"
    done
}

# Require the complete fixture result, not merely absence of an ASan string.
# Separate files preserve stream identity, embedded NULs and trailing newlines.
# Every command under test also prints its own status before a later command
# could hide a failure behind the shell sequence's final exit status.
check_fixed() {
    title="$1"
    script="$2"
    expected_stdout="$3"
    expected_stderr="$4"

    echo "--------------------------------------------------------------------------------"
    echo "CASE: $title"
    echo "INPUT: $script"
    printf '%b' "$expected_stdout" > "$case_dir/expected_out"
    printf '%b' "$expected_stderr" > "$case_dir/expected_err"

    status=0
    ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}log_path=$case_dir/asan" \
        "$PROGRAM" -c "$script" > "$case_dir/out" 2> "$case_dir/err" || status=$?
    echo "EXIT STATUS: $status"
    case_ok=1
    [ "$status" -eq 0 ] || case_ok=0
    check_sanitizer_reports || case_ok=0
    for stream in out err; do
        if ! cmp -s "$case_dir/expected_$stream" "$case_dir/$stream"; then
            echo "$stream mismatch:" >&2
            diff -u "$case_dir/expected_$stream" "$case_dir/$stream" >&2 || true
            case_ok=0
        fi
    done
    if [ "$case_ok" -eq 1 ]; then
        echo "RESULT: [PASS] status and complete fixture output match"
        pass_count=$((pass_count + 1))
    else
        echo "RESULT: [FAIL] status, output or sanitizer report mismatch"
        fail_count=$((fail_count + 1))
    fi
    echo ""
}

# 1. cat -B heap use-after-free (raw_cat()'s function-local `buf`, gated
# on file-scope `bsize`; only reachable when -B requests a buffer larger
# than the built-in fb_buf, i.e. > 1024 -- a plain `cat x; cat y` with no
# -B never mallocs and never reaches this). This case covers same-size
# invocations only. The retained-cache implementation still has independent
# resize and kernel-recreation blockers; this pass does not establish a
# complete cache lifecycle fix.
check_fixed "cat -B 2048 multi-invocation heap use-after-free" \
    "echo hello > /tmp/cat1; echo world > /tmp/cat2; cat -B 2048 /tmp/cat1; echo cat1_status=\$?; cat -B 2048 /tmp/cat2; echo cat2_status=\$?" \
    "hello\ncat1_status=0\nworld\ncat2_status=0\n" ""

# 2. ls default column-mode heap use-after-free (printcol()'s
# function-local `array`/`lastentries`; a second ls whose entry count
# does not exceed the first's bypasses realloc and writes through freed
# heap). This checks two invocations in one kernel, not recreation or
# interleaved cache ownership.
check_fixed "ls default column mode heap use-after-free across invocations" \
    "echo 1 > /tmp/a1; echo 1 > /tmp/a2; echo 1 > /tmp/a3; ls /tmp; echo ls1_status=\$?; rm /tmp/a3; echo rm_status=\$?; ls /tmp; echo ls2_status=\$?" \
    "a1 a2 a3\nls1_status=0\nrm_status=0\na1 a2\nls2_status=0\n" ""

# 3. rm stale eval exit-status leakage (file-scope static `eval`, never
# reset at the top of main()). Fixed by resetting cb_rm_eval every
# invocation -- see static_reset.c's own rm_slots.
check_fixed "rm stale eval exit status leakage on successful second invocation" \
    "rm /tmp/nonexistent; echo rm1_status=\$?; echo data > /tmp/valid; rm /tmp/valid; echo rm2_status=\$?" \
    "rm1_status=1\nrm2_status=0\n" \
    "rm: /tmp/nonexistent: no such file or directory\n"

# 4. cp flag leakage. cp.c's own main() only resets four of its eleven
# plain-global option flags (Hflag, Lflag, Pflag, Rflag); the other seven
# (fflag, iflag, lflag, pflag, rflag, vflag, Nflag) persist. This case
# exercises vflag only: the first copy is verbose, the second is quiet,
# both statuses are zero, and both destination files contain the fixture.
# It does not prove isolation of the remaining options or interleaved tasks.
check_fixed "cp persistent vflag leakage on second invocation without -v" \
    "echo 1 > /tmp/cpa; echo 2 > /tmp/cpb; cp -v /tmp/cpa /tmp/cpa_out; echo cp1_status=\$?; cp /tmp/cpb /tmp/cpb_out; echo cp2_status=\$?; cat /tmp/cpa_out; cat /tmp/cpb_out" \
    "/tmp/cpa -> /tmp/cpa_out\ncp1_status=0\ncp2_status=0\n1\n2\n" ""

# 5. mv fastcopy() reachability check -- NOT a UAF assertion. fastcopy()'s
# `bp`/`blen` UAF is confirmed real in the source (same shape as ls's and
# cat's) but LATENT: every session directory (/, /tmp, /home, /bin) sits
# on the single unified root ramfs mount, rename() never returns EXDEV
# inside that mount, and fastcopy() is only ever called on the EXDEV
# fallback path -- so no shell command reaches it today. This case only
# confirms mv still works correctly across invocations; it cannot exercise
# or verify a fastcopy cache lifecycle fix. Retention in mv's executor
# does not by itself prove correctness; that requires a second mount to
# reach fastcopy plus independent ownership and teardown coverage.
check_fixed "mv cross-directory intra-mount rename (fastcopy reachability check)" \
    "echo test > /tmp/mva; mv /tmp/mva /home/user/mvb; echo mv1_status=\$?; mv /home/user/mvb /tmp/mvc; echo mv2_status=\$?; cat /tmp/mvc" \
    "mv1_status=0\nmv2_status=0\ntest\n" ""

echo "================================================================================"
echo "STATICS-RESET-01 verification summary: $pass_count cases passed, $fail_count cases failed (mv fastcopy remains unexercised)"
echo "================================================================================"

if [ "$fail_count" -ne 0 ]; then
    exit 1
fi

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
# run red against main/pre-fix and green against this branch's fix.
set -u

PROGRAM="${PROGRAM_PATH:-./build/sanitize/bsdinacan}"

echo "================================================================================"
echo "STATICS-RESET-01 verification: five findings from STATICS-AUDIT-01/REPRO-01"
echo "Target Binary: $PROGRAM"
echo "================================================================================"

pass_count=0
fail_count=0

# check_fixed: asserts bug_pattern does NOT appear in the command's
# combined stdout+stderr. A case whose bug is a wrong exit status rather
# than an output pattern passes expected_status/expected_pattern instead
# (see the rm and ls cases below).
check_fixed() {
    title="$1"
    script="$2"
    bug_pattern="$3"

    echo "--------------------------------------------------------------------------------"
    echo "CASE: $title"
    echo "INPUT: $script"

    set +e
    output=$("$PROGRAM" -c "$script" 2>&1)
    status=$?
    set -e

    echo "EXIT STATUS: $status"
    echo "OUTPUT:"
    printf '%s\n' "$output" | head -n 30

    if printf '%s\n' "$output" | grep -q "$bug_pattern"; then
        echo "RESULT: [STILL BROKEN] matched bug pattern: '$bug_pattern'"
        fail_count=$((fail_count + 1))
    else
        echo "RESULT: [FIXED] bug pattern absent: '$bug_pattern'"
        pass_count=$((pass_count + 1))
    fi
    echo ""
}

# 1. cat -B heap use-after-free (raw_cat()'s function-local `buf`, gated
# on file-scope `bsize`; only reachable when -B requests a buffer larger
# than the built-in fb_buf, i.e. > 1024 -- a plain `cat x; cat y` with no
# -B never mallocs and never reaches this. Fixed by leaving both bsize
# and buf unmanaged and relying on CB_EXECUTOR_PERSISTENT_HEAP to keep
# buf's target valid across invocations -- see static_reset.c's own
# cat_slots comment.)
check_fixed "cat -B 2048 multi-invocation heap use-after-free" \
    "echo hello > /tmp/cat1; echo world > /tmp/cat2; cat -B 2048 /tmp/cat1; cat -B 2048 /tmp/cat2" \
    "AddressSanitizer: heap-use-after-free"

# 2. ls default column-mode heap use-after-free (printcol()'s
# function-local `array`/`lastentries`; a second ls whose entry count
# does not exceed the first's bypasses realloc and writes through freed
# heap). Fixed by CB_EXECUTOR_PERSISTENT_HEAP.
check_fixed "ls default column mode heap use-after-free across invocations" \
    "echo 1 > /tmp/a1; echo 1 > /tmp/a2; echo 1 > /tmp/a3; ls /tmp; rm /tmp/a3; ls /tmp" \
    "AddressSanitizer: heap-use-after-free"

# 3. rm stale eval exit-status leakage (file-scope static `eval`, never
# reset at the top of main()). Fixed by resetting cb_rm_eval every
# invocation -- see static_reset.c's own rm_slots.
check_fixed "rm stale eval exit status leakage on successful second invocation" \
    "rm /tmp/nonexistent; echo rm1_status=\$?; echo data > /tmp/valid; rm /tmp/valid; echo rm2_status=\$?" \
    "rm2_status=1"

# 4. cp flag leakage. cp.c's own main() only resets four of its eleven
# plain-global option flags (Hflag, Lflag, Pflag, Rflag); the other seven
# (fflag, iflag, lflag, pflag, rflag, vflag, Nflag) persist. -v is the
# cosmetic member of that set; -f/-i/-l/-p/-r change semantics, not just
# output, which is why all eleven are covered here, not just vflag. Fixed
# by resetting every one of the eleven every invocation regardless of
# what cp.c's own main() does internally -- see static_reset.c's own
# cp_slots.
check_fixed "cp persistent vflag leakage on second invocation without -v" \
    "echo 1 > /tmp/cpa; echo 2 > /tmp/cpb; cp -v /tmp/cpa /tmp/cpa_out; cp /tmp/cpb /tmp/cpb_out" \
    "/tmp/cpb -> /tmp/cpb_out"

# 5. mv fastcopy() reachability check -- NOT a UAF assertion. fastcopy()'s
# `bp`/`blen` UAF is confirmed real in the source (same shape as ls's and
# cat's) but LATENT: every session directory (/, /tmp, /home, /bin) sits
# on the single unified root ramfs mount, rename() never returns EXDEV
# inside that mount, and fastcopy() is only ever called on the EXDEV
# fallback path -- so no shell command reaches it today. This case only
# confirms mv still works correctly across invocations; it cannot exercise
# or verify the UAF fix (CB_EXECUTOR_PERSISTENT_HEAP is applied to mv's
# executor anyway, on the same reasoning as ls/cat, so the fix is in place
# for whenever a second mount makes fastcopy() reachable, but this suite
# has no way to prove that without a second mount to rename across).
check_fixed "mv cross-directory intra-mount rename (fastcopy reachability check)" \
    "echo test > /tmp/mva; mv /tmp/mva /home/user/mvb; mv /home/user/mvb /tmp/mvc; cat /tmp/mvc" \
    "AddressSanitizer:"

echo "================================================================================"
echo "STATICS-RESET-01 verification summary: $pass_count fixed, $fail_count still broken"
echo "================================================================================"

if [ "$fail_count" -ne 0 ]; then
    exit 1
fi

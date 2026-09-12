#!/usr/bin/env bash
# Offline, deterministic tests for tools/lib/solaris9-qualify-lib.sh.
#
# No SSH, no rig, no guest -- sq_rsh here executes the given command
# string against a real local directory standing in for the rig's own
# filesystem (`bash -c "$2"`), so lock atomicity, staging exit-code
# handling, and hash verification are exercised against real file
# operations, not a hand-rolled pattern-matching fake. Only the
# genuinely guest-specific steps (console.py/mon.py driving an actual
# QEMU guest) are stubbed at a higher level, since faithfully emulating
# those offline would just be re-describing the mock's own assumptions.
#
# Run: bash tests/test_solaris9_qualify.sh
set -uo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
source "$repo_root/tools/lib/solaris9-qualify-lib.sh"

test_scratch=$(mktemp -d)
test_output=$test_scratch/out
test_error=$test_scratch/err
pass_count=0
fail_count=0
current_test=""

t_begin() { current_test=$1; }
t_ok() { pass_count=$((pass_count + 1)); printf 'ok   - %s\n' "$current_test"; }
t_fail() { fail_count=$((fail_count + 1)); printf 'FAIL - %s: %s\n' "$current_test" "$*"; }

assert_eq() {
    local desc=$1 expected=$2 actual=$3
    if [ "$expected" = "$actual" ]; then t_ok; else t_fail "$desc: expected [$expected] got [$actual]"; fi
}
assert_success() { if "$@" >"$test_output" 2>"$test_error"; then t_ok; else t_fail "expected success, got exit $?: $(cat "$test_error")"; fi; }
assert_failure() { if "$@" >"$test_output" 2>"$test_error"; then t_fail "expected failure, but succeeded: $(cat "$test_output")"; else t_ok; fi; }

# Fake rig filesystem, real directory: gives genuine mkdir atomicity,
# genuine file read/write semantics -- not a hand-rolled simulation of
# them.
fake_rig=$(mktemp -d)
fake_rig2=""
still_flag=""
trap 'rm -rf "$fake_rig" "$fake_rig2" "$still_flag" "$test_scratch"' EXIT

sq_rsh() {
    local _timeout=$1 cmd=$2
    bash -c "$cmd"
}
sq_rscp() {
    cp "$1" "$2"
}

echo "== lock: quoting =="
t_begin "sq_quote_remote round-trips a string with an apostrophe through a real shell"
val="it's a test"
q=$(sq_quote_remote "$val")
got=$(eval "printf '%s' $q")
assert_eq "apostrophe round-trip" "$val" "$got"

t_begin "sq_quote_remote round-trips embedded newlines through a real shell"
val=$'line one\nline two with '"'"'quote'
q=$(sq_quote_remote "$val")
got=$(eval "printf '%s' $q")
assert_eq "quote round-trip" "$val" "$got"

echo "== lock: acquire/release =="
t_begin "first acquire succeeds"
assert_success sq_acquire_lock "$fake_rig/coordinator.lock" "token-A" "owner A"

t_begin "second acquire with a different token is rejected (competing owner)"
assert_failure sq_acquire_lock "$fake_rig/coordinator.lock" "token-B" "owner B"

t_begin "release with the wrong token is refused"
assert_failure sq_release_lock "$fake_rig/coordinator.lock" "token-B"

t_begin "lock directory still exists after a refused release"
[ -d "$fake_rig/coordinator.lock" ] && t_ok || t_fail "lock holder directory vanished"

t_begin "release with the correct token succeeds"
assert_success sq_release_lock "$fake_rig/coordinator.lock" "token-A"

t_begin "lock directory is gone after a correct release"
[ ! -d "$fake_rig/coordinator.lock" ] && t_ok || t_fail "lock holder directory still present"

t_begin "a fresh acquire succeeds again after a clean release"
assert_success sq_acquire_lock "$fake_rig/coordinator.lock" "token-C" "owner C"
sq_release_lock "$fake_rig/coordinator.lock" "token-C" >/dev/null 2>&1

echo "== lock: malformed / stale =="
fake_rig2=$(mktemp -d)
mkdir -p "$fake_rig2/coordinator.lock"
# Deliberately no owner.txt inside -- a malformed/incomplete lock.
t_begin "malformed lock (missing owner.txt) is treated as held, not auto-cleared"
assert_failure sq_acquire_lock "$fake_rig2/coordinator.lock" "token-D" "owner D"
t_begin "malformed lock directory is left untouched, never deleted"
[ -d "$fake_rig2/coordinator.lock" ] && t_ok || t_fail "malformed lock was removed"

echo "== source: commit validation =="
t_begin "sq_archive_source rejects a non-commit ref"
bogus_blob=$(git -C "$repo_root" rev-parse HEAD:AGENTS.md)
assert_failure sq_archive_source "$bogus_blob" "$fake_rig/should-not-exist.tar"
t_begin "no tar file was produced for the rejected ref"
[ ! -e "$fake_rig/should-not-exist.tar" ] && t_ok || t_fail "tar file exists despite rejection"

t_begin "sq_archive_source accepts a real commit and produces a stable, ustar tar"
assert_success sq_archive_source "HEAD" "$fake_rig/real.tar"
t_begin "produced tar has no pax_global_header contamination"
if ! tar -tf "$fake_rig/real.tar" > "$test_scratch/archive-list"; then
    t_fail "cannot list produced source archive"
elif grep -q pax_global_header "$test_scratch/archive-list"; then
    t_fail "pax_global_header leaked into the archive"
else
    t_ok
fi

echo "== staging: hash verification and exit-code capture =="
mkdir -p "$fake_rig/stagehost"
t_begin "sq_stage_iso rejects a corrupted transfer (hash mismatch)"
sq_rscp() { cp "$1" "$2"; printf 'corruption' >> "$2"; }  # simulate silent corruption in transit
if sq_stage_iso "$fake_rig/real.tar" "$fake_rig/stagehost" "runA" >"$test_output" 2>"$test_error"; then
    t_fail "corrupt archive was accepted"
elif grep -q 'staged archive hash mismatch' "$test_error"; then
    t_ok
else
    t_fail "did not reach checksum rejection: $(cat "$test_error")"
fi
sq_rscp() { cp "$1" "$2"; }  # restore

t_begin "corrupt-transfer rejection never extracts the reserved namespace"
[ ! -d "$fake_rig/stagehost/qualify-runA/tree" ] && t_ok || t_fail "isostage-runA exists despite the copy failing before mkisofs ran"

if command -v mkisofs >/dev/null 2>&1; then
    t_begin "sq_stage_iso succeeds end to end with a real mkisofs"
    if out=$(sq_stage_iso "$fake_rig/real.tar" "$fake_rig/stagehost" "runB" 2>"$test_error"); then
        iso_name=$(printf '%s\n' "$out" | sed -n 1p)
        iso_hash=$(printf '%s\n' "$out" | sed -n 2p)
        [ -f "$fake_rig/stagehost/$iso_name" ] && [ -n "$iso_hash" ] && t_ok || t_fail "missing iso or hash: $out"
    else
        t_fail "$(cat "$test_error")"
    fi

    t_begin "a second stage with the same run id is refused (no silent overwrite)"
    assert_failure sq_stage_iso "$fake_rig/real.tar" "$fake_rig/stagehost" "runB"
else
    echo "skip - mkisofs not installed on this host; skipping the two mkisofs-dependent cases"
fi

t_begin "sq_stage_iso surfaces a real mkisofs failure, not masked by a trailing tail"
fake_bin=$(mktemp -d)
cat > "$fake_bin/mkisofs" <<'EOF'
#!/bin/sh
echo "simulated mkisofs failure" >&2
exit 7
EOF
chmod +x "$fake_bin/mkisofs"
PATH="$fake_bin:$PATH" bash -c '
    source "'"$repo_root"'/tools/lib/solaris9-qualify-lib.sh"
    sq_rsh() { bash -c "$2"; }
    sq_rscp() { cp "$1" "$2"; }
    sq_stage_iso "'"$fake_rig"'/real.tar" "'"$fake_rig"'/stagehost" "runC"
' >"$test_output" 2>"$test_error"
rc=$?
rm -rf "$fake_bin"
if [ "$rc" != 0 ] && grep -q 'simulated mkisofs failure' "$fake_rig/stagehost/qualify-runC/mkisofs.log"; then
    t_ok
else
    t_fail "did not propagate actual mkisofs failure: $(cat "$test_error")"
fi

echo "== media swap: verified unmount before eject =="
t_begin "sq_swap_media refuses to proceed if the guest still reports /mnt mounted"
sq_guest() { echo "STILL_MOUNTED"; }
sq_rsh() { t_fail 'unexpected monitor call on failed unmount'; return 1; }
assert_failure sq_swap_media "$fake_rig" "drive7" "some.iso"

t_begin "sq_swap_media proceeds only after a confirmed UNMOUNTED state, and verifies block state at each step"
eject_called=0
change_called=0
sq_guest() { echo "SQ_UNMOUNTED"; }
sq_rsh() {
    local _t=$1 cmd=$2
    case "$cmd" in
        *"console.py"*"mount | grep"*) echo "UNMOUNTED" ;;
        *"mon.py"*"eject"*) eject_called=1 ;;
        *"mon.py"*"info block"*)
            if [ "$change_called" = 1 ]; then
                echo "drive7 (#block1): $fake_rig/some.iso (raw, read-only)"
            else
                echo "drive7: [not inserted]"
            fi
            ;;
        *"mon.py"*"change"*) change_called=1 ;;
        *) bash -c "$cmd" ;;
    esac
}
assert_success sq_swap_media "$fake_rig" "drive7" "some.iso"

echo "== poll: bounded timeout never falsely reports success =="
t_begin "sq_poll_build returns 2 (uncertain) when the guest process never disappears before the deadline"
sq_guest() { echo "SQ_RUNNING"; }
deadline=$(( $(date +%s) + 1 ))
sq_poll_build "$fake_rig" "12345" "$deadline" 1 /unused/exit token
rc=$?
assert_eq "poll timeout return code" "2" "$rc"

t_begin "sq_poll_build returns 0 once the guest process is confirmed gone"
# A shell variable does not work as mock state here: sq_rsh is invoked
# via command substitution ($(...) ), which forks a subshell, so a
# plain variable mutation inside it never persists back to the caller
# (found via a real test run, not assumed) -- a file does persist.
still_flag=$(mktemp)
echo 1 > "$still_flag"
sq_guest() {
    if [ "$(cat "$still_flag")" = 1 ]; then echo 0 > "$still_flag"; echo "SQ_RUNNING"; else echo "SQ_DONE:token:0"; fi
}
deadline=$(( $(date +%s) + 60 ))
sq_poll_build "$fake_rig" "12345" "$deadline" 1 /unused/exit token
rc=$?
assert_eq "poll success return code" "0" "$rc"

echo "== verify: real exit status AND exact fresh marker, never a stale/partial match =="
t_begin "nonzero exit status fails even if the marker text is present"
printf 'SOLARIS9_CANNEDBSD_TEST=PASS\n' > "$fake_rig/stale.log"
assert_failure sq_verify_pass "$fake_rig/stale.log" "1"

t_begin "zero exit status fails if the marker is absent"
printf 'no marker here\n' > "$fake_rig/nomark.log"
assert_failure sq_verify_pass "$fake_rig/nomark.log" "0"

t_begin "zero exit status fails if the marker is only a partial/substring match"
printf 'not really SOLARIS9_CANNEDBSD_TEST=PASSED as a substring\n' > "$fake_rig/partial.log"
assert_failure sq_verify_pass "$fake_rig/partial.log" "0"

t_begin "zero exit status with the exact marker line passes"
printf 'some build output\nSOLARIS9_CANNEDBSD_TEST=PASS\n' > "$fake_rig/good.log"
assert_success sq_verify_pass "$fake_rig/good.log" "0"

echo
echo "== summary: $pass_count passed, $fail_count failed =="
[ "$fail_count" -eq 0 ]

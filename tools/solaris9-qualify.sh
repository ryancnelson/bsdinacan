#!/usr/bin/env bash
# Serialized, exact-commit Solaris 9 SPARC qualification runner (SOLARIS-02).
#
# Orchestrates, from the operator's own host (not the guest, not the
# rig host): validate and git-archive an exact commit, stage it onto
# the shared QEMU rig via a fresh Rock-Ridge ISO, drive the guest
# console to mount and build it with tools/solaris9-build.sh, and
# assert a real, fresh PASS -- both the exact final marker text AND a
# real captured exit status, not merely a text grep -- before
# declaring success. All actual logic lives in
# tools/lib/solaris9-qualify-lib.sh, which an offline test harness
# sources and exercises with mocked transport. The full driver also runs
# against a temporary, offline transport fixture in
# tests/test_solaris9_qualify_driver.py; make check-solaris9-qualify runs both
# suites and the generated-command/protocol controls without using a rig.
#
# Every artifact and log this run touches is written under a unique,
# collision-checked run directory; nothing here overwrites or deletes
# any other run's evidence, and never edits VM image files directly.
# Rig ownership is acquired atomically and released only once
# the guest's completion state is positively confirmed -- a timeout
# with uncertain guest state deliberately leaves the lock held rather
# than risk two runs colliding.
#
# This intentionally does not manage a Woodpecker `solaris9` CI status
# itself (that is SOLARIS-02's own eventual "Accept" criterion, once
# this manual/scripted gate has a proven track record); it is the
# smallest repeatable local runner that gate can eventually wrap.
#
# Required environment (no defaults baked in -- keeps private rig
# coordinates out of the repository, per notes/CI.md):
#   SOLARIS_SSH_HOSTNAME     real IP/hostname of the rig's SSH endpoint
#   SOLARIS_SSH_HOSTKEYALIAS ssh -o HostKeyAlias value (pinned host key)
#   SOLARIS_SSH_KNOWNHOSTS   path to the known_hosts file pinning that key
#   SOLARIS_RIG_DIR          rig-side working directory (holds console.py,
#                            mon.py, coordinator.lock/, isostage dirs, etc.)
#   SOLARIS_DRIVE_ID         QEMU block device id for the source CD-ROM
#                            (e.g. "drive7"; see `info block` on the rig)
#   SOLARIS_GUEST_DEV        guest device node for that CD-ROM
#                            (e.g. "/dev/dsk/c0t6d0s2")
#   OWNER_LABEL              free-text string identifying this run's owner,
#                            recorded as the rig lock's holder
#
# Optional:
#   SOLARIS_SSH_USER         default "root"
#   EVIDENCE_ROOT            default "$repo_root/evidence"
#
# Usage:
#   tools/solaris9-qualify.sh <git-ref-or-commit>
#
# Exit status: 0 only if the guest run produced both a real exit status
# of 0 and the exact SOLARIS9_CANNEDBSD_TEST=PASS marker in this run's
# own fresh transcript. Any other outcome is a nonzero exit with a
# clear diagnosis. Any uncertainty after the first guest operation retains
# ownership, including timeout, disconnect, interruption and failed unmount.
# Host-only failures before guest use may release our exact owned lock.
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
# shellcheck source=lib/solaris9-qualify-lib.sh
source "$repo_root/tools/lib/solaris9-qualify-lib.sh"

die() { sq_die "$*"; exit 1; }

[ $# -eq 1 ] || die "usage: $0 <git-ref-or-commit>"
REF=$1

: "${SOLARIS_SSH_HOSTNAME:?SOLARIS_SSH_HOSTNAME must be set (see script header)}"
: "${SOLARIS_SSH_HOSTKEYALIAS:?SOLARIS_SSH_HOSTKEYALIAS must be set}"
: "${SOLARIS_SSH_KNOWNHOSTS:?SOLARIS_SSH_KNOWNHOSTS must be set}"
: "${SOLARIS_RIG_DIR:?SOLARIS_RIG_DIR must be set}"
: "${SOLARIS_DRIVE_ID:?SOLARIS_DRIVE_ID must be set}"
: "${SOLARIS_GUEST_DEV:?SOLARIS_GUEST_DEV must be set}"
: "${OWNER_LABEL:?OWNER_LABEL must be set (identify who/what owns this run)}"
SOLARIS_SSH_USER=${SOLARIS_SSH_USER:-root}

cd "$repo_root"

# --- real transport, installed over the lib's stub implementations -----
ssh_target="${SOLARIS_SSH_USER}@${SOLARIS_SSH_HOSTKEYALIAS}"
ssh_opts=(-o BatchMode=yes -o ConnectTimeout=8 \
          -o "HostKeyAlias=${SOLARIS_SSH_HOSTKEYALIAS}" \
          -o "UserKnownHostsFile=${SOLARIS_SSH_KNOWNHOSTS}" \
          -o "Hostname=${SOLARIS_SSH_HOSTNAME}")
sq_rsh() { timeout "${1}" ssh "${ssh_opts[@]}" "$ssh_target" "$2"; }
sq_rscp() {
    # Stream into the already exclusively reserved namespace, without scp's
    # protocol-dependent remote pathname quoting. Refuse an existing file.
    timeout 60 ssh "${ssh_opts[@]}" "$ssh_target" \
        "set -C; cat > $(sq_quote_remote "$2")" < "$1"
}

# Resolve and peel the commit before any rig operation.
commit=$(git rev-parse --verify --end-of-options "${REF}^{commit}") || die "not a commit: $REF"
token=$(sq_gen_token) || die "cannot generate unique run token"
run_id="$token"
evidence_root=${EVIDENCE_ROOT:-$repo_root/evidence}
mkdir -p "$evidence_root" || die "cannot create evidence parent"
run_dir=$evidence_root/$run_id
mkdir "$run_dir" || die "run directory collision: $run_dir"
log() { printf '%s\n' "$*" | tee -a "$run_dir/00-summary.log"; }
log "run $run_id commit $commit"
sq_archive_source "$commit" "$run_dir/source.tar" || die 'source archive failed'
printf '%s\n' "$commit" > "$run_dir/commit-sha.txt"
git rev-parse "$commit^{tree}" > "$run_dir/tree-sha.txt"
shasum -a 256 "$run_dir/source.tar" > "$run_dir/source-sha256.txt"

lock_base="$SOLARIS_RIG_DIR/coordinator.lock"
lock_held=0
safe_to_release=1
release_lock_if_held() {
    local result=$?
    trap - EXIT
    if [ "$lock_held" = 1 ]; then
        if [ "$safe_to_release" = 1 ]; then
            if sq_release_lock "$lock_base" "$token"; then
                log "rig lock released: $token"
            else
                log 'rig lock release failed; inspect retained ownership'
                result=1
            fi
        else
            log "rig lock retained: guest state uncertain; token $token"
        fi
    fi
    exit "$result"
}
trap 'release_lock_if_held' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

sq_acquire_lock "$lock_base" "$token" "$OWNER_LABEL" || die 'could not acquire rig ownership'
lock_held=1
log "rig lock acquired: $token"
stage_result=$(sq_stage_iso "$run_dir/source.tar" "$SOLARIS_RIG_DIR" "$run_id") || die 'ISO staging failed'
rig_iso=$(printf '%s\n' "$stage_result" | sed -n '1p')
staged_hash=$(printf '%s\n' "$stage_result" | sed -n '2p')
[ "$(printf '%s\n' "$stage_result" | wc -l | tr -d ' ')" = 2 ] || die 'invalid stage result'
log "staged ISO: $rig_iso sha256=$staged_hash"

# From this point an interrupted/failed command may have changed guest state.
# Release only after positive job completion and verified final unmount.
safe_to_release=0
sq_swap_media "$SOLARIS_RIG_DIR" "$SOLARIS_DRIVE_ID" "$rig_iso" || die 'media swap failed'
guest_dir="/var/tmp/sq-$run_id"
sq_mount_and_extract "$SOLARIS_RIG_DIR" "$SOLARIS_GUEST_DEV" "$guest_dir" \
    > "$run_dir/02-guest-stage.log" || die 'guest extraction failed'
sq_guest "$SOLARIS_RIG_DIR" toolchain 'echo "PATH=$PATH"; command -v gcc && command -v make && gcc --version && make --version && uname -a' \
    > "$run_dir/03-toolchain.log" || die 'toolchain capture failed'

guest_log="/var/tmp/sq-$run_id.log"
guest_exit_marker="/var/tmp/sq-$run_id.exit"
build_pid=$(sq_start_build "$SOLARIS_RIG_DIR" "$guest_dir" "$guest_log" "$guest_exit_marker" "$token") || die 'guest build start uncertain'
log "guest build PID $build_pid"
poll_deadline=$(( $(date +%s) + 1800 ))
set +e
real_exit=$(sq_poll_build "$SOLARIS_RIG_DIR" "$build_pid" "$poll_deadline" 20 "$guest_exit_marker" "$token")
poll_status=$?
set -e
case "$poll_status" in
    0) log "guest job completed: status $real_exit" ;;
    2)
        log "TIMEOUT: build completion unconfirmed, PID $build_pid"
        exit 1
        ;;
    *) log 'guest completion uncertain; no further console commands'; exit 1 ;;
esac
sq_fetch_transcript "$SOLARIS_RIG_DIR" "$guest_log" "$run_dir/native-transcript.raw.log" "$run_dir/native-transcript.log" || die 'transcript fetch failed'
shasum -a 256 "$run_dir/native-transcript.raw.log" "$run_dir/native-transcript.log" > "$run_dir/SHA256SUMS.txt"
sq_unmount "$SOLARIS_RIG_DIR" || die 'final guest unmount unconfirmed'
safe_to_release=1
printf '%s\n' "$real_exit" > "$run_dir/guest-exit-status.txt"
if sq_verify_pass "$run_dir/native-transcript.log" "$real_exit"; then
    log 'outcome: PASS (lock release must also succeed)'
    log "evidence: $run_dir"
    exit 0
fi
log 'outcome: FAIL'
exit 1

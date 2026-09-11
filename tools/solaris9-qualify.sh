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
# (tests/test_solaris9_qualify.sh) sources and exercises with mocked
# transport, without ever touching the real rig; this script is only
# the real-transport wiring plus the ordered call sequence.
#
# Every artifact and log this run touches is written under a unique,
# collision-checked run directory; nothing here overwrites or deletes
# any other run's evidence, and no VM disk image (qcow2) is ever
# touched. Rig ownership is acquired atomically and released only once
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
# clear diagnosis; on a build-completion timeout specifically, the rig
# lock is deliberately left held (see the lib's sq_poll_build) rather
# than released against an uncertain guest state.
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
sq_rscp() { timeout 60 scp "${ssh_opts[@]}" "$1" "$ssh_target:$2"; }

commit=$(git rev-parse "$REF") || die "cannot resolve ref '$REF'"
token=$(sq_gen_token)
run_id="$token"
evidence_root=${EVIDENCE_ROOT:-$repo_root/evidence}
run_dir=$evidence_root/$run_id
# mkdir, not -p: a collision here would mean sq_gen_token's own
# collision resistance failed, which must abort loudly, not silently
# reuse or overwrite another run's evidence directory.
mkdir "$run_dir" || die "run directory '$run_dir' already existed (unexpected token collision)"

log() { printf '%s\n' "$*" | tee -a "$run_dir/00-summary.log"; }

log "== solaris9-qualify: run $run_id for commit $commit =="

lock_base="$SOLARIS_RIG_DIR/coordinator.lock"
lock_held=0
release_lock_if_held() {
    if [ "$lock_held" = 1 ]; then
        if sq_release_lock "$lock_base" "$token"; then
            log "rig lock: released ($token)"
        else
            log "rig lock: RELEASE FAILED -- left held; investigate manually"
        fi
    fi
}
# On a build-completion timeout specifically, sq_poll_build's own
# caller below never calls release_lock_if_held at all (see the
# explicit `case` on its return status) -- this trap is only a backstop
# for other unexpected exits (e.g. an uncaught error), and still
# deliberately leaves the lock alone if $lock_held was never set to a
# confirmed-safe-to-release state by that point.
trap 'release_lock_if_held' EXIT

sq_acquire_lock "$lock_base" "$token" "$OWNER_LABEL run $run_id" \
    || die "could not acquire the rig lock; see stderr above"
lock_held=1
log "rig lock: acquired for $OWNER_LABEL (run $run_id)"

sq_archive_source "$commit" "$run_dir/source.tar" \
    || die "source archive step failed; see stderr above"
git rev-parse "$commit" > "$run_dir/commit-sha.txt"
git rev-parse "$commit^{tree}" > "$run_dir/tree-sha.txt"
shasum -a 256 "$run_dir/source.tar" > "$run_dir/source-sha256.txt"
log "source archive: $(cat "$run_dir/source-sha256.txt")"

stage_result=$(sq_stage_iso "$run_dir/source.tar" "$SOLARIS_RIG_DIR" "$run_id") \
    || die "ISO staging failed; see stderr above"
rig_iso=$(printf '%s\n' "$stage_result" | sed -n '1p')
staged_hash=$(printf '%s\n' "$stage_result" | sed -n '2p')
log "staged ISO: $rig_iso sha256=$staged_hash"

sq_swap_media "$SOLARIS_RIG_DIR" "$SOLARIS_DRIVE_ID" "$rig_iso" \
    || die "media swap failed; see stderr above"
log "media: attached $rig_iso to $SOLARIS_DRIVE_ID (verified unmount-then-swap)"

guest_dir="/var/tmp/sq-$run_id"
sq_mount_and_extract "$SOLARIS_RIG_DIR" "$SOLARIS_GUEST_DEV" "$guest_dir" \
    > "$run_dir/02-guest-stage.log" || die "guest mount/extract failed; see $run_dir/02-guest-stage.log"
log "guest: source staged at $guest_dir"

sq_rsh 30 "cd $(sq_quote_remote "$SOLARIS_RIG_DIR") && python3 console.py 'echo PATHIS:\$PATH' 'which gcc make ksh' 'gcc --version' 'make --version' 'uname -a'" \
    > "$run_dir/03-toolchain.log" 2>&1 || true

guest_log="/var/tmp/sq-$run_id.log"
guest_exit_marker="/var/tmp/sq-$run_id.exit"
build_pid=$(sq_start_build "$SOLARIS_RIG_DIR" "$guest_dir" "$guest_log" "$guest_exit_marker") \
    || die "could not start the guest build; see stderr above"
log "build started as guest PID $build_pid, polling for completion..."

poll_deadline=$(( $(date +%s) + 1800 ))  # 30 minutes, generous upper bound
set +e
sq_poll_build "$SOLARIS_RIG_DIR" "$build_pid" "$poll_deadline" 20
poll_status=$?
set -e
case "$poll_status" in
    0)
        log "build process $build_pid no longer running; guest state confirmed"
        ;;
    2)
        # Deliberately do NOT release the lock: the guest's actual
        # completion state is unknown, not merely "probably done".
        log "TIMEOUT: build did not finish within the poll deadline (guest PID $build_pid)"
        log "rig lock intentionally left held -- guest state uncertain, not released on a timeout"
        exit 1
        ;;
    *)
        log "polling itself failed (rig unreachable?) -- guest state uncertain, lock left held"
        exit 1
        ;;
esac

real_exit=$(sq_fetch_exit_status "$SOLARIS_RIG_DIR" "$guest_exit_marker") \
    || die "could not fetch the guest exit status; see stderr above"
sq_fetch_transcript "$SOLARIS_RIG_DIR" "$guest_log" \
    "$run_dir/native-transcript.raw.log" "$run_dir/native-transcript.log" \
    || die "could not fetch the guest transcript; see stderr above"
shasum -a 256 "$run_dir/native-transcript.raw.log" "$run_dir/native-transcript.log" \
    > "$run_dir/SHA256SUMS.txt"

log "real_exit_status: $real_exit"

if sq_verify_pass "$run_dir/native-transcript.log" "$real_exit"; then
    log "outcome: PASS"
    echo "evidence: $run_dir"
    exit 0
else
    log "outcome: FAIL -- see $run_dir/native-transcript.log"
    exit 1
fi

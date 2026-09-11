#!/usr/bin/env bash
# Function library for tools/solaris9-qualify.sh, split out so an
# offline test harness (tests/test_solaris9_qualify.sh) can source it
# and override sq_rsh/sq_rscp/sq_git/etc. with mocks -- none of this
# file's own top level executes anything; it only defines functions.
# Sourcing it is always safe, with or without real rig credentials.

# --- quoting -----------------------------------------------------------
# Single-quote a string for safe embedding in a remote POSIX shell
# command line, including apostrophes and embedded newlines. Plain
# printf '%s' with naive quoting breaks on both; this is the standard
# POSIX-safe technique (close the quote, emit an escaped literal quote,
# reopen the quote) and does not depend on the remote shell being bash.
sq_quote_remote() {
    local s=$1 escaped
    # Built via sed, not a bash ${..//..} pattern substitution: an
    # earlier version of this function used the latter and produced
    # subtly wrong output (found via a real round-trip test, not
    # inspection) -- parameter-expansion replacement text follows its
    # own quoting rules that do not compose the way a literal `'\''`
    # sequence would suggest. sed's replacement text is unambiguous.
    escaped=$(printf '%s' "$s" | sed "s/'/'\\\\''/g")
    printf "'%s'" "$escaped"
}

sq_die() {
    printf 'solaris9-qualify: %s\n' "$*" >&2
    return 1
}

# --- transport (overridden wholesale by tests) --------------------------
# sq_rsh <timeout-seconds> <remote-command-string>
# sq_rscp <local-path> <remote-path>
# Real implementations are installed by the driver script after it has
# validated SOLARIS_SSH_* environment variables; tests define their own
# versions before sourcing this file's callers, simulating rig
# responses without any network access.
sq_rsh() { sq_die "sq_rsh has no real implementation loaded"; }
sq_rscp() { sq_die "sq_rscp has no real implementation loaded"; }

# --- unique tokens -------------------------------------------------------
# Timestamp + pid + 4 random bytes: collision-resistant even for two
# invocations started in the same second (found to matter in practice:
# a bare `date -u +%Y%m%dT%H%M%SZ`-derived run_id collided across two
# runs started less than a second apart in earlier testing).
sq_gen_token() {
    local rand
    rand=$(od -An -N4 -tx1 /dev/urandom 2>/dev/null | tr -d ' \n')
    [ -n "$rand" ] || rand=$RANDOM$RANDOM
    printf '%s-%s-%s' "$(date -u +%Y%m%dT%H%M%SZ)" "$$" "$rand"
}

# --- source identity -----------------------------------------------------
# Validates the ref resolves to an actual commit object (not merely
# "some object", which could be a tree, blob, or dangling tag), then
# archives that exact commit -- never the live working tree, so
# uncommitted local changes can never contaminate what is qualified.
#
# git archive --format=tar emits a pax global header recording the
# commit hash (for reproducibility) that this guest's own old `tar`
# does not understand -- found via a real staging attempt, not
# assumed: it converts the unrecognized header into a literal
# "pax_global_header" file in the extracted tree, contaminating the
# source. Repack through a plain extract + ustar re-tar to avoid it.
#
# sq_archive_source <commit> <out-tar-path>
sq_archive_source() {
    local commit=$1 out_tar=$2
    git cat-file -e "${commit}^{commit}" 2>/dev/null \
        || { sq_die "'$commit' does not resolve to a commit object"; return 1; }
    local extract_dir
    extract_dir=$(mktemp -d) || { sq_die "mktemp -d failed"; return 1; }
    if ! git archive --format=tar "$commit" | (cd "$extract_dir" && tar -xf -); then
        rm -rf "$extract_dir"
        sq_die "git archive/extract failed for $commit"
        return 1
    fi
    if ! COPYFILE_DISABLE=1 tar --format=ustar -cf "$out_tar" -C "$extract_dir" .; then
        rm -rf "$extract_dir"
        sq_die "ustar repack failed for $commit"
        return 1
    fi
    rm -rf "$extract_dir"
    return 0
}

# --- serialized rig ownership ---------------------------------------------
# Atomic: ownership is decided by whether a remote `mkdir` for a fixed,
# well-known path succeeds -- mkdir either creates the directory or
# fails if it already exists, with no window for two callers to both
# believe they succeeded (unlike the earlier "read owner.txt, then
# separately write it" sequence, which had a real check-then-act race,
# and unlike a substring match against a human-readable owner label,
# which could falsely treat an unrelated lock as already ours, or
# refuse to recognize a real one that phrases the label differently).
# A malformed or unreadable existing lock is always treated as "held by
# someone else" -- never auto-cleared.
#
# sq_acquire_lock <lock-base-dir> <token> <owner-label>
# On success, echoes nothing and returns 0; the caller's token is now
# recorded as the holder. On failure (lock held, or any remote error),
# returns 1 and prints the existing owner info (if any) to stderr.
sq_acquire_lock() {
    local lock_base=$1 token=$2 owner_label=$3
    local holder_dir="$lock_base/holder"
    # mkdir -p on the base only: idempotent and harmless if several
    # callers race on it (it never fails just because the directory
    # already exists), unlike the atomic mkdir below on $holder_dir
    # itself, which is the actual mutex. Without this, a fresh rig
    # whose coordinator.lock/ parent does not yet exist would fail
    # with ENOENT on the very first acquire and be misreported as
    # "already held by someone else" -- found via a real test, not
    # assumed, since the live rig's coordinator.lock/ already existed
    # from an earlier manual claim and never exposed this path.
    if ! sq_rsh 15 "mkdir -p $(sq_quote_remote "$lock_base")" 2>/dev/null; then
        sq_die "could not create the rig lock base directory '$lock_base'"
        return 1
    fi
    if ! sq_rsh 20 "mkdir $(sq_quote_remote "$holder_dir")" 2>/dev/null; then
        local existing
        existing=$(sq_rsh 15 "cat $(sq_quote_remote "$holder_dir/owner.txt") 2>/dev/null") || existing=""
        if [ -n "$existing" ]; then
            printf 'Rig lock already held:\n%s\n' "$existing" >&2
        else
            printf 'Rig lock directory exists but its owner record is unreadable or malformed;\n' >&2
            printf 'treating as held by someone else and refusing to proceed.\n' >&2
        fi
        return 1
    fi
    local owner_file="$holder_dir/owner.txt"
    if ! sq_rsh 20 "printf '%s\n' $(sq_quote_remote "$owner_label") > $(sq_quote_remote "$owner_file") && printf '%s\n' $(sq_quote_remote "$token") >> $(sq_quote_remote "$owner_file")"; then
        # We created the marker but failed to record who we are; do not
        # leave an anonymous, unattributable lock behind.
        sq_rsh 20 "rmdir $(sq_quote_remote "$holder_dir")" 2>/dev/null || true
        sq_die "acquired the lock marker but failed to record ownership; released it"
        return 1
    fi
    return 0
}

# Releases the lock ONLY if the recorded token matches exactly what we
# hold -- never released merely because a caller believes a run timed
# out, and never released "because it looks stale": a genuinely
# uncertain guest state (e.g. the build's completion could not be
# confirmed) must keep the lock held for a human to investigate, not
# have it silently freed for the next automated run to collide with.
#
# sq_release_lock <lock-base-dir> <token>
sq_release_lock() {
    local lock_base=$1 token=$2
    local holder_dir="$lock_base/holder"
    local owner_file="$holder_dir/owner.txt"
    local recorded
    recorded=$(sq_rsh 20 "cat $(sq_quote_remote "$owner_file") 2>/dev/null") || recorded=""
    if ! printf '%s\n' "$recorded" | grep -qxF "$token"; then
        sq_die "recorded lock token does not match ours; NOT releasing (possible takeover, restart, or corruption -- investigate manually)"
        return 1
    fi
    sq_rsh 20 "rm -f $(sq_quote_remote "$owner_file") && rmdir $(sq_quote_remote "$holder_dir")"
}

# --- staging: fresh, uniquely-named, never overwriting another run's ----
# sq_stage_iso <local-tar-path> <rig-dir> <run-id> ; echoes the rig-side
# ISO filename and its remote sha256 on success (two lines).
sq_stage_iso() {
    local local_tar=$1 rig_dir=$2 run_id=$3
    local rig_stage="cannedbsd-src-$run_id.tar.gz"
    local rig_isostage="isostage-$run_id"
    local rig_iso="source-$run_id.iso"
    local local_gz
    local_gz=$(mktemp) || { sq_die "mktemp failed"; return 1; }
    gzip -c "$local_tar" > "$local_gz"
    local local_gz_hash remote_gz_hash
    local_gz_hash=$(shasum -a 256 "$local_gz" | awk '{print $1}')

    if ! sq_rscp "$local_gz" "$rig_dir/$rig_stage"; then
        rm -f "$local_gz"
        sq_die "scp of staged archive failed"
        return 1
    fi
    rm -f "$local_gz"

    # Verify the transfer landed intact before ever extracting it --
    # reject silent corruption rather than discovering it only as a
    # confusing later build failure.
    remote_gz_hash=$(sq_rsh 30 "cd $(sq_quote_remote "$rig_dir") && sha256sum $(sq_quote_remote "$rig_stage") | awk '{print \$1}'") \
        || { sq_die "could not read back the staged archive's hash on the rig"; return 1; }
    if [ "$remote_gz_hash" != "$local_gz_hash" ]; then
        sq_die "staged archive hash mismatch: local $local_gz_hash != remote $remote_gz_hash -- rejecting, not building from a possibly-corrupt copy"
        return 1
    fi

    # mkdir (not -p): a collision on an isostage directory name must be
    # a loud, immediate failure, never a silent reuse/overwrite of
    # another run's staging area.
    if ! sq_rsh 60 "cd $(sq_quote_remote "$rig_dir") && mkdir $(sq_quote_remote "$rig_isostage")"; then
        sq_die "isostage directory '$rig_isostage' already existed on the rig (unexpected token collision) -- aborting rather than reusing it"
        return 1
    fi

    # Capture mkisofs's own real exit status: never through a `| tail`,
    # which would report tail's exit status instead and silently mask
    # a real mkisofs failure.
    local extract_and_build
    extract_and_build="cd $(sq_quote_remote "$rig_dir") && tar -xzf $(sq_quote_remote "$rig_stage") -C $(sq_quote_remote "$rig_isostage"); te=\$?; if [ \$te -ne 0 ]; then echo TAR_EXIT=\$te; exit 1; fi; echo TAR_EXIT=0; mkisofs -r -J -o $(sq_quote_remote "$rig_iso") $(sq_quote_remote "$rig_isostage") > /tmp/mkisofs-$run_id.out 2>&1; me=\$?; tail -5 /tmp/mkisofs-$run_id.out; echo MKISOFS_EXIT=\$me; if [ \$me -ne 0 ]; then exit 1; fi; sha256sum $(sq_quote_remote "$rig_iso")"
    local stage_out
    if ! stage_out=$(sq_rsh 90 "$extract_and_build"); then
        printf '%s\n' "$stage_out" >&2
        sq_die "ISO staging failed on the rig (tar extract or mkisofs); see output above"
        return 1
    fi
    printf '%s\n' "$stage_out"
    if ! printf '%s\n' "$stage_out" | grep -q 'TAR_EXIT=0'; then
        sq_die "remote tar extract did not report a clean exit"
        return 1
    fi
    if ! printf '%s\n' "$stage_out" | grep -q 'MKISOFS_EXIT=0'; then
        sq_die "remote mkisofs did not report a clean exit"
        return 1
    fi
    local iso_hash
    iso_hash=$(printf '%s\n' "$stage_out" | grep -Eo '^[0-9a-f]{64}  ' | awk '{print $1}' | tail -1)
    [ -n "$iso_hash" ] || { sq_die "could not parse the staged ISO's sha256 from rig output"; return 1; }
    printf '%s\n%s\n' "$rig_iso" "$iso_hash"
    return 0
}

# --- media swap: verified unmount first, verified state at every step ---
# Never eject before the guest has actually unmounted the current
# media (yanking a mounted device out from under a live guest kernel
# is not assumed safe merely because reads are read-only) and never
# assume a monitor command took effect -- `info block` is queried
# after both the eject and the change, and each result is checked
# against what was actually requested.
#
# sq_swap_media <rig-dir> <drive-id> <iso-filename>
sq_swap_media() {
    local rig_dir=$1 drive_id=$2 iso_name=$3

    local umount_check
    umount_check=$(sq_rsh 60 "cd $(sq_quote_remote "$rig_dir") && python3 console.py 'mount | grep -q \" /mnt \" && umount /mnt; mount | grep -q \" /mnt \" && echo STILL_MOUNTED || echo UNMOUNTED'") \
        || { sq_die "could not verify guest unmount state"; return 1; }
    if printf '%s\n' "$umount_check" | grep -q 'STILL_MOUNTED'; then
        sq_die "guest still reports /mnt mounted after umount attempt -- refusing to eject media out from under it"
        return 1
    fi
    if ! printf '%s\n' "$umount_check" | grep -q 'UNMOUNTED'; then
        sq_die "could not confirm guest unmount (no UNMOUNTED/STILL_MOUNTED marker seen)"
        return 1
    fi

    sq_rsh 30 "cd $(sq_quote_remote "$rig_dir") && python3 mon.py $(sq_quote_remote "eject -f $drive_id") >/dev/null" \
        || { sq_die "monitor eject command failed"; return 1; }
    local block_after_eject
    block_after_eject=$(sq_rsh 30 "cd $(sq_quote_remote "$rig_dir") && python3 mon.py 'info block'") \
        || { sq_die "could not query block state after eject"; return 1; }
    if ! printf '%s\n' "$block_after_eject" | grep -A2 "^$drive_id:" | grep -q 'not inserted'; then
        sq_die "drive $drive_id does not report 'not inserted' after eject -- refusing to insert new media into an uncertain state"
        return 1
    fi

    local change_cmd="change $drive_id json:{\"driver\":\"raw\",\"read-only\":true,\"file\":{\"driver\":\"file\",\"filename\":\"$rig_dir/$iso_name\",\"locking\":\"off\"}}"
    sq_rsh 30 "cd $(sq_quote_remote "$rig_dir") && python3 mon.py $(sq_quote_remote "$change_cmd") >/dev/null" \
        || { sq_die "monitor change command failed"; return 1; }
    local block_after_change
    block_after_change=$(sq_rsh 30 "cd $(sq_quote_remote "$rig_dir") && python3 mon.py 'info block'") \
        || { sq_die "could not query block state after change"; return 1; }
    if ! printf '%s\n' "$block_after_change" | grep -F "$iso_name"; then
        sq_die "drive $drive_id does not show '$iso_name' attached after change -- aborting"
        return 1
    fi
    return 0
}

# --- guest mount + extract, real exit codes, exclusive fresh dir ---------
# sq_mount_and_extract <rig-dir> <guest-dev> <guest-dir>
sq_mount_and_extract() {
    local rig_dir=$1 guest_dev=$2 guest_dir=$3
    # mkdir, not `rm -rf ... && mkdir -p`: a name collision must abort
    # loudly rather than silently delete and reuse another run's
    # extracted tree.
    local cmd
    cmd="mount -F hsfs -o ro $guest_dev /mnt; me=\$?; echo MOUNT_EXIT=\$me; if [ \$me -ne 0 ]; then exit 1; fi; mkdir $guest_dir; de=\$?; echo MKDIR_EXIT=\$de; if [ \$de -ne 0 ]; then exit 1; fi; (cd /mnt && find . -print | cpio -pdum $guest_dir) > /tmp/cpio-$$.out 2>&1; ce=\$?; tail -3 /tmp/cpio-$$.out; echo CPIO_EXIT=\$ce; if [ \$ce -ne 0 ]; then exit 1; fi"
    local out
    if ! out=$(sq_rsh 90 "cd $(sq_quote_remote "$rig_dir") && python3 console.py $(sq_quote_remote "$cmd")"); then
        printf '%s\n' "$out" >&2
        sq_die "guest mount/mkdir/cpio failed; see output above"
        return 1
    fi
    printf '%s\n' "$out"
    printf '%s\n' "$out" | grep -q 'MOUNT_EXIT=0' || { sq_die "guest mount did not report a clean exit"; return 1; }
    printf '%s\n' "$out" | grep -q 'MKDIR_EXIT=0' || { sq_die "guest staging directory mkdir did not report a clean exit (possible name collision)"; return 1; }
    printf '%s\n' "$out" | grep -q 'CPIO_EXIT=0' || { sq_die "guest cpio extract did not report a clean exit"; return 1; }
    return 0
}

# --- build: start detached, poll with bounded, explicit ownership -------
# Never a single blocking call: console.py's own internal deadline is a
# fixed 600s, and a full clean build+run+acceptance-check pass on this
# emulated 32-bit SPARC guest has taken up to ~8-10 real minutes in
# practice. Started via nohup+background and polled with short,
# separate calls instead.
#
# sq_start_build <rig-dir> <guest-dir> <guest-log> <guest-exit-marker>
# echoes the guest-side PID on success.
sq_start_build() {
    local rig_dir=$1 guest_dir=$2 guest_log=$3 guest_exit_marker=$4
    local cmd="cd $guest_dir && nohup ksh -c 'ksh tools/solaris9-build.sh > $guest_log 2>&1; echo \$? > $guest_exit_marker' >/dev/null 2>&1 & echo STARTED_PID=\$!"
    local out
    out=$(sq_rsh 30 "cd $(sq_quote_remote "$rig_dir") && python3 console.py $(sq_quote_remote "$cmd")") \
        || { sq_die "could not start the background build"; return 1; }
    local pid
    pid=$(printf '%s\n' "$out" | grep -Eo 'STARTED_PID=[0-9]+' | grep -Eo '[0-9]+' | tail -1)
    [ -n "$pid" ] || { sq_die "could not capture the background build's PID; raw output: $out"; return 1; }
    printf '%s\n' "$pid"
    return 0
}

# sq_poll_build <rig-dir> <guest-pid> <deadline-epoch-seconds> <poll-interval-seconds>
# Returns 0 once the guest process is confirmed gone, 1 on a
# definitively unreachable rig, or 2 if the deadline passed while the
# guest state remains uncertain -- callers MUST treat exit 2 as "do not
# release the lock", not as a normal failure.
sq_poll_build() {
    local rig_dir=$1 guest_pid=$2 deadline=$3 interval=${4:-20}
    while :; do
        if [ "$(date +%s)" -ge "$deadline" ]; then
            return 2
        fi
        sleep "$interval"
        local still_running
        still_running=$(sq_rsh 30 "cd $(sq_quote_remote "$rig_dir") && python3 console.py $(sq_quote_remote "ps -ef | grep $guest_pid | grep -v grep")") || still_running=""
        if ! printf '%s\n' "$still_running" | grep -q "$guest_pid"; then
            return 0
        fi
    done
}

# --- fetch + verify: real exit status AND a fresh, non-stale marker ----
# sq_fetch_exit_status <rig-dir> <guest-exit-marker> ; echoes the integer
sq_fetch_exit_status() {
    local rig_dir=$1 guest_exit_marker=$2
    local out
    out=$(sq_rsh 30 "cd $(sq_quote_remote "$rig_dir") && python3 console.py $(sq_quote_remote "cat $guest_exit_marker")") \
        || { sq_die "could not read the guest exit marker"; return 1; }
    local val
    val=$(printf '%s\n' "$out" | python3 -c '
import re, sys
for line in sys.stdin:
    line = line.strip()
    if re.fullmatch(r"[0-9]+", line):
        print(line)
' | tail -1)
    [ -n "$val" ] || { sq_die "could not parse a bare integer exit status from guest output: $out"; return 1; }
    printf '%s\n' "$val"
    return 0
}

# sq_fetch_transcript <rig-dir> <guest-log> <raw-out-path> <clean-out-path>
sq_fetch_transcript() {
    local rig_dir=$1 guest_log=$2 raw_out=$3 clean_out=$4
    sq_rsh 30 "cd $(sq_quote_remote "$rig_dir") && python3 console.py $(sq_quote_remote "cat $guest_log")" \
        > "$raw_out" 2>&1 || { sq_die "could not fetch the guest transcript"; return 1; }
    python3 - "$raw_out" "$clean_out" <<'PYEOF'
import sys
raw_path, clean_path = sys.argv[1], sys.argv[2]
with open(raw_path, encoding="utf-8", errors="replace") as f:
    lines = f.read().splitlines()
out, skipped_echo = [], False
for ln in lines:
    if not skipped_echo and ln.startswith("# cat "):
        skipped_echo = True
        continue
    if ln.strip() == "#" or ln.startswith("CB_DONE_"):
        continue
    out.append(ln)
with open(clean_path, "w") as f:
    f.write("\n".join(out) + "\n")
PYEOF
    return 0
}

# Both the real exit status (0) AND the exact fresh marker line must be
# present in THIS run's own freshly-fetched transcript -- a nonzero
# exit with the marker text merely present somewhere stale, or a zero
# exit with no marker at all, are both rejected. The marker is matched
# as an exact whole line, not a substring, so it cannot be satisfied by
# a coincidental partial match elsewhere in the log.
#
# sq_verify_pass <clean-transcript-path> <real-exit-status>
# Returns 0 (pass), 1 (fail) and prints the reason to stderr.
sq_verify_pass() {
    local clean_transcript=$1 real_exit=$2
    if [ "$real_exit" != "0" ]; then
        printf 'FAIL: guest run exited %s (nonzero)\n' "$real_exit" >&2
        return 1
    fi
    if ! grep -qx 'SOLARIS9_CANNEDBSD_TEST=PASS' "$clean_transcript"; then
        printf 'FAIL: exit status 0 but SOLARIS9_CANNEDBSD_TEST=PASS marker missing from this run'"'"'s own fresh transcript\n' >&2
        return 1
    fi
    return 0
}

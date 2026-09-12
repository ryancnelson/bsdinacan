#!/usr/bin/env bash
# Offline-testable operations. Sourcing this file performs no rig actions.
sq_protocol_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sq_quote_remote() {
    # printf + sed runs in a pipeline so trailing newlines are preserved too.
    printf "'"
    printf '%s' "$1" | sed "s/'/'\\\\''/g"
    printf "'"
}
sq_die() { printf 'solaris9-qualify: %s\n' "$*" >&2; return 1; }
sq_rsh() { sq_die 'no transport loaded'; }
sq_rscp() { sq_die 'no copy transport loaded'; }
sq_gen_token() {
    local rand
    rand=$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n') || return 1
    [ "${#rand}" = 32 ] || return 1
    printf '%s-%s-%s' "$(date -u +%Y%m%dT%H%M%SZ)" "$$" "$rand"
}
sq_archive_source() {
    local commit=$1 out_tar=$2 extract_dir
    commit=$(git rev-parse --verify --end-of-options "${commit}^{commit}") || return 1
    extract_dir=$(mktemp -d) || return 1
    if ! (set -o pipefail; git archive --format=tar "$commit" | (cd "$extract_dir" && tar -xf -)); then
        rm -rf "$extract_dir"
        sq_die 'source archive/extraction failed'; return 1
    fi
    if ! COPYFILE_DISABLE=1 tar --format=ustar -cf "$out_tar" -C "$extract_dir" .; then
        rm -rf "$extract_dir"; return 1
    fi
    rm -rf "$extract_dir"
}

# The directory itself is the mutex used by manual owners, too. An old,
# empty, malformed or unreadable coordinator.lock is held, never migrated.
sq_acquire_lock() {
    local lock=$1 token=$2 label=$3
    sq_rsh 20 "mkdir $(sq_quote_remote "$lock")" || {
        sq_die 'rig ownership unavailable; existing lock left untouched'; return 1;
    }
    if ! sq_rsh 20 "printf '%s\n' $(sq_quote_remote "$token") > $(sq_quote_remote "$lock/token") && printf '%s\n' $(sq_quote_remote "$label") > $(sq_quote_remote "$lock/owner.txt")"; then
        sq_die 'ownership write uncertain; lock retained for inspection'; return 1
    fi
}
sq_release_lock() {
    local lock=$1 token=$2
    # One remote command, comparing the entire separate token file. Labels
    # never authorize release. Unexpected files prevent rmdir, not rm -rf.
    sq_rsh 20 "printf '%s\n' $(sq_quote_remote "$token") | cmp - $(sq_quote_remote "$lock/token") && rm $(sq_quote_remote "$lock/token") $(sq_quote_remote "$lock/owner.txt") && rmdir $(sq_quote_remote "$lock")"
}

# Reserve the whole namespace BEFORE copying anything. Stdout is exactly
# relative ISO path + SHA256; all diagnostics use stderr.
sq_stage_iso() {
    local local_tar=$1 rig_dir=$2 run_id=$3 space local_gz local_hash remote_hash out hash
    space="qualify-$run_id"
    sq_rsh 20 "mkdir $(sq_quote_remote "$rig_dir/$space")" || return 1
    local_gz=$(mktemp) || return 1
    if ! gzip -c "$local_tar" > "$local_gz"; then rm -f "$local_gz"; return 1; fi
    local_hash=$(shasum -a 256 "$local_gz") || { rm -f "$local_gz"; return 1; }
    local_hash=${local_hash%% *}
    if ! sq_rscp "$local_gz" "$rig_dir/$space/source.tar.gz"; then
        rm -f "$local_gz"; return 1
    fi
    rm -f "$local_gz"
    remote_hash=$(sq_rsh 20 "sha256sum $(sq_quote_remote "$rig_dir/$space/source.tar.gz")") || return 1
    [ "${remote_hash%% *}" = "$local_hash" ] || { sq_die 'staged archive hash mismatch'; return 1; }
    if ! out=$(sq_rsh 90 "cd $(sq_quote_remote "$rig_dir/$space") && mkdir tree && tar -xzf source.tar.gz -C tree && mkisofs -r -J -o source.iso tree > mkisofs.log 2>&1 && sha256sum source.iso"); then
        sq_die "ISO staging failed; retained $space for diagnosis"; return 1
    fi
    hash=${out%% *}
    [[ "$hash" =~ ^[0-9a-f]{64}$ ]] || { sq_die 'invalid ISO digest'; return 1; }
    printf '%s\n%s\n' "$space/source.iso" "$hash"
}

# A fresh per-call frame distinguishes output from echoed commands, CRs,
# prompts, old replies and the console helper's own completion markers.
# The guest command runs in ksh -c: exit/error never exits the login shell.
sq_guest() {
    local rig=$1 phase=$2 command=$3 nonce script raw parsed
    nonce=$(sq_gen_token) || return 1
    script="SQ_NONCE=$nonce; SQ_PHASE=$phase; printf '\nSQ_BEGIN_%s\n' \"\$SQ_NONCE\"; /bin/ksh -c $(sq_quote_remote "$command"); sq_rc=\$?; printf '\nSQ_END_%s:%s\n' \"\$SQ_NONCE\" \"\$sq_rc\""
    raw=$(sq_rsh 60 "cd $(sq_quote_remote "$rig") && python3 console.py $(sq_quote_remote "$script")") || {
        sq_die "guest $phase transport/completion uncertain"; return 1;
    }
    parsed=$(printf '%s\n' "$raw" | python3 "$sq_protocol_dir/solaris9-protocol.py" frame "$nonce") || return 1
    printf '%s\n' "$parsed"
}

# /etc/mnttab's second field is the exact mount point. Require a readable,
# parseable table containing /; missing/failed queries cannot imply unmounted.
sq_unmount() {
    local rig=$1 out query command
    query='awk '\''NF && NF < 2 {bad=1} $2 == "/" {root=1} $2 == "/mnt" {mounted=1} END {if (bad || !root) exit 2; print mounted ? "mounted" : "clear"}'\'' /etc/mnttab'
    command="state=\$($query) || exit 1; if [ \"\$state\" = mounted ]; then umount /mnt || exit 1; fi; state=\$($query) || exit 1; [ \"\$state\" = clear ] || exit 1; printf 'SQ_UNMOUNTED\n'"
    out=$(sq_guest "$rig" unmount "$command") || return 1
    [ "$out" = SQ_UNMOUNTED ] || return 1
}
sq_swap_media() {
    local rig=$1 drive=$2 iso=$3 out change
    [[ "$drive" =~ ^[a-zA-Z0-9_.-]+$ ]] || return 1
    sq_unmount "$rig" || return 1
    sq_rsh 30 "cd $(sq_quote_remote "$rig") && python3 mon.py $(sq_quote_remote "eject $drive")" >&2 || return 1
    out=$(sq_rsh 30 "cd $(sq_quote_remote "$rig") && python3 mon.py 'info block'") || return 1
    printf '%s\n' "$out" | python3 "$sq_protocol_dir/solaris9-protocol.py" block "$drive" --empty || return 1
    change=$(python3 -c 'import json,sys; print("change " + sys.argv[1] + " json:" + json.dumps({"driver":"raw","read-only":True,"file":{"driver":"file","filename":sys.argv[2],"locking":"off"}},separators=(",",":")))' "$drive" "$rig/$iso") || return 1
    sq_rsh 30 "cd $(sq_quote_remote "$rig") && python3 mon.py $(sq_quote_remote "$change")" >&2 || return 1
    out=$(sq_rsh 30 "cd $(sq_quote_remote "$rig") && python3 mon.py 'info block'") || return 1
    printf '%s\n' "$out" | python3 "$sq_protocol_dir/solaris9-protocol.py" block "$drive" "$rig/$iso"
}
sq_mount_and_extract() {
    local rig=$1 dev=$2 dir=$3 command
    # find and cpio are separate commands with separately propagated status.
    # List and log are inside the exclusively created source directory.
    command="mount -F hsfs -o ro $(sq_quote_remote "$dev") /mnt || exit 1; mkdir $(sq_quote_remote "$dir") || exit 1; cd /mnt || exit 1; find . -print > $(sq_quote_remote "$dir/.sq-file-list") || exit 1; cpio -pdum $(sq_quote_remote "$dir") < $(sq_quote_remote "$dir/.sq-file-list") > $(sq_quote_remote "$dir/.sq-copy.log") 2>&1 || exit 1; printf 'SQ_EXTRACTED\n'"
    local out
    out=$(sq_guest "$rig" extract "$command") || return 1
    [ "$out" = SQ_EXTRACTED ] || return 1
}
sq_start_build() {
    local rig=$1 dir=$2 log=$3 marker=$4 token=$5 command job out
    # Final marker is atomic and bound to this run. No background stdin is
    # inherited from the console. Its publication is the job's last mutation.
    job="/bin/ksh tools/solaris9-build.sh > $(sq_quote_remote "$log") 2>&1; rc=\$?; printf '%s:%s\n' $(sq_quote_remote "$token") \"\$rc\" > $(sq_quote_remote "$marker.tmp") && mv $(sq_quote_remote "$marker.tmp") $(sq_quote_remote "$marker")"
    command="SQ_RUN_TOKEN=$(sq_quote_remote "$token"); cd $(sq_quote_remote "$dir") || exit 1; [ ! -e $(sq_quote_remote "$marker") ] && [ ! -e $(sq_quote_remote "$marker.tmp") ] || exit 1; nohup /bin/ksh -c $(sq_quote_remote "$job") </dev/null >/dev/null 2>&1 & pid=\$!; printf 'SQ_PID=%s\n' \"\$pid\""
    out=$(sq_guest "$rig" start "$command") || return 1
    [[ "$out" =~ ^SQ_PID=([1-9][0-9]*)$ ]] || return 1
    printf '%s\n' "${BASH_REMATCH[1]}"
}
# The deadline and finite attempt budget both bound polling. Transport error,
# missing completion, wrong run, and clock expiry all retain uncertain state.
sq_poll_build() {
    local rig=$1 pid=$2 deadline=$3 interval=$4 marker=$5 token=$6 now tries out command
    [[ "$pid" =~ ^[1-9][0-9]*$ && "$interval" =~ ^[0-9]+$ ]] || return 1
    now=$(date +%s) || return 1
    tries=$(( (deadline - now) / (interval + 1) + 2 ))
    [ "$tries" -le 1802 ] || return 1
    command="SQ_RUN_TOKEN=$(sq_quote_remote "$token"); if kill -0 $pid 2>/dev/null; then printf 'SQ_RUNNING\n'; else [ -f $(sq_quote_remote "$marker") ] || exit 1; printf 'SQ_DONE:'; cat $(sq_quote_remote "$marker") || exit 1; fi"
    while [ "$tries" -gt 0 ]; do
        now=$(date +%s) || return 1
        [ "$now" -lt "$deadline" ] || return 2
        tries=$((tries - 1))
        out=$(sq_guest "$rig" poll "$command") || return 1
        if [[ "$out" =~ ^SQ_DONE:$token:([0-9]+)$ ]]; then
            [ "${BASH_REMATCH[1]}" -le 255 ] || return 1
            printf '%s\n' "${BASH_REMATCH[1]}"; return 0
        fi
        [ "$out" = SQ_RUNNING ] || return 1
        sleep "$interval"
    done
    return 2
}
sq_fetch_transcript() {
    local rig=$1 log=$2 raw=$3 clean=$4 nonce script
    nonce=$(sq_gen_token) || return 1
    script="SQ_NONCE=$nonce; SQ_PHASE=transcript; printf '\nSQ_BEGIN_%s\n' \"\$SQ_NONCE\"; /bin/ksh -c $(sq_quote_remote "cat $(sq_quote_remote "$log")"); sq_rc=\$?; printf '\nSQ_END_%s:%s\n' \"\$SQ_NONCE\" \"\$sq_rc\""
    sq_rsh 60 "cd $(sq_quote_remote "$rig") && python3 console.py $(sq_quote_remote "$script")" > "$raw" 2>&1 || return 1
    python3 "$sq_protocol_dir/solaris9-protocol.py" frame "$nonce" < "$raw" > "$clean"
}
sq_verify_pass() {
    [ "$2" = 0 ] && grep -qx 'SOLARIS9_CANNEDBSD_TEST=PASS' "$1"
}

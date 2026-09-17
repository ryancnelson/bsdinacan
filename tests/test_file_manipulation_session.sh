#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
program=${PROGRAM_PATH:-$(make -s print-program)}

case_dir=$(mktemp -d "${TMPDIR:-/tmp}/cannedbsd-e2e-session.XXXXXX")
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

check_stdin_session() {
    local input="$1"
    local exp_status="$2"
    local exp_stdout="$3"
    local exp_stderr="$4"
    local name="$5"

    printf "%b" "$exp_stdout" > "$case_dir/exp_stdout"
    printf "%b" "$exp_stderr" > "$case_dir/exp_stderr"

    printf "%b" "$input" > "$case_dir/stdin_input"

    set +e
    ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}log_path=$case_dir/asan" \
        "$program" < "$case_dir/stdin_input" > "$case_dir/out" 2> "$case_dir/err"
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

# 1. Full end-to-end lifecycle session exercising all 6 milestone verbs in a single guest session:
#    - create: echo redirect (create /tmp/alpha, /tmp/beta)
#    - list: ls (/tmp)
#    - inspect: cat, head, wc (cat /tmp/alpha, head -n 1 /tmp/alpha, wc -c /tmp/beta)
#    - copy: cp (cp /tmp/alpha /tmp/alpha_bak, cp -r /home/user /tmp/user_copy)
#    - move: mv (mv /tmp/alpha_bak /tmp/alpha_moved)
#    - delete: rm (rm /tmp/beta, rm -r /tmp/user_copy, rm /tmp/alpha /tmp/alpha_moved)
check_case \
    'echo "first line of alpha" > /tmp/alpha; echo "second line of alpha" >> /tmp/alpha; echo "data content for beta" > /tmp/beta; ls /tmp; cat /tmp/alpha; head -n 1 /tmp/alpha; wc -c /tmp/beta; cp /tmp/alpha /tmp/alpha_bak; cp -r /home/user /tmp/user_copy; ls /tmp; cat /tmp/alpha_bak; mv /tmp/alpha_bak /tmp/alpha_moved; ls /tmp; cat /tmp/alpha_moved; rm /tmp/beta; rm -r /tmp/user_copy; ls /tmp; rm /tmp/alpha /tmp/alpha_moved; ls /tmp' \
    0 \
    "beta\nalpha\nfirst line of alpha\nsecond line of alpha\nfirst line of alpha\n22\nuser_copy\nalpha_bak\nbeta\nalpha\nfirst line of alpha\nsecond line of alpha\nalpha_moved\nuser_copy\nbeta\nalpha\nfirst line of alpha\nsecond line of alpha\nalpha_moved\nalpha\n" \
    "" \
    "full lifecycle session through all six file manipulation verbs"

# 2. Pipeline composition joining file manipulation utilities and verifying exit status
check_case \
    'echo "entry1" > /tmp/item1; echo "entry2" > /tmp/item2; ls /tmp | tr a-z A-Z; cp /tmp/item1 /tmp/item_copy; cat /tmp/item_copy | tr a-z A-Z; rm /tmp/item1 /tmp/item2 /tmp/item_copy; ls /tmp' \
    0 \
    "ITEM2\nITEM1\nENTRY1\n" \
    "" \
    "pipeline composition connecting file manipulation verbs"

# 3. Cross-directory hierarchy manipulation (create in subdir, copy to /tmp, inspect, move, delete)
check_case \
    'echo "user secret" > /home/user/secret.txt; cp /home/user/secret.txt /tmp/public.txt; cat /tmp/public.txt; head -n 1 /home/user/secret.txt; mv /tmp/public.txt /tmp/renamed.txt; ls /tmp; rm /tmp/renamed.txt /home/user/secret.txt; ls /tmp; ls /home/user' \
    0 \
    "user secret\nuser secret\nrenamed.txt\n" \
    "" \
    "cross-directory hierarchy manipulation session"

# 4. Status code propagation and error recovery within a single session
check_case \
    'echo file1 > /tmp/f1; cp /tmp/f1 /tmp/f2; echo $?; ls /tmp/nonexistent; echo $?; cat /tmp/f2; echo $?; rm /tmp/f1 /tmp/f2; echo $?' \
    0 \
    "0\n1\nfile1\n0\n0\n" \
    "ls: /tmp/nonexistent: no such file or directory\n" \
    "status code propagation and non-fatal error recovery within session"

# 5. Interactive stdin session driving the full verb suite with prompt assertions
check_stdin_session \
    'echo "hello from stdin" > /tmp/session_file\ncat /tmp/session_file\nhead -n 1 /tmp/session_file\nwc -c /tmp/session_file\ncp /tmp/session_file /tmp/session_copy\nmv /tmp/session_copy /tmp/session_moved\nls /tmp\nrm /tmp/session_file /tmp/session_moved\nls /tmp\n' \
    0 \
    "cannedBSD$ cannedBSD$ hello from stdin\ncannedBSD$ hello from stdin\ncannedBSD$ 17\ncannedBSD$ cannedBSD$ cannedBSD$ session_moved\nsession_file\ncannedBSD$ cannedBSD$ cannedBSD$ " \
    "" \
    "interactive stdin session driving all six verbs"

echo 'file manipulation session behavioral suite passed'

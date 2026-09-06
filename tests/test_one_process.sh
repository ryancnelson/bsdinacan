#!/usr/bin/env bash
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
program=${PROGRAM_PATH:-$(make -s print-program)}

coproc CANNEDBSD_PROCESS { exec "$program"; }
host_pid=$CANNEDBSD_PROCESS_PID
output_fd=${CANNEDBSD_PROCESS[0]}
input_fd=${CANNEDBSD_PROCESS[1]}

cleanup() {
    if kill -0 "$host_pid" 2>/dev/null; then
        kill "$host_pid" 2>/dev/null || true
    fi
    wait "$host_pid" 2>/dev/null || true
}
trap cleanup EXIT

printf 'echo READY; cat | cat\n' >&"$input_fd"
if ! IFS= read -r -t 5 -u "$output_fd" marker ||
        [[ $marker != *READY ]]; then
    printf 'FAIL: internal blocked-pipeline marker was <%s>\n' \
        "${marker-}" >&2
    exit 1
fi
if ! kill -0 "$host_pid" 2>/dev/null; then
    echo 'FAIL: cannedBSD exited before /proc inspection' >&2
    exit 1
fi

shopt -s nullglob
host_thread_paths=("/proc/$host_pid/task/"[0-9]*)
if [[ ${#host_thread_paths[@]} -ne 1 ]]; then
    printf 'FAIL: expected one host thread, found <%s>\n' \
        "${host_thread_paths[*]-}" >&2
    exit 1
fi
host_thread_id=${host_thread_paths[0]##*/}
if [[ $host_thread_id != "$host_pid" ]]; then
    printf 'FAIL: host thread <%s> differs from process <%s>\n' \
        "$host_thread_id" "$host_pid" >&2
    exit 1
fi

host_children=$(<"/proc/$host_pid/task/$host_pid/children")
if [[ -n ${host_children//[[:space:]]/} ]]; then
    printf 'FAIL: internal pipeline created host children <%s>\n' \
        "$host_children" >&2
    exit 1
fi
if [[ $(readlink "/proc/$host_pid/exe") != "$project_dir/${program#./}" &&
      $(readlink "/proc/$host_pid/exe") != "$program" ]]; then
    printf 'FAIL: inspected PID is not %s\n' "$program" >&2
    exit 1
fi

exec {input_fd}>&-
if ! wait "$host_pid"; then
    echo 'FAIL: cannedBSD did not exit cleanly after terminal EOF' >&2
    exit 1
fi
trap - EXIT
printf 'one-process /proc test passed (pid=%s, host_threads=1, host_children=0)\n' \
    "$host_pid"

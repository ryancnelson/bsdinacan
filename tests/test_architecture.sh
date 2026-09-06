#!/usr/bin/env bash
set -euo pipefail

require_pattern() {
    local pattern=$1
    local path=$2
    local description=$3
    if ! rg -q -- "$pattern" "$path"; then
        printf 'missing architecture boundary: %s\n' "$description" >&2
        exit 1
    fi
}

require_pattern 'struct cb_executor_ops' src/internal.h 'executor operation table'
require_pattern '\(\*prepare\)' src/internal.h 'executor prepare operation'
require_pattern '\(\*instance_create\)' src/internal.h 'executor instance-create operation'
require_pattern '\(\*start_or_resume\)' src/internal.h 'executor start-or-resume operation'
require_pattern '\(\*request_termination\)' src/internal.h 'executor termination operation'
require_pattern '\(\*instance_destroy\)' src/internal.h 'executor instance-destroy operation'
require_pattern 'struct cb_vfs_mount_ops' src/internal.h 'VFS mount operation table'
require_pattern 'struct cb_vfs_node_ops' src/internal.h 'VFS node operation table'

if rg -n -- 'task->program->start|program->requested_stack_size|task->context' \
        src/core.c; then
    printf 'native execution detail leaked into portable task core\n' >&2
    exit 1
fi

if rg -n -- 'cb_fs_|struct cb_node|\bfs_root\b' src/core.c; then
    printf 'RAMFS implementation detail leaked into portable task core\n' >&2
    exit 1
fi

printf 'architecture boundary checks passed\n'

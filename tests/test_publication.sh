#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
git rev-parse --is-inside-work-tree >/dev/null

failed=0
excluded=':(exclude)tests/test_publication.sh'

check_pattern()
{
    description=$1
    pattern=$2
    findings=$(git grep -nEI "$pattern" -- . "$excluded" || true)
    if [ -n "$findings" ]; then
        printf 'FAIL: possible %s in tracked files:\n%s\n' \
            "$description" "$findings" >&2
        failed=1
    fi
}

check_pattern 'email address' \
    '[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}'
check_pattern 'secret or private key' \
    '(github_pat_|gh[pousr]_[A-Z0-9_]+|AKIA[A-Z0-9]{16}|BEGIN [A-Z ]*PRIVATE KEY)'
check_pattern 'private network address' \
    '(^|[^0-9])(10\.[0-9]+\.[0-9]+\.[0-9]+|192\.168\.[0-9]+\.[0-9]+|172\.(1[6-9]|2[0-9]|3[01])\.[0-9]+\.[0-9]+|100\.(6[4-9]|[7-9][0-9]|1[01][0-9]|12[0-7])\.[0-9]+\.[0-9]+)([^0-9]|$)'
check_pattern 'private service URL' \
    '(\.ts\.net|\.atlassian\.net)'

home_findings=$(git grep -nE '/(home|Users)/[^/[:space:]]+' -- . "$excluded" || true)
home_findings=$(printf '%s\n' "$home_findings" | grep -v '/home/user' || true)
if [ -n "$home_findings" ]; then
    printf 'FAIL: possible personal home path in tracked files:\n%s\n' \
        "$home_findings" >&2
    failed=1
fi

if [ "$failed" -ne 0 ]; then
    exit 1
fi

echo 'publication hygiene checks passed'

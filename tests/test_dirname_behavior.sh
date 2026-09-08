#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
program=${PROGRAM_PATH:-$(make -s print-program)}

case_dir=$(mktemp -d "${TMPDIR:-/tmp}/cannedbsd-dirname.XXXXXX")
trap 'rm -rf "$case_dir"' EXIT
trap 'exit 1' HUP INT TERM

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

# 1. Ordinary parent paths
output=$("$program" -c 'dirname /tmp/example')
[ "$output" = "/tmp" ] || fail "ordinary path produced <$output>"

output=$("$program" -c 'dirname usr/bin/')
[ "$output" = "usr" ] || fail "trailing slash path produced <$output>"

# 2. Plain names
output=$("$program" -c 'dirname plain')
[ "$output" = "." ] || fail "plain name produced <$output>"

# 3. Root/repeated slashes
output=$("$program" -c 'dirname /')
[ "$output" = "/" ] || fail "root produced <$output>"

output=$("$program" -c 'dirname ////')
[ "$output" = "/" ] || fail "repeated root slashes produced <$output>"

output=$("$program" -c 'dirname /foo////bar')
[ "$output" = "/foo" ] || fail "repeated middle slashes produced <$output>"

# 4. Empty argument
output=$("$program" -c 'dirname ""')
[ "$output" = "." ] || fail "empty argument produced <$output>"

# 5. Zero arguments
set +e
stdout_output=$("$program" -c 'dirname' 2>"$case_dir/stderr-zero")
status=$?
set -e
stderr_output=$(cat "$case_dir/stderr-zero")
rm -f "$case_dir/stderr-zero"
[ -z "$stdout_output" ] || fail "zero arguments produced stdout <$stdout_output>"
case "$stderr_output" in
*"usage: dirname path"*) ;;
*) fail "zero arguments diagnostic was <$stderr_output>" ;;
esac
[ "$status" -eq 1 ] || fail "zero arguments exit status was $status"

# 6. Too many arguments
set +e
stdout_output=$("$program" -c 'dirname a b' 2>"$case_dir/stderr-many")
status=$?
set -e
stderr_output=$(cat "$case_dir/stderr-many")
rm -f "$case_dir/stderr-many"
[ -z "$stdout_output" ] || fail "too many arguments produced stdout <$stdout_output>"
case "$stderr_output" in
*"usage: dirname path"*) ;;
*) fail "too many arguments diagnostic was <$stderr_output>" ;;
esac
[ "$status" -eq 1 ] || fail "too many arguments exit status was $status"

# 7. Invalid options
set +e
stdout_output=$("$program" -c 'dirname -x' 2>"$case_dir/stderr-inv")
status=$?
set -e
stderr_output=$(cat "$case_dir/stderr-inv")
rm -f "$case_dir/stderr-inv"
[ -z "$stdout_output" ] || fail "invalid option produced stdout <$stdout_output>"
case "$stderr_output" in
*"dirname: unknown option -- x"*) ;;
*) fail "invalid option diagnostic was <$stderr_output>" ;;
esac
[ "$status" -eq 1 ] || fail "invalid option exit status was $status"

# 8. -- before a dash-leading path
output=$("$program" -c 'dirname -- -leading/dash')
[ "$output" = "." ] || fail "-- dash-leading path produced <$output>"

# 9. Repeated invocations
output=$("$program" -c 'dirname /foo/bar; dirname /baz/qux')
[ "$output" = "/foo
/baz" ] || fail "repeated invocations produced <$output>"

# 10. Pipeline
output=$("$program" -c 'dirname /a/b/c | cat')
[ "$output" = "/a/b" ] || fail "pipeline produced <$output>"

echo 'dirname behavioral matrix passed'

#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
program=${PROGRAM_PATH:-$(make -s print-program)}

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

# Named lookup.
output=$("$program" -c 'printenv PATH')
[ "$output" = "/bin" ] || fail "named lookup produced <$output>"

# Missing variable: no output, exit status 1.
set +e
output=$("$program" -c 'printenv NOPE' 2>/dev/null)
status=$?
set -e
[ -z "$output" ] || fail "missing lookup produced output <$output>"
[ "$status" -eq 1 ] || fail "missing lookup exit status was $status"

# Empty-valued variable: prints an empty line.
output=$("$program" -c 'export EMPTYVAR=; printenv EMPTYVAR')
[ "$output" = "" ] || fail "empty-valued lookup produced <$output>"

# Enumerate all: every boot-environment entry is present.
output=$("$program" -c 'printenv')
case "$output" in
*HOME=/home/user*) ;;
*) fail "enumerate-all missing HOME: <$output>" ;;
esac
case "$output" in
*PATH=/bin*) ;;
*) fail "enumerate-all missing PATH: <$output>" ;;
esac

# '=' in the requested name is a fatal diagnostic, not a lookup.
set +e
stdout_output=$("$program" -c 'printenv FOO=bar' 2>/tmp/printenv_test_stderr_eq)
status=$?
set -e
stderr_output=$(cat /tmp/printenv_test_stderr_eq)
rm -f /tmp/printenv_test_stderr_eq
[ -z "$stdout_output" ] || fail "'=' case produced stdout <$stdout_output>"
# A substring check, not exact equality: an ASan sanitizer build prints its
# own benign, non-deterministic (memory addresses vary per run) banner to
# stderr alongside this diagnostic; only the diagnostic's exact text is
# actually under test here.
case "$stderr_output" in
*"printenv: Invalid environment variable FOO=bar"*) ;;
*) fail "'=' diagnostic was <$stderr_output>" ;;
esac
[ "$status" -eq 1 ] || fail "'=' case exit status was $status"

# Too many arguments: usage diagnostic, no program-name prefix (a bare
# fprintf in the pinned source, not errx).
set +e
stdout_output=$("$program" -c 'printenv a b' 2>/tmp/printenv_test_stderr_usage)
status=$?
set -e
stderr_output=$(cat /tmp/printenv_test_stderr_usage)
rm -f /tmp/printenv_test_stderr_usage
[ -z "$stdout_output" ] || fail "usage case produced stdout <$stdout_output>"
case "$stderr_output" in
*"Usage: printenv [name]"*) ;;
*) fail "usage diagnostic was <$stderr_output>" ;;
esac
[ "$status" -eq 1 ] || fail "usage case exit status was $status"

# Pipeline stage.
output=$("$program" -c 'printenv PATH | cat')
[ "$output" = "/bin" ] || fail "pipeline case produced <$output>"

# Output redirection.
output=$("$program" -c 'printenv PATH > /tmp/printenv_test_redirect; cat /tmp/printenv_test_redirect')
[ "$output" = "/bin" ] || fail "redirection case produced <$output>"

echo 'printenv behavioral matrix passed'

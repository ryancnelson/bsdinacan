#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
mkdir -p build
scratch=$(mktemp -d build/analyzer-canary.XXXXXX)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM

# Exercise the real analyze target, not a separate compiler invocation.
cat > "$scratch/probe.c" <<'EOF'
void analyzer_probe(void)
{
    int *pointer = 0;
    *pointer = 1;
}
EOF
if "${MAKE:-make}" analyze ANALYZE_SOURCES="$scratch/probe.c" > "$scratch/log" 2>&1; then
    echo 'FAIL: analyzer accepted a null dereference' >&2
    exit 1
fi
if ! rg -q 'analyzer-null-dereference' "$scratch/log"; then
    cat "$scratch/log" >&2
    echo 'FAIL: analyze failed without the expected diagnostic' >&2
    exit 1
fi

# An ordinary source must pass the same target and produce an object.
printf 'int analyzer_probe(void) { return 0; }\n' > "$scratch/probe.c"
if ! "${MAKE:-make}" analyze ANALYZE_SOURCES="$scratch/probe.c" > "$scratch/log" 2>&1; then
    cat "$scratch/log" >&2
    exit 1
fi
if [ ! -s "build/analyze/analysis/$scratch/probe.o" ]; then
    echo 'FAIL: analyzer did not compile the probe' >&2
    exit 1
fi
echo 'analyzer positive and negative controls passed'

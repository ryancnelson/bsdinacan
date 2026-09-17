#!/bin/sh
# tests/test_statics_repro.sh - Red reproduction suite for statics state leakage and heap UAF
set -u

PROGRAM="${PROGRAM_PATH:-./build/sanitize/bsdinacan}"

echo "================================================================================"
echo "STATICS-REPRO-01: Pinned Command Statics Vulnerability & Leakage Matrix"
echo "Target Binary: $PROGRAM"
echo "================================================================================"

pass_count=0
fail_count=0

run_repro_test() {
    title="$1"
    script="$2"
    expected_failure_pattern="$3"
    
    echo "--------------------------------------------------------------------------------"
    echo "TEST: $title"
    echo "INPUT: $script"
    
    set +e
    output=$("$PROGRAM" -c "$script" 2>&1)
    status=$?
    set -e
    
    echo "EXIT STATUS: $status"
    echo "OUTPUT:"
    printf '%s\n' "$output" | head -n 30
    
    if printf '%s\n' "$output" | grep -q "$expected_failure_pattern"; then
        echo "RESULT: [REPRODUCED AS EXPECTED] matched pattern: '$expected_failure_pattern'"
        pass_count=$((pass_count + 1))
    else
        echo "RESULT: [UNEXPECTED BEHAVIOR] did not match pattern: '$expected_failure_pattern'"
        fail_count=$((fail_count + 1))
    fi
    echo ""
}

# 1. cat -B heap use-after-free
run_repro_test "cat -B 2048 multi-invocation heap use-after-free" \
    "echo hello > /tmp/cat1; echo world > /tmp/cat2; cat -B 2048 /tmp/cat1; cat -B 2048 /tmp/cat2" \
    "AddressSanitizer: heap-use-after-free"

# 2. rm eval exit status leakage
run_repro_test "rm stale eval exit status leakage on successful second invocation" \
    "rm /tmp/nonexistent; echo rm1_status=\$?; echo data > /tmp/valid; rm /tmp/valid; echo rm2_status=\$?" \
    "rm2_status=1"

# 3. cp flag leakage (vflag persists when second cp has no -v)
run_repro_test "cp persistent vflag leakage on second invocation without -v" \
    "echo 1 > /tmp/cpa; echo 2 > /tmp/cpb; cp -v /tmp/cpa /tmp/cpa_out; cp /tmp/cpb /tmp/cpb_out" \
    "/tmp/cpb -> /tmp/cpb_out"

# 4. mv cross-mount fastcopy reachability check
run_repro_test "mv cross-directory intra-mount rename (fastcopy reachability check)" \
    "echo test > /tmp/mva; mv /tmp/mva /home/user/mvb; mv /home/user/mvb /tmp/mvc; cat /tmp/mvc" \
    "test"

echo "================================================================================"
echo "STATICS-REPRO SUMMARY: $pass_count reproduced, $fail_count unexpected"
echo "================================================================================"

#!/bin/sh
set -eu

PROGRAM="${PROGRAM:-./build/bsdinacan}"

run_session() {
    local title="$1"
    local script="$2"
    echo "================================================================================"
    echo "TEST SESSION: $title"
    echo "================================================================================"
    echo "INPUT SCRIPT:"
    printf '%s\n' "$script"
    echo "--------------------------------------------------------------------------------"
    
    set +e
    output=$("$PROGRAM" -c "$script" 2>&1)
    status=$?
    set -e
    
    echo "EXIT STATUS: $status"
    echo "OUTPUT (combined stdout & stderr):"
    printf '%s\n' "$output"
    echo ""
}

echo "Starting Exploratory Milestone Re-Measurement on $(uname -a)"
echo "Target Binary: $PROGRAM"
echo ""

# 1. cat -n and cat -b
run_session "CAT-01 / FORMAT-01: cat -n and cat -b line numbering" \
'echo "first line" > /tmp/f.txt; echo "" >> /tmp/f.txt; echo "third line" >> /tmp/f.txt; echo "" >> /tmp/f.txt; echo "fifth line" >> /tmp/f.txt; echo "=== plain cat ==="; cat /tmp/f.txt; echo "=== cat -n ==="; cat -n /tmp/f.txt; echo "=== cat -b ==="; cat -b /tmp/f.txt; echo "=== cat -s ==="; cat -s /tmp/f.txt; echo "=== cat -ns ==="; cat -ns /tmp/f.txt'

# 2. wc multi-file and multi-invocation statics isolation
run_session "WC-02: wc multi-file totals and multi-invocation statics isolation" \
'echo "line one alpha" > /tmp/w1; echo "line two beta" >> /tmp/w1; echo "line three gamma" >> /tmp/w1; echo "first delta" > /tmp/w2; echo "second epsilon" >> /tmp/w2; echo "=== wc w1 ==="; wc /tmp/w1; echo "=== wc w2 ==="; wc /tmp/w2; echo "=== wc w1 w2 (Invocation 1) ==="; wc /tmp/w1 /tmp/w2; echo "=== wc w1 w2 (Invocation 2 - checking statics reset) ==="; wc /tmp/w1 /tmp/w2; echo "=== wc -l w1 w2 (Invocation 1) ==="; wc -l /tmp/w1 /tmp/w2; echo "=== wc -l w1 w2 (Invocation 2) ==="; wc -l /tmp/w1 /tmp/w2; echo "=== wc -w w1 w2 (Invocation 1) ==="; wc -w /tmp/w1 /tmp/w2; echo "=== wc -w w1 w2 (Invocation 2) ==="; wc -w /tmp/w1 /tmp/w2; echo "=== wc -c w1 w2 (Invocation 1) ==="; wc -c /tmp/w1 /tmp/w2; echo "=== wc -c w1 w2 (Invocation 2) ==="; wc -c /tmp/w1 /tmp/w2; echo "=== wc -L w1 w2 (Invocation 1) ==="; wc -L /tmp/w1 /tmp/w2; echo "=== wc -L w1 w2 (Invocation 2) ==="; wc -L /tmp/w1 /tmp/w2'

# 3. mkdir -p deep hierarchy, recursive rm -r, ls verification
run_session "MKDIR-CMD-01 & VFS-05: deep mkdir -p, populate, rm -r, ls verification" \
'mkdir -p /tmp/nest/l1/l2/l3; echo "top level" > /tmp/nest/top.txt; echo "deep data" > /tmp/nest/l1/l2/l3/leaf.txt; ls /tmp/nest; ls /tmp/nest/l1/l2/l3; cat /tmp/nest/l1/l2/l3/leaf.txt; rm -r /tmp/nest; echo "=== ls /tmp after rm -r ==="; ls /tmp; echo "=== ls /tmp/nest (expect error) ==="; ls /tmp/nest; echo "status: $?"'

# 4. echo behavior and netbsdecho retirement check
run_session "ECHO-02: echo behavior and netbsdecho retirement check" \
'echo "=== echo ==="; echo hello world; echo -n hello; echo " post-n"; echo "=== multi -n flags ==="; echo -n -n foo; echo ""; echo "=== netbsdecho retirement (expect command not found) ==="; netbsdecho hello world; echo "status: $?"'

# 5. ls with options (LS-02 held check)
run_session "LS-01 / LS-02: ls with options rejection check" \
'echo "fileA" > /tmp/fileA; echo "=== ls plain ==="; ls /tmp; echo "=== ls -l ==="; ls -l /tmp; echo "status: $?"; echo "=== ls -a ==="; ls -a /tmp; echo "status: $?"; echo "=== ls -F ==="; ls -F /tmp; echo "status: $?"'

# 6. Non-existent path diagnostics and error propagation
run_session "DIAGNOSTICS: non-existent path diagnostics and exit statuses across utilities" \
'echo "=== cat nonexistent ==="; cat /tmp/none; echo "status: $?"; echo "=== wc nonexistent ==="; wc /tmp/none; echo "status: $?"; echo "=== head nonexistent ==="; head /tmp/none; echo "status: $?"; echo "=== cp nonexistent ==="; cp /tmp/none /tmp/dest; echo "status: $?"; echo "=== mv nonexistent ==="; mv /tmp/none /tmp/dest; echo "status: $?"; echo "=== rm nonexistent ==="; rm /tmp/none; echo "status: $?"; echo "=== mkdir missing parent ==="; mkdir /tmp/no_parent/child; echo "status: $?"; echo "=== mkdir -p missing parent ==="; mkdir -p /tmp/no_parent/child; echo "status: $?"; ls /tmp/no_parent; echo "status: $?"'

# 7. Pipeline chaining and multi-command flow
run_session "PIPELINES: pipeline composition, subdirectory redirection, repeated multi-command flow" \
'mkdir -p /tmp/pipeline_test/in /tmp/pipeline_test/out; echo "alpha bravo" > /tmp/pipeline_test/in/data.txt; echo "charlie delta echo" >> /tmp/pipeline_test/in/data.txt; cat /tmp/pipeline_test/in/data.txt | tr a-z A-Z; cat /tmp/pipeline_test/in/data.txt | wc -w; cat /tmp/pipeline_test/in/data.txt | wc -l; cat /tmp/pipeline_test/in/data.txt | wc -c; cp -r /tmp/pipeline_test/in /tmp/pipeline_test/backup; ls /tmp/pipeline_test/backup; cat /tmp/pipeline_test/backup/data.txt; mv /tmp/pipeline_test/backup /tmp/pipeline_test/archived; ls /tmp/pipeline_test; rm -r /tmp/pipeline_test; ls /tmp'

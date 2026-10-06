# ASPRINTF-01 — bounded allocating formatting for uniq

Base: a4048b90df5188858515d65899fccc02cd663a96. Shared symbol owner:
cb_libc_asprintf and private stdio declarations; no FILE layout or ABI change.
Implementation follows the reviewed literal/%c/%s design, with separate checked
count/emission passes, task allocation, NULL on failure, int return bounds and
explicit rejection of unsupported conversions. The independent snprintf parser
is unchanged. No full uniq import or command acceptance is claimed.

## Evidence

The original worker observed an ordinary-source missing-interface red. After
transport failures the coordinator preserved and finished its dirty tree, then
independently reproduced the baseline red from git archive a4048b9: a private
stdio probe calling asprintf(&p, "-%c%s", 'f', "3") fails strict C99 compilation
with undeclared asprintf. This second run is a baseline reproduction, not a claim
that the coordinator wrote the test before the worker implementation.

The unchanged obsolete fixture was verified against the downloaded pinned
full-source hash and exact function body; see UPSTREAM.md for provenance.
Ordinary-source cases exercise real obsolete conversion and stop conditions,
embedded NUL output, empty allocation, errno preservation and rejection.
A production-parser unit harness checks exact allocation size and canaries,
long text, allocation failure, actual obsolete NULL failure handling, unsupported
formats and synthetic INT_MAX/SIZE_MAX additions without huge allocations.

Linux check-asprintf passed in isolated network-disabled Docker image
tribblix-woodpecker-agent:3.18.0 on biggie. The macOS-host unit link is unsupported
by the existing bare-symbol NetBSD strcmp/memcpy adapters; that setup failure
was not treated as product red and was not patched around. Linux is the normal
runtime-test host. Twenty-two Mac protocol tests and capacity checking passed:
83 records, 2113 transcript bytes plus NUL = 2114 bytes. The previous 82 records
are retained; stale evidence is rejected. These are host checks, not guest tests.

Actual-task lifecycle and shared acceptance checks passed on Linux with
build/test_core --err and --mac-acceptance. They observe peer rejection, stable
bytes across yield, exit cleanup before reap, failed exec retention and successful
exec reclamation. An initial test incorrectly expected free to change errno;
free deliberately preserves it, so ownership rejection is observed through the
existing raw release API instead. This was a test correction, not product red.

Full exact
Woodpecker and independent review remain required. Fresh Mac and Solaris runs
are pending; the reserved guest was not operated. No runtime merge is claimed.

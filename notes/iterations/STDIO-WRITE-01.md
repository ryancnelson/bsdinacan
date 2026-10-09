# STDIO-WRITE-01 — task-owned writable streams

Base: 0ea2461d2eaacfc6924b683471a8cf863ddc8963. This candidate implements
w/wb fopen, dynamic fprintf/fwrite, stream error state, flush and close using the
existing task-owned FILE list. No public ABI layout or version changes. Shared
FILE implementation ownership remains serialized ahead of FGETLN-01.

Wrappers are allocated before destructive opens. Output handles partial writes,
rejects zero progress and oversized callback results, and keeps errors on the
actual stream even when its descriptor equals stdout or stderr. Close unlinks
before its callback and invalidates the wrapper even on failure. Failed exec
retains wrappers; successful exec reclaims wrappers while retaining inherited
non-CLOEXEC descriptors. Exit cleanup occurs before reap.

## Validation

The original worker reported a baseline red: the new ordinary-source writable
file probe returns 180 against the previous implementation and the test suite
fails at fileprobe. The coordinator did not independently reproduce that red.
After worker transport failures, the coordinator preserved and finished the
existing worktree. Review found a fwrite precedence regression: checking a
closed standard descriptor too early hid overflow and NULL-buffer errors. The
coordinator restored the previous ordering and added callback-free regression
cases for EOVERFLOW, EINVAL and EBADF.

The finalized implementation passed make -j4 LDLIBS=-lucontext test in isolated,
network-disabled tribblix-woodpecker-agent:3.18.0 on biggie, under
/tmp/stdio-finish.5KDkeA (focused.log). This includes the production-code unit
harness, native lifecycle tests and portable file probes. Twenty-three Mac
protocol tests and build parity/output capacity checks passed locally. The
acceptance suite retains its previous 83 cases and adds fileprobe write-probe:
84 records, 2140 transcript bytes plus NUL = 2141 bytes.

These are host tests, not Mac guest acceptance. Preliminary review found no
additional blocker after the precedence correction; immutable final review and
exact-commit Woodpecker checks are still required. Fresh Mac and Solaris
qualification remain pending. The user's reserved guest was not operated and
no runtime merge is claimed.

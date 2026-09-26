# Combined TERM-03 and SIG-01 qualification candidate

Base is freshly fetched main `7b8fa78` (the additional commits since `7b1a152`
change only BACKLOG.md). This isolated integration explicitly merges reviewed
TERM-03 `fe082eb` and SIG-01
`ea667d6e1bc7e3d544649e26563dcfb27845fcc6`. Their original worktrees and newer
remote worker commits are preserved; the newer commits are not substituted for
these reviewed candidates.

TERM-03 merged cleanly. SIG-01 had six additive wiring conflicts: Makefile,
Mac CMake sources, Mac main declarations, guest transcript, native test entry
points, and protocol-test expectations. Resolutions retain both helpers,
both focused test selections, both Mac invocations and both source lists.
No runtime policy decision or semantic implementation change was needed.

The combined transcript contains **69 distinct PASS records and 1736 UTF-8
bytes**, including its title, ALL PASS line and final newline. Its initial
records are contexts, terminalengine, consolewrite, teestate and interrupts.
Removing only terminalengine and interrupts reproduces the base's complete
67-record expected transcript byte for byte. Existing command cases remain
unchanged. The terminal engine uses no kernel registry, and signal scenarios
use isolated six-program kernels; the fixed program capacity remains 64.

The integrated core.c, executor.c, internal.h and signal_probe.c are byte
identical to reviewed SIG-01. terminal.c, terminal.h and
terminal_engine_probe.c are byte identical to reviewed TERM-03. Both features'
original tests and historical notes remain intact. Their individual 68-record
counts describe their separate feature candidates, not this combined artifact.

Local protocol verification passed all 18 tests. Publication hygiene and
`git diff --check` passed. A direct composition check verified record uniqueness,
exact byte count, the unchanged base transcript, reviewed implementation hashes,
and capacity 64. This is integration validation; no new pre-implementation red
or guest execution is claimed.

The local Docker daemon is stopped. The combined full Linux gate therefore
runs in the existing build host's pinned
`tribblix-woodpecker-agent:3.18.0` image,
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`,
using `make LDLIBS=-lucontext SANITIZE_CC=clang ci`. Its result and the exact
feature Woodpecker pipeline are reported in the handoff after execution.

This is a candidate for independent integration review and new combined Mac
and native Solaris qualification. Neither feature's standalone CI or historical
guest evidence qualifies this combined artifact. No main merge, UI operation,
shared guest/rig operation or acceptance rollup is performed in this branch.

# LS-P-OPERAND-01 — qualify full-path operand acceptance

- Assigned base: `49bd1bf4e920261199c0341c0d61f6328ba952b7`.
- Branch: `work/LS-P-OPERAND-01`, isolated sibling worktree.
- Scope: investigation and optional diagnostic tools only; no runtime change.
- Hypothesis: `-P` directory-child coverage does not establish file, `-d`
  directory or mixed root-operand acceptance. Root-preview path/name aliasing
  predicts duplicate text, but the expected NetBSD prefix needs independent
  evidence before defining a repair.

## Measured candidate output

Pinned Linux agent image:
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`,
network disabled, isolated temporary source copy. Built only
`make -j4 LDLIBS=-lucontext build/bsdinacan`, then ran
`python3 tests/characterize_ls_p.py build/bsdinacan`.

Each case starts a fresh cannedBSD instance. Fixture setup creates
`/tmp/p/file`, `/tmp/p/dir/child`, then changes directory to `/tmp/p`.
The tool independently checks the fixture's complete contents and status.
Every command below returned status **0**, with **empty stderr**. Escaped
stdout is exact, including final newlines:

| Command | stdout |
| --- | --- |
| `ls -1P file` | `file/file\n` |
| `ls -1P /tmp/p/file` | `/tmp/p/file//tmp/p/file\n` |
| `ls -1Pd dir` | `dir/dir\n` |
| `ls -1Pd /tmp/p/dir` | `/tmp/p/dir//tmp/p/dir\n` |
| `ls -1Pd file dir /tmp/p/file /tmp/p/dir` | `/tmp/p/dir//tmp/p/dir\n/tmp/p/file//tmp/p/file\ndir/dir\nfile/file\n` |
| `ls -1P file /tmp/p/dir` | `file/file\n\n/tmp/p/dir:\n/tmp/p/dir/child\n` |
| `ls -1P dir` | `dir/child\n` |
| `ls -1P /tmp/p/dir` | `/tmp/p/dir/child\n` |

`PROGRAM_PATH=build/bsdinacan tests/test_ls_behavior.sh` passed, preserving
all existing behavior cases, including directory-child `-P` coverage.
The new optional reporter checks status/stderr and those established child
expectations. It deliberately records root output without accepting duplicated
text as the desired contract. It is not added to `make test`.

## Reference boundary and reproducible experiment

Reference revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`.
Sources read:

- [fts.c](https://github.com/NetBSD/src/blob/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/lib/libc/gen/fts.c)
- [fts.h](https://github.com/NetBSD/src/blob/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/include/fts.h)
- [fts(3)](https://github.com/NetBSD/src/blob/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/lib/libc/gen/fts.3)

Pinned ls.c calls `display(NULL, fts_children(...))` before its first
`fts_read`. Its pinned `printpath` joins `fts_path`, a slash and `fts_name`.
The reference FTS allocates the shared path buffer without initializing its
bytes; initial `fts_children` returns the root list without loading a path.
The manual only guarantees a terminated path for the latest `fts_read` result.
Consequently source reading cannot supply a deterministic initial root prefix.

`tests/ls_p_reference.py` is a Linux-host experiment, **not a native NetBSD ls
execution**. It verifies these downloaded source hashes before compiling:

- fts.c: `24157b290edfbbdb9fa63e5c05de7632b7427fae1eb465deba4d6af1b21be55c`
- fts.h: `8d2d89cefd95f03004a09f77d3b3c0c11dffd1b804379a3010972cf9edb8fa62`

Download those two files from `raw.githubusercontent.com/NetBSD/src/` at the
revision above into a temporary reference directory, then run:

```
python3 tests/ls_p_reference.py /path/to/reference-directory
```

The tool performs no network access. It compiles unchanged fts.c with small
namespace/cdefs/config adapters and a reallocarr implementation, and extracts
printpath directly from this repository's unchanged pinned print.c. It uses
`FTS_PHYSICAL | FTS_NOCHDIR`, no comparator, and calls only the initial
`fts_open`/`fts_children` preview before `fts_close`. Thus it does not reproduce
all ls options, sorting or traversal, or the native NetBSD allocator.

A controlled realloc adapter initializes a newly allocated block to a terminated
`ALLOC-A` or `ALLOC-B` string. The harness checks that every root's path still
points to the shared buffer and still has exactly that prefix. The two runs
both exited **0**, with empty stderr. Exact output from the first standalone
experiment (absolute fixture path `/work`):

```
ALLOC-A/file
ALLOC-A//work/file
ALLOC-A/dir
ALLOC-A//work/dir
```

The second run had identical suffixes with `ALLOC-B` instead of `ALLOC-A`.
The checked-in tool reproduced this with its generated temporary absolute
fixture paths, asserting all bytes, status and stderr. These controlled bytes
show initial allocation dependence; neither prefix is an expected user-visible
NetBSD result. This avoids interpreting accidentally zero-filled allocation
storage as a meaningful empty-prefix contract.

## Acceptance decision and limits

Qualify the `-P` matrix as **directory children tested; file and `-d` directory
root operands unresolved**. Mixed operands inherit the unresolved file prefix.
The measured duplication confirms the candidate limitation; it does not prove
which stable replacement matches native pinned NetBSD behavior. No conventional
single-path expectation is asserted, and no red-to-green runtime fix is claimed.
A future behavior change needs an explicit root-display contract or reliable
native reference evidence, plus ordinary review, CI and guest acceptance.

No pinned sources or shared symbols changed. No guest/UI operations occurred.
Two setup-only failures were corrected: the minimal Linux image needed a
cdefs adapter for the standalone reference; the reporter initially used shell
`&&`, which this command parser does not support. Neither is product red
evidence. Final focused commands above passed; `git diff --check` passed.
Exact pushed-commit CI and review results are reported in the handoff. Native
NetBSD behavior remains unmeasured. This documentation/host-tool change needs
no new guest execution; outstanding runtime-candidate guest gates remain intact.

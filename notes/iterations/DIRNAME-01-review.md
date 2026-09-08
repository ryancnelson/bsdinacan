# DIRNAME-01: three bounded test corrections

Base `4a44e1472fbbae4b357d61c1ebcfecd894fe3e49`; branch
`work/dirname-review`, sibling worktree `bsdinacan-dirname-review`.
The coordinator assigned these corrections while the command worker was
unavailable. No command source, adapter, runtime, ABI, or capacity changed.

- Decode fixed expected `\n` strings with `printf '%b'`, preserving exact file
  comparisons for stdout/stderr and explicit command status checks.
- Require the unchanged command's actual private `printf`, `fprintf`, and `err`
  imports instead of nonexistent `cb_libc_puts`; reject host printf/fprintf too.
- Restore `dirname /a/b/c | cat`, requiring `/a/b\n`, empty stderr and status 0.

## Focused evidence and remaining dependency

Before changing the helper, invoking its extracted `check_case` against
`/bin/sh -c 'printf "/tmp\n"'` failed: the expectation ended in literal bytes
`5c 6e` while actual output ended in `0a`. After the change, isolated helper
checks passed for a newline, separate stdout/stderr with exit 7, and a pipeline.
Negative checks confirmed rejection of an extra newline, unexpected stderr,
and an incorrect exit status. These test the comparison helper with the host
shell; they are not cannedBSD command acceptance.

`/bin/sh -n tests/test_dirname_behavior.sh`,
`bash -n tests/test_netbsd_source.sh`, and `git diff --check` passed.
An isolated strict compilation of the unchanged command in the Linux agent
image confirmed imports of `cb_libc_printf`, `cb_libc_fprintf`, and `cb_libc_err`,
with no `cb_libc_puts`. It still imports unprefixed `dirname` because the separate
private libc prerequisite is not present in this branch. The boundary test
continues to reject that import; no replacement header or stub was introduced.

The full runtime suite and exact combined-commit CI/guest acceptance remain
pending integration of the separately assigned real libc dirname implementation.
Neither isolated object compilation nor helper tests establish that the command
runs inside cannedBSD or System 7.

## Combined integration candidate

The integration includes the real task-owned dirname libc and C-only locale
prerequisites. The command object is `dirname_command.o` /
`cb_dirname_command`; the separately imported libc routine retains
`netbsd_dirname.o` / `cb_dirname`, preventing build-name collisions. Command
header dependencies include private `libgen.h` and `locale.h`. The checked
Mac probe registration is present, and six direct command cases cover a
path, repeated root slashes, a dash-leading path, an invalid option with
status 1, a pipeline, and an empty argument. The complete shared transcript
now has 28 records including contexts. Host tests still assert stdout and
stderr separately; the Mac host captures their combined console output.

Combined validation and exact-artifact guest acceptance are pending. No
guest completion is claimed by this integration preparation.

The first combined candidate's Woodpecker Linux sanitizer run reached the
new exact-stderr matrix and exposed the existing ASan ucontext warning in
the command capture. The matrix now sends ASan's own reports to its private
temporary directory and accepts only the exact known three-line
`__asan_handle_no_return` warning. Any other report fails, including on
cases expecting status 1; stdout and command stderr remain byte-exact.

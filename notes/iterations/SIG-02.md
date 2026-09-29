# SIG-02 — private cooperative SIGINT veneer

Base: `7a8b0fb` (fresh origin/main), branch `work/SIG-02`.
Candidate implementation; independent review and exact CI pending.
Mac and native Solaris acceptance remain coordinator gates. No guest was used.

## Contract

Private `signal(SIGINT, SIG_DFL/SIG_IGN)` now calls the existing cooperative
interrupt setter. It returns the actual previous disposition and preserves
errno on success. Other signal numbers return SIG_ERR/EINVAL; arbitrary handlers
and SIG_ERR passed as a handler return SIG_ERR/ENOSYS without changing pending
or disposition state. SIGINFO progress handlers remain unsupported.

SIG_DFL is a null function pointer. SIG_IGN and SIG_ERR are distinct private
function identities, compared but never invoked; no integer/function-pointer
conversion is used. The ordinary-source probe checks their distinctness and
round trips through the function call boundary. Both Linux and Retro68 builds are wired to compile
and link that same source, and the existing Mac interrupts probe will execute it.
No new transcript record or larger capture buffer is needed.

The public API appends `set_interrupt(int, int *)`. It transports integer
CB_INTERRUPT_DEFAULT/IGNORE dispositions, not function pointers, and returns
zero or -1/errno. Its implementation delegates to SIG-01's existing
`cb_task_set_interrupt`; core pending delivery, task ownership, spawn and exec
semantics are unchanged. The veneer checks the actual field end before reading
its callback. Old/short allocations and a missing callback fail ENOSYS.

This branch starts from main before the held LS-02 API addition. The coordinator
must preserve accepted append order when integrating its wall-clock tail with
this setter; the two branches must not silently assign different callbacks to
the same accepted ABI offset.

## Falsifiable check

A new ordinary-source probe first called `signal(SIGINT, (sig_t)0)` against
the unchanged existing stub, expecting default disposition and preserved EPIPE.
After registering the shell needed to boot the fixture, this command failed
with `signal libc failed status 11` and exit 1:

```sh
make LDLIBS=-lucontext build/test_core
./build/test_core --signal-libc
```

That was the behavioral red. An initial fixture setup failure before base
program registration was corrected and is not counted as red evidence.

The implemented probe then passed `--signal-libc`; after adding the complete
portable matrix, the existing `./build/test_core --signals` returned zero.
Both observations used the pinned Linux image
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`
with libucontext, an isolated source export and network disabled.

## Coverage and limits

- Ordinary private-header calls check default/ignore previous values, repeated
  ignore, marker identities, success errno preservation, invalid numbers and
  unsupported handlers (which must not execute).
- A real queued request survives rejected requests, genuinely short API
  allocations (mandatory prefix, absent field, incomplete field) and a full
  table with null callback. Selecting ignore clears it; a later request while
  ignored is discarded and a yield still resumes normally.
- Spawn inherits ignore independently of the parent's later default reset.
  Separate peers exercise actual default delivery (status 130, no code after
  yield), ignored delivery, failed exec and successful exec preserving ignore.
- A native executor with its cooperative capability cleared makes the veneer
  return ENOSYS through the real runtime callback.
- Existing SIG-01 tests still own immediate allocation/descriptor cleanup,
  blocked-state wakeups, absent executor prefixes and both exec-transition
  request boundaries. This veneer does not duplicate or weaken those tests.
- No host signals, keyboard integration, asynchronous handlers, kill(), masks,
  restart semantics or tee import are provided.

Shared symbols: cb_libc_signal; new cb_libc_sig_ignore/cb_libc_sig_error;
cb_api_v1.set_interrupt and core api_set_interrupt; existing integer disposition
constants moved unchanged from internal.h to abi.h. No pinned sources changed.

## Final evidence

Exact Woodpecker ci/mac68k/mac-automation, independent review and guest
qualification are pending. A green workflow alone does not establish a clean
sanitizer log; inspect the final log. Required runtime Solaris qualification
is not waived by this prerequisite's small scope.

# MAC-06: wait for local matcher readiness before boot

- Base: `5374fad`, freshly fetched `origin/main`.
- Branch/worktree: `work/MAC-06`, sibling `bsdinacan-MAC-06`.
- Hypothesis: waiting for an explicit import-complete handshake prevents a hung
  matcher from booting a guest that automation cannot control.

## Diagnosis and red

On 2026-09-07 the coordinator's run timed out after 45 seconds without a first
match. Sampling the existing Python helper showed `read` within nested module
imports. `lsof` identified a NumPy bytecode file; `ls -lO` marked it
`compressed,dataless`. A separate `import cv2` with Python faulthandler timed out
inside importlib `get_data`. Sampling Hammerspoon showed its main thread blocked
in Lua `io_open` / `fopen`; its old scratch JSON was also `dataless`. These files
were in iCloud-managed Documents. No guest process was controlled for diagnosis.

`lua tests/test_mac_runner_startup.lua` initially failed:
`must not boot before matcher readiness`. This is the behavioral red check;
the import hang is supporting field evidence, not a deterministic unit test.

## Implementation and green

The persistent matcher optionally emits a protocol-1 readiness message after
imports. The runner waits before starting Basilisk, rejects an invalid handshake,
and times out startup after 30 seconds by default. The guest timeout begins at
boot. Finishing closes and terminates only the owned matcher helper; it never
terminates Basilisk. Existing mouse and menu delivery is unchanged.

- `lua tests/test_mac_runner_startup.lua`: pass; silence and malformed readiness
  cannot launch a guest, valid readiness launches once, timeout cleans up helper.
- `python tests/test_mac_image_match.py`: nine pass, including readiness before
  stdin/EOF and continued multiple-request processing.
- `luac -p platform/mac68k/automation/run.lua`: pass.
- A fresh pinned environment on local cache storage imported OpenCV in 13.38
  seconds initially and 0.099 seconds subsequently. The old evicted environment
  was left intact; no live exported files or disks were moved.
- Full `make LDLIBS=-lucontext SANITIZE_CC=clang ci`: pass in the documented
  Alpine 3.22 environment (optimized, sanitizer, analyzer, architecture checks).
- Automation tests: pass in Python 3.14-slim with pinned packages and Lua 5.4.
- Exact-commit Woodpecker and coordinator guest trial: pending feature push.

## Bounds

This fixes launch ordering and helper lifetime, not the OS's cloud hydration.
The README requires local, non-synchronized storage for source, templates,
configuration, Python environment, guest state, and scratch data. Lua timers
cannot interrupt a filesystem call blocked on Hammerspoon's main thread.
The synthetic Lua test makes no event-delivery or live-guest claim. The
coordinator owns the serialized exact-artifact guest acceptance and main merge.

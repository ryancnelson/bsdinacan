# System 7 / 68K host

This experimental native application uses the portable cannedBSD core with a
Classic Mac Toolbox backend. Linux and Solaris sources are not modified.
The first target is a 68000-compatible build running in Basilisk II under
System 7.5.3, with an 8 MiB application partition.

## Build and run

Woodpecker builds this target for every push and pull request, using the pinned
Retro68 image in `.woodpecker/mac68k.yml`. Retrieve `CannedBSD.tar.gz`, its
`SHA256SUMS`, and `commit.txt` from the dedicated runner's artifact store.
Verify with `sha256sum -c SHA256SUMS`, then extract the archive.

Copy `CannedBSD.APPL` and both matching `.rsrc/CannedBSD.APPL` and
`.finf/CannedBSD.APPL` files into Basilisk II's host shared folder. An empty
data fork is normal: the executable lives in the resource fork. Alternatively,
use the included HFS disk image or decode `CannedBSD.bin` with a MacBinary-aware
utility. Launch CannedBSD in the guest.

For local diagnostics, `bash platform/mac68k/build.sh` uses the same pinned
Docker toolchain. The authoritative build remains the Woodpecker artifact.

## Acceptance check

At startup the application exercises two independent stacks with 512 total
child yields, then checks seven shell cases with exact output and exit
status assertions: pipelines and redirection, `wc`, a three-stage pipeline,
append, shell status, working directory, and a nonzero exit.
It writes results to `Unix:cannedbsd-result.txt`. Require `ALL PASS` in a newly
written result file for the artifact under test; remove any old result before
launch. A successful run then opens an interactive shell in the same window.
Type `exit`, press Command-Q, or close the window to quit.

## Optional unattended evidence mode

Normal launches remain interactive. To request an unattended guest run, create
`cannedbsd-autorun.txt` in the shared host directory before boot. Also precreate
empty writable `cannedbsd-result.txt`, `cannedbsd-screen.pict`, and
`cannedbsd-done.txt`. Native Basilisk II's shared-folder backend does not
reliably create absent files. Use fresh or emptied outputs for every run.

The app clears done before testing, writes and flushes the detailed result,
redraws and captures its actual window pixels as a PICT file, then writes and
flushes `PASS` or `FAIL` plus a newline to done. Both test outcomes exit normally
after evidence succeeds. Any evidence error leaves the app open and withholds
completion. Remove the autorun marker to restore the interactive shell.

An external controller must require both an exact done token and the app's
normal exit before requesting a guest shutdown. A token alone is insufficient:
a failed final flush can leave visible bytes while the app remains open. The
app itself never requests System 7 shutdown. The PICT contains a standard
512-byte file header and the captured content area of the app window.

## Host implementation and limits

Memory uses non-relocatable Toolbox pointers. The application heap is expanded
before entering any coroutine stack. Cooperative stack switching saves
the 68K C ABI's callee-saved registers, including the A5 application world,
and the System 7 `StkLowPt` stack-sniffer state. The VBL sniffer is disabled
only while a private heap-backed stack is active, then restored on the original
application stack. Toolbox calls requested by a task are serviced synchronously
on that original stack before the same task resumes. The context acceptance
check holds both private stacks across VBL ticks and exercises memory requests.
The executable uses Retro68's single-segment mode, so lazy code loading cannot
invoke Toolbox traps on task stacks; CI checks the emitted CODE resources.
The default compiler ABI uses software floating point. The host pumps Toolbox
events on the original scheduler stack and provides line-buffered ASCII input.
TickCount supplies monotonic time; UTC wall time is unavailable and returns
zero as allowed by the host ABI.

This first frontend has a bounded text display, no terminal escape processing,
and no preemption of commands that do not yield. The guest filesystem remains
the core's RAM filesystem. Only the acceptance evidence file uses the shared
host volume. Self-hosted compilation with Symantec C++ 7.0 is a possible later
experiment: https://macintoshgarden.org/apps/symantec-c-70 . It is not part of
this cross-build gate.

# System 7 / 68K host

This experimental native application uses the portable cannedBSD core with a
Classic Mac Toolbox backend. Linux and Solaris sources are not modified.
The first target is a 68000-compatible build running in Basilisk II under
System 7.5.3, with an 8 MiB application partition.

## Build and run

Woodpecker builds this target for every push and pull request, using the pinned
Retro68 image in `.woodpecker/mac68k.yml`. Retrieve `CannedBSD.tar.gz`, its
`SHA256SUMS`, and `commit.txt` from the dedicated runner's artifact store.
Use the host runner below to verify and stage the archive. Prefer the included
HFS disk image: it preserves the application resource fork and Finder metadata
on both native and Docker-hosted Basilisk II. Copying `.APPL` with `.rsrc` and
`.finf` sidecars depends on the emulator's extfs backend and may produce a
non-launchable document. An empty application data fork is normal.

For local diagnostics, `bash platform/mac68k/build.sh` uses the same pinned
Docker toolchain. The authoritative build remains the Woodpecker artifact.

## Acceptance check

At startup the application exercises two independent stacks with 512 total
child yields, then checks ten command cases with exact output and exit
status assertions: pipelines and redirection, `wc`, a three-stage pipeline,
append, shell status, working directory, a nonzero exit, ordinary-source
memory/string boundaries, a quiet unknown getopt option, and truncate/ftruncate
shrink, zero-filled growth, and errors. The command table in
`acceptance_cases.def` also runs unchanged in the Linux test executable.
It writes results to `Unix:cannedbsd-result.txt`. A successful run then opens an
interactive shell in the same window. Type `echo ci | tr a-z A-Z` and observe
`CI`; type `exit` to return to Finder. The runner checks startup results; record
the interactive observation separately in the iteration note.

For the calibrated Hammerspoon driver, see [automation/README.md](automation/README.md).
It uses this staging/evidence protocol and a dedicated desktop applet.

### Serialized host runner

Python 3 is required; slot release additionally requires `lsof`. All workers
must use the same absolute `--state` directory, assigned by the coordinator.
The runner never starts or stops an emulator and never reuses a staged disk.

```sh
python3 platform/mac68k/guest.py stage \
  --artifact /path/to/downloaded-ci-run \
  --commit FULL_40_CHARACTER_COMMIT_SHA \
  --state /path/to/shared-guest-state \
  --boot-seed /path/to/clean-shutdown-System7.dsk \
  --native-template /path/to/current-basilisk-prefs \
  --rom /path/to/Mac.ROM
```

This verifies `commit.txt` and `SHA256SUMS`, reserves the guest slot, and prints
a fresh run directory. It copies the verified HFS application image, optionally
copies the boot seed to `System.dsk`, creates a fresh `shared/` directory with an
empty result placeholder, and records commit, checksums, and staging time in `manifest.json`. The seed stays
untouched. No prior result can survive into the new shared folder. The empty
placeholder exists before the staging timestamp and cannot pass validation. It accommodates
native extfs implementations where opening an existing host file works but
creating the fully qualified result path fails; it must exist before boot. Keep
the seed from a verified clean guest shutdown; a copied damaged disk remains damaged.

For native Basilisk, the optional `--native-template` and `--rom` pair requires
`--boot-seed` and generates `basilisk_prefs` inside the run. It preserves hardware
settings and replaces every disk, export, and ROM entry. Launch with the stable
installed app path (never a temporary AppTranslocation path):

```sh
open -na /path/to/BasiliskII.app --args --config /path/to/run/basilisk_prefs
```

For other setups, with the emulator stopped, configure its `disk` entries to use
this run's `System.dsk` and `CannedBSD.dsk`, and its `extfs` entry to use this run's
`shared/`. Keep the existing ROM and hardware settings. Launch Basilisk II,
open the `CannedBSD` disk, select the application and use Finder's File > Open.
Native computer-use keyboards accept `super+o` for Command-O. Verify the screen
after each action, including any System 7 warning or confirmation dialog.

```sh
python3 platform/mac68k/guest.py check --state /path/to/shared-guest-state
```

Acceptance requires a newly written regular result file with the exact ordered
transcript from `acceptance_cases.def`: eleven named `PASS` lines including
`PASS contexts`, followed by `ALL PASS`. Missing or duplicated probe lines are
rejected. Staging also saves `expected-result.txt` for the Hammerspoon driver. Missing,
stale, or incomplete results fail with a nonzero exit. A passing check writes
`acceptance.json` binding the commit, archive SHA-256, result SHA-256, and times.
A visible `ALL PASS` with a failed file write is useful diagnostic evidence but
does not pass this automated check. Do not transcribe a result file by hand.

Freshness uses the unique shared directory and host modification time. It is
not a guest-authenticated nonce: an operator pointing the emulator at a wrong
application and new shared folder, or manually copying/touching an old result,
can defeat that association. Verify the mounted disk and avoid concurrent guest
instances. Do not move, rename, or delete exported host files while the guest is
running: extfs may retain a stale directory entry and fail the next result write.
Create the fresh share before boot and retain it until shutdown. A future guest
nonce protocol can strengthen this boundary.

After testing, exit CannedBSD. In native Basilisk II, choose BasiliskII > Quit
BasiliskII; inspect the System 7 confirmation and click Shut Down. Finder's
Special > Shut Down is another guest path; inspect and confirm its dialog too.
Verify the guest shutdown completed and the emulator process exited. A
still-running process alone is not a reason to terminate it. Do not kill the emulator or overwrite mounted disks.

```sh
python3 platform/mac68k/guest.py release --state /path/to/shared-guest-state
```

Release refuses while `lsof` reports either staged disk open; it also refuses
when ownership cannot be checked. It frees only the slot and retains every run,
disk, and receipt. `status --state ...` identifies the current owner. Checking
evidence does not release the slot because the guest may still be running.

Native Basilisk II is a supported guest path when a Docker-hosted emulator
fails to boot. A toolchain build or Docker health check does not establish guest
health. Use the same verified HFS artifact and fresh evidence procedure for both.

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

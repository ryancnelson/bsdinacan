# Continuous integration

The canonical release gate is:

```sh
make ci
```

Run it before every push. `.woodpecker/ci.yml` runs the same gate for every push
and pull request. It has no deployment, remote-shell, emulator, scheduled, or
secret-dependent steps.

## What the gate runs

1. Host-only Mac guest-runner protocol tests (checksum, fresh evidence, and slot ownership).
2. Publication-hygiene checks for email addresses, likely credentials, private
   network addresses and service URLs, and personal host home paths.
3. A clean optimized build and the complete test suite.
4. A clean ASan/UBSan build and the same suite.
5. The build-mode regression proving ordinary tests do not reuse sanitizer
   artifacts.
6. A second clean optimized suite.
7. GCC's static analyzer compiling separate objects, the architecture boundary
   scan, and positive/negative controls proving the analyzer diagnoses a known
   null dereference while accepting valid code.

The complete suite includes a Linux `/proc` integration: it holds an internal
three-task shell pipeline open, then proves the running `bsdinacan` application
has one host thread and no host child processes.

The Woodpecker local runner is an Alpine Linux 3.22 image with its build
dependencies baked in: `bash`, `build-base`, `clang20`, `compiler-rt`, `git`,
`libucontext-dev`, `python3`, and `ripgrep`. The project workflow performs no
package installation; it links `libucontext`, selects Clang for the sanitizer
build because Alpine's GCC package omits sanitizer runtimes, and invokes the
canonical gate. These are build dependencies, not cannedBSD runtime APIs. The
shared POSIX backend remains the only source file allowed to use `ucontext`.

The separate Solaris 9 guest gate is `/bin/ksh tools/solaris9-build.sh`, described
in [SOLARIS9.md](SOLARIS9.md). It runs `test-runtime`: the core suite, ordinary
source compilation probes, launcher checks, and acceptance pipelines. Linux
`/proc`, sanitizer, analyzer, and source-provenance checks remain in `make ci`.
The hosted pipeline does not launch the Solaris VM.

## Safety and reproducibility

- CI never prints or consumes credentials.
- CI never names or contacts a private host.
- CI never launches a VM or emulator.
- CI never deploys artifacts.
- The Alpine runner generation and required package set are documented
  explicitly.
- A failing gate blocks release; it is not retried with a different command.

The repository is active in Woodpecker. Pipeline #1 established the GitHub
webhook-to-agent path; every subsequent push must pass the same canonical gate.

## Classic Mac build

`.woodpecker/mac68k.yml` additionally cross-compiles the System 7 application on
every push and pull request. It requires a Docker backend with the `role=retro68`
label and pins the Retro68 toolchain image by digest. The dedicated agent uses
the mandatory label `!role=retro68` so it cannot take unrelated workflows.
The existing local runner and Linux gate remain unchanged.

The runner provides an `/artifacts` volume for build retention. Each pipeline
and source commit gets a directory containing separate run directories, so
reruns retain earlier artifacts. `CannedBSD.tar.gz` includes MacBinary, an HFS
disk image, and the application with its `.rsrc` and `.finf` metadata. It is
accompanied by `commit.txt` and `SHA256SUMS`.

This gate proves the Mac application compiles and packages successfully. Guest
execution is a separate acceptance check: retrieve the CI artifact, verify its
checksum, and run it under System 7 as described in `platform/mac68k/README.md`.
CI does not install the application into an emulator. The Iterate Bot workflow
requires this guest check for the exact artifact from every runtime, libc, VFS,
shell, command, or Mac-host behavior change. The coordinator serializes noVNC
access and records the tested commit and checksum so a successful build cannot
be mistaken for successful System 7 execution.

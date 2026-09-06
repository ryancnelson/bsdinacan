# Continuous integration

The canonical release gate is:

```sh
make ci
```

Run it before every push. `.woodpecker/ci.yml` runs the same gate for every push
and pull request. It has no deployment, remote-shell, emulator, scheduled, or
secret-dependent steps.

## What the gate runs

1. Publication-hygiene checks for email addresses, likely credentials, private
   network addresses and service URLs, and personal host home paths.
2. A clean optimized build and the complete test suite.
3. A clean ASan/UBSan build and the same suite.
4. The build-mode regression proving ordinary tests do not reuse sanitizer
   artifacts.
5. A second clean optimized suite.
6. GCC's static analyzer and the architecture boundary scan.

The Woodpecker local runner is an Alpine Linux 3.22 image with its build
dependencies baked in: `bash`, `build-base`, `clang20`, `compiler-rt`, `git`,
`libucontext-dev`, `python3`, and `ripgrep`. The project workflow performs no
package installation; it links `libucontext`, selects Clang for the sanitizer
build because Alpine's GCC package omits sanitizer runtimes, and invokes the
canonical gate. These are build dependencies, not cannedBSD runtime APIs. The
Linux backend remains the only source file allowed to use `ucontext`.

## Safety and reproducibility

- CI never prints or consumes credentials.
- CI never names or contacts a private host.
- CI never launches a VM or emulator.
- CI never deploys artifacts.
- The Alpine runner generation and required package set are documented
  explicitly.
- A failing gate blocks release; it is not retried with a different command.

Woodpecker repository activation is an external administrative step. Before
activation, inspect `.woodpecker/ci.yml`, run a harmless push proof, and verify
that the only webhook receiver is the intended current Woodpecker service.

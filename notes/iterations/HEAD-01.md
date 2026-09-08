# HEAD-01: accepted unchanged NetBSD head

Accepted and merged at `e65e36fc0444193445b8a304f6e818d2768d5b62` after
independent clean review, all three exact Woodpecker #324 workflows, and fresh
System 7 guest acceptance. This milestone imports the real command unchanged;
its owned descriptor, private namespace and tests supply the adaptation.

## Source and dependency history

Pinned source SHA256:
`33745355975529ef5b33256578bee822dae8e80fbb27dc615a1761385d7eb18a`,
NetBSD revision `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`, path
`usr.bin/head/head.c`. Source bytes and license remain unchanged.
An earlier compile against actual private headers at accepted main `22aeb5a`
with C99/full warnings failed solely at head.c:159 for the undeclared fwrite
call. That was measured feasibility, not a behavioral red or a successful
build. [FWRITE-01-review](FWRITE-01-review.md) records the subsequently accepted
prerequisite and its separate guest evidence.

## Real command fixtures and regression controls

`tests/head_probe.c` runs 21 internal cases through the unchanged command.
It checks the literal default ten-line fixture, leading obsolete numeric argv,
option ordering and diagnostics, q/v header precedence, multiple files and
missing-file continuation, literal `-` handling, empty and binary input,
short byte input, a real pipe producer, invalid/overflow counts, and the
65538-byte boundary. The large output is a RAMFS file checked for exact length
and content in bounded chunks; no console capture buffer was enlarged.

Independent disposable Linux validation exported the exact accepted commit:

```sh
make LDLIBS=-lucontext build/test_core
build/test_core --head
```

The pinned `tribblix-woodpecker-agent:3.18.0` container (Alpine 3.22.5, GCC 14.2,
network disabled) passed the original baseline, exit 0. Two independently
mutated copies of only upstream head.c then failed the unchanged probe:

- Changing only `linecnt = 10` to 9 failed case 0, headprobe status 20,
  test executable exit 1. Mutated source SHA256:
  `2f292125e2a50fd7d132ee7bf40f58db163533fd734875f52c58c5c3e09093b7`.
- Breaking after the first positive byte block failed case 13, headprobe
  status 33, test executable exit 1: 65536 output bytes do not satisfy the
  required 65538-byte file. Mutated source SHA256:
  `239fe90ab12344ce1219600d976251bc80924121097e814ce89d05b2da417d25`.

Restoring the pinned source, rebuilding, and rerunning returned exit 0.
All four builds succeeded; no source-hash gate was run on deliberate mutants.
These are after-implementation regression controls, not preimplementation TDD.
The unchanged probe SHA256 was
`422947d370994ea0c5e345c3f8f7bba2221ad9cdbe0f977498e1f96d12fdcf02`.
The exported source archive SHA256 was
`3ee1e4472b8e9d2a1e6e9628c00a60f9dfe8d4b8723aca6d7adb1b073a0e1fac`.

## Stack boundary and exact guest acceptance

The head descriptor requests **128 KiB**, leaving global task defaults unchanged.
The exact private Mac build uses the checked-in `-Os` configuration and
`-fstack-usage`; its published `CannedBSD.tar.gz` contains
`head-stack-usage.txt`. The report records `head` at **65580 bytes** and
`cb_head_main` at **84 bytes**, each `dynamic,bounded`. These are isolated
compiler frame reports, not measured call-chain peak, minimum safe stack size,
or arithmetic headroom. Successful execution with the selected budget proves
the tested workload, not a universal stack bound. The earlier
[PORT32-01 audit](PORT32-01.md) remains historical at `731b447` and explicitly
retracts unsupported frame arithmetic from exploratory host-header builds.

Exact archive SHA256:
`8ade221adcee608ac01fd69efaa30d72c8a923e5d8899c455ffb0335865c609c`.
Fresh guest `run-8u8ke41l` produced **65 PASS records plus ALL PASS**, including
the single headprobe record covering all 21 internal cases, in **23.1268449
seconds**. The coordinator inspected the fresh transcript and screenshot,
verified app closure, normal shutdown, closed disks, and released the slot.
The exact #324 ci, mac68k and mac-automation workflows all passed before this
acceptance; no local compile was substituted for the published artifact.

## Remaining characterization

The pinned source treats input EOF and input error alike: line mode stops on
getc EOF and byte mode stops on zero fread without checking input ferror.
HEAD-02 is assigned to characterize actual injected read/output failures and
repeated recovery through this unchanged command. A zero exit after an injected
read error must be documented as the upstream limitation, not successful input
processing. No new signal behavior or source patch is implied by HEAD-01.

# TEE-01 portable probe in Mac acceptance

Candidate integration only. Base is the independently reviewed 79-record ls
sprint `f66aab307930fa2b048c1b511335fbb88c065657`, exact #518 all-three CI
success, plus reviewed tee implementation `fce35e5`. Neither this integration
nor its ls prerequisite is declared guest-qualified or merged to main.

Mac main now calls the actual command probe on the host/root stack after the
cooperative interrupt probe, before any ordinary command-case kernel exists.
The helper owns and destroys its kernel and tests actual tee bytes, files,
short/error I/O, repeated/interleaved lists, cleanup before reap and genuine
interrupt delivery. A nonzero helper result stops acceptance and produces FAIL.
This is separate from the older synthetic tee-state probe, which is retained.

The expected transcript gains PASS teecommand: 80 total records, 2050 bytes
plus NUL. The derived buffer capacity is 2051; the former fixed 2048 allocation
would not fit. Every previous case remains. Both prior 69- and 79-record
transcripts are rejected by the host protocol before any acceptance receipt.
Observed local checks: all 20 host protocol tests, build parity and the output
length executable passed. These host checks do not prove guest execution.

Exact combined ci/mac68k/mac-automation, independent wiring review and fresh
serialized Mac guest acceptance remain required. The current Mac is locked
and Ryan owns the interactive session; no guest input or slot change occurred.
Current native Solaris qualification also remains outstanding.

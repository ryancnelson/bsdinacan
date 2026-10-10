# COLLATE-C-01 — C/POSIX collation candidate

Based on accepted main 5865993. Adds cb_libc_strcoll and the ordinary private
string.h mapping, using the existing unsigned-byte strcmp implementation.
The supported locale remains C/POSIX only. No host locale, task API, allocation,
public ABI change, case folding or broader locale support is introduced.

Ordinary-source tests cover equality, empty strings, prefixes, ASCII ordering,
unsigned high bytes, errno preservation and rejected locale changes. Native tests
also call the helper before kernel creation and after kernel destruction.

Before implementation, strict freestanding compilation of the added ordinary
probe failed on undeclared strcoll (exit 1). After implementation it passed.
A fresh isolated Linux container build passed test_core --locale and
--mac-acceptance. All 19 Mac protocol tests and build-parity/output checks passed.
The new guest case yields 70 records and 1742 bytes including the terminator,
within the existing 2048-byte result buffer. The stale 69-record transcript is
explicitly rejected.

Exact-commit full CI, independent review, fresh guest acceptance and Solaris
qualification remain pending. This candidate is not a runtime merge or a claim
that comm now compiles; strcasecmp and LINE_MAX remain separate prerequisites.

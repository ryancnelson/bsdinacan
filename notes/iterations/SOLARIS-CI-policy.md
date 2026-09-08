# Solaris testing and CI policy update

Date: 2026-09-08. Base: 3e02a2c. User-assigned coordinator documentation change.

BACKLOG.md now prioritizes SOLARIS-01 integration and its dependent SOLARIS-02
CI harness. AGENTS.md, notes/CI.md and CURRENT-STATE.md change in the same commit
so workers see consistent instructions. Existing assigned tasks are preserved.
The local Solaris port is published separately as reference, not merged over
newer runtime work. Native current-main acceptance and automated CI remain pending.

This changes no runtime behavior. Behavioral red/green and Mac/Solaris guest
acceptance are not applicable; publication, whitespace and exact-commit hosted
workflow results are the validation boundary. The policy requires fresh exact
source identity and exit-status evidence and distinguishes bootstrap exceptions
from completed cross-platform qualification.

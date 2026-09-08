# HEAD-01: unchanged NetBSD head import

Coordinator-owned source/fixture staging in an isolated worktree. Final
acceptance remains blocked on the reviewed fwrite integration and complete
native/Mac command tests. No working utility or safe stack margin is claimed
by this initial staging.

The pinned source SHA256 is
`33745355975529ef5b33256578bee822dae8e80fbb27dc615a1761385d7eb18a` at
NetBSD revision `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`. Source bytes and
license remain unchanged. A compile against actual project private headers at
accepted main22aeb5a with C99/full warnings failed solely at head.c159 for the
undeclared fwrite call. This is measured source feasibility, not a behavioral
red test or a successful build. The explicit module requests128KiB for head,
leaving global task stack defaults unchanged. Exact private build/frame checks
and the reviewed full fixtures remain required before accepting that budget.

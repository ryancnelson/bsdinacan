# TEE-STATE-01

## Implementation
Implemented the synthetic `cb_tee_state_probe` to prove behavioral red with `cb_native_executor` sharing a global list, and then verified execution isolation by wrapping `cb_native_executor` inside `tee_wrapper_ops` sidecar.

Tests explicitly inject errors across the kernel execution paths by systematically testing allocation failures (+1 to maximum allocations) during setup. Failed exec checks explicitly test `waitpid` propagation and `get_errno`. The sidecar properly unwinds across kernel failures preserving allocations correctly. No tee public API, TLS, or signal stubs were introduced as dictated by `TEE-STATE-01-design`.

## Mac Guest Wiring
The synthetic probe is wired to the System 7 guest runner exactly behind the console write probe without replacing any records or expanding max `capacity64`. Both Linux and Mac guests execute the exact same memory isolation tests.

## CI Results
Woodpecker CI Pipeline 364 pending.
Solaris acceptance is pending `SOLARIS-01` integration; no modifications were made to the shared Solaris rig.

Awaiting independent coordinator review for guest slot.

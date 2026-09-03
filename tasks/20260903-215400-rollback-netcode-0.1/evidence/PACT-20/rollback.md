# PACT-20 Rollback Session Evidence

## Contract

- Objective: independent peer-local prediction, snapshots, earliest-frame correction, resimulation, confirmation, and exact metrics.
- Gap: deterministic simulation had no temporal history or correction mechanism.
- Guardrail: no peer state copying, no forced final overwrite, fixed capacity, typed failure beyond 120 frames, no per-tick allocation.
- Done when: ring wrap, zero-latency, prediction matches/mismatches, dirty coalescing, window boundary, event-effect replacement, convergence, and move-only ownership pass.

## RED

```text
tests/unit/rollback_session_test.cpp(3): fatal error C1083:
cannot open include file 'rollback_lab/netcode/frame_ring.hpp'
```

## Root-cause packet: initial stack overflow

The first GREEN implementation compiled, then exited consistently with Windows `-1073741571` (`0xC00000FD`). Per-case flush instrumentation showed the last passing case was earliest-dirty coalescing and the next test created two sessions. The session embedded 256 full `WorldState` snapshots by value, so two locals exceeded the default Windows test-thread stack before the first statement executed.

Single hypothesis and fix: allocate the entire fixed-capacity session storage once behind a private `std::unique_ptr<Storage>`. The ring capacities remain compile-time fixed; construction performs one RAII allocation and tick/resimulation paths perform none. The original multi-session tests then passed without changing their behavioral assertions.

## GREEN and regression

```text
Compiler: MSVC 19.51.36248.0
Configuration: msvc-debug, warning-as-error
CTest: 1/1 passed
Internal cases: 19 tests, 0 failures
Feature commit: 79cd6d14f5eaef7aed7501fe6b19a9319368706f
```

Observed invariants include: 60-frame zero-latency run with 0 predictions/rollbacks; 10 matching late predictions with 0 rollbacks; out-of-order corrections for frames 7 and 5 coalesced into one rollback from 5 with 7 resimulated frames; depth 120 accepted and 121 rejected; projectile ID did not duplicate during resimulation; two independently owned sessions with 4/7-frame delays converged at confirmed boundary 180 and equal final hash.


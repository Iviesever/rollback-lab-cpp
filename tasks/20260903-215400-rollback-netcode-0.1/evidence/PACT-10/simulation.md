# PACT-10 Deterministic Simulation Evidence

## Contract

- Objective: deterministic 60 Hz-compatible, integer-only arena transition and canonical hash.
- Gap: only version constants existed.
- Guardrail: no clock, OS state, `random_device`, floating-point canonical fields, unbounded entities, or signed-overflow dependence.
- Done when: boundaries, capacity, combat, frame math, seeded inputs, byte format, repeated hash history, and a pinned golden hash pass under warning-as-error.

## RED

After adding `tests/unit/core_simulation_test.cpp`, MSVC failed for the intended missing production API:

```text
tests/unit/core_simulation_test.cpp(3): fatal error C1083:
cannot open include file 'rollback_lab/core/checked_math.hpp'
```

## GREEN and regression

```text
Compiler: MSVC 19.51.36248.0
Configuration: msvc-debug
Warnings: /W4 /WX /permissive- /EHsc
CTest: 1/1 passed
Internal cases: 11 tests, 0 failures
Golden: seed 0xC0FFEE, 600-frame final hash 0xA263C92E66A0AA27
Forbidden-token audit: no chrono, random_device, float, or double in core/simulation sources
Commit: d173fdc2db55c465543b07a070743ca5b03b58e5
```

Covered cases: uint32 frame wrap/distance/ring index, int32 overflow/underflow, fixed array/state types, random-access PCG32 inputs, repeated 600-frame hash history, wrong frame rejection, conflicting directions, arena clamp, full projectile capacity, damage/defeat/score/respawn, and explicit little-endian canonical prefix.

Cross-compiler reproduction of the same pinned golden hash is deliberately retained as PACT-70 evidence.


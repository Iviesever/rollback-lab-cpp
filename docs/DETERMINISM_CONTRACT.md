# Determinism Contract

## What is deterministic

Given the same simulation version, initial state, ordered input pairs, and simulation variant, every canonical boundary serializes to the same bytes and FNV-1a 64-bit hash on supported MSVC, Clang, and GCC builds.

The in-process transport also guarantees identical packet scheduling, delivery order, loss/duplication/reorder decisions, metrics, canonical report identity, replay, and trace for the same transport configuration and PCG32 seed.

## Canonical numeric model

- Tick rate: 60 frames/s.
- Coordinate scale: 1 world unit = 1,024 integer subunits.
- Arena: 1,024 × 576 world units.
- Canonical positions/velocities: signed 32-bit integers.
- Intermediate addition: signed 64-bit, range checked before narrowing.
- Players: exactly 2 in stable ID order A, B.
- Projectiles: exactly 64 slots; active iteration is stable slot/ID order.
- No ordinary `float` or `double` is stored in `WorldState`.
- Unsigned frame arithmetic handles wrap; order is valid within the half-range ambiguity bound.

## Forbidden canonical inputs

`simulation` and `core` do not read system time, `std::chrono::now()`, OS state, environment variables, files, sockets, random devices, thread IDs, or scheduling. PCG32 is explicit, versioned, and used for scripted input/transport outside the canonical transition.

## Canonical bytes and hash

`serialize_canonical` writes the simulation version, boundary frame, both player records, next projectile ID, and every projectile slot field by field in little-endian order. It never hashes C++ object padding or addresses. FNV-1a is chosen for a stable compact identity, not cryptographic collision resistance or cheat prevention.

The checked-in golden for scenario seed `0xC0FFEE` after 600 frames is:

```text
0xA263C92E66A0AA27
```

## What is not deterministic

OS process scheduling, localhost UDP arrival order/timing, benchmark duration, PID, port choice, compiler/OS labels, and wall-clock time are observations. They are reported but do not enter canonical state. Report `identity_digest` excludes timing and machine/build labels so deterministic content can be compared across builds.

Localhost success does not prove WAN behavior, NAT traversal, congestion control, security, or production readiness.


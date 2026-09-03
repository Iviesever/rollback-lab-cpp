# Product Contract

## Goal

Deliver `Iviesever/rollback-lab-cpp`, a public C++23 lab that demonstrates deterministic fixed-tick rollback netcode end to end, including independent peers, hostile-network emulation, real UDP subprocesses, replay, desync diagnosis, reports, and a self-contained trace viewer.

## Acceptance contract

1. Canonical simulation is a pure `FrameNumber + Inputs -> WorldState` transition at 60 ticks/s and contains no floating-point fields or wall-clock/random/OS reads.
2. Each peer owns separate world state, histories, snapshot ring, rollback decisions, metrics, and hash history.
3. Last-known remote input prediction only rolls back on a mismatch, beginning at the earliest affected frame and restoring the snapshot at that frame boundary.
4. Fixed-capacity histories fail closed with typed errors when input is too old or queues overflow according to the documented policy.
5. Seeded in-process transport deterministically models latency, jitter, loss, reordering, duplication, burst loss, bounded bandwidth/queues, and maximum age.
6. A field-by-field versioned binary codec validates magic, version, type, identities, sequence/ack, bounded redundant inputs, optional hashes, payload length, and CRC32C-style accidental-corruption integrity.
7. Versioned replay recreates per-frame/checkpoint hashes and rejects corruption or unsupported versions.
8. Confirmed-frame hash exchange finds and records the earliest bounded divergent frame without classifying speculative differences as desync.
9. `udp-demo` launches relay and two peer subprocesses on dynamically reserved localhost UDP ports, enforces watchdogs, reaps every child, and verifies convergence plus replay.
10. Canonical JSON and trace outputs use stable field/key order. The self-contained HTML viewer renders real trace data and passes browser console, interaction, timeline-marker, and responsive-layout checks.
11. Local fresh evidence covers MSVC Debug/Release, GCC or Clang, sanitizer support, CTest, 10,000 seeds, fuzz smoke, UDP, replay, desync, viewer, benchmarks, and clean rebuild.
12. Documentation and Draft PR report exact SHAs, commands, counts, limitations, AI authorship, and artifacts.

## Fixed technical constraints

- C++23; CMake is authoritative.
- Canonical integer coordinate scale: 1 world unit = 1,024 subunits.
- Position range: `[-1,048,576, +1,048,576]` subunits; velocity range: `[-16,384, +16,384]` subunits/tick.
- Arithmetic uses checked widening in `std::int64_t`; canonical values are bounded `std::int32_t` and never rely on signed overflow.
- Arena: 1,024 x 576 world units; two stable player IDs; at most 64 projectiles; projectile order is ascending stable ID.
- Snapshot/history capacity: 256 frames; maximum accepted rollback depth: 120 frames.
- Protocol byte order: little-endian, encoded field by field; maximum packet size: 1,200 bytes; redundant input window: at most 32 frames.
- Canonical hash: documented 64-bit FNV-1a over the versioned canonical serialization; it is not cryptographic or anti-cheat protection.
- Integrity: CRC-32/ISO-HDLC for accidental corruption only; it does not authenticate packets.
- Seeded PRNG: PCG32 with a fixed algorithm/version recorded in every report.
- No detached threads; all resources use RAII and bounded timeout/join paths.
- No runtime dependency on Node or a CDN for generated viewers.

## Failure model

Typed status categories include invalid argument, capacity exceeded, stale frame, rollback window exceeded, invalid protocol, unsupported version/type, truncated/corrupt data, duplicate/stale sequence, queue overflow, timeout, child failure, replay mismatch, and desync. CLI failures return non-zero and still emit bounded diagnostics when possible.

## Scope guard

The authoritative goal objective's strict non-goals and mutation prohibitions apply. In particular, no other repository may be modified; no convergence may be manufactured by copying peer state; no PR merge, tag, release, remote-branch deletion, or credential persistence is authorized.


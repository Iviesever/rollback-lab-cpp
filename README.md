# Rollback Lab C++

> A deterministic C++23 rollback-netcode laboratory with a real two-process UDP path.

```text
Fixed-Tick Simulation
    +
Local Input
    +
Predicted Remote Input
    +
Late Packet
    ↓
Restore Snapshot
    ↓
Resimulate
    ↓
Confirmed State Convergence
```

The project is being delivered on `feat/rollback-netcode-0.1`. This baseline locks the build and verification contract before implementation; results and generated artifacts will only be claimed after they exist and have fresh evidence.

## Quick start

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

Planned user-facing commands on the feature branch:

```text
rollback_lab simulate --scenario default
rollback_lab replay <file>
rollback_lab udp-demo
rollback_lab verify
rollback_lab benchmark
rollback_lab compare <report-a> <report-b>
```

## Contract

- 60 Hz canonical simulation with integer-only state.
- Independent peer state, last-known-input prediction, bounded snapshots, earliest-frame rollback, and deterministic resimulation.
- Versioned binary protocol, deterministic adverse-network emulator, strict replay, confirmed-frame desync diagnosis, and real localhost UDP subprocesses.
- A self-contained viewer generated from production trace data.

This is an educational portfolio laboratory, not production networking, matchmaking, NAT traversal, encryption, anti-cheat, or a game engine integration.

## AI assistance

The user specified the career goal, product direction, constraints, deadline, and acceptance criteria. Codex GPT-5.6 Sol is responsible for architecture refinement, implementation, tests, debugging, protocol, UDP, viewer, documentation, and packaging. The user did not hand-write delivery code in this work session. This is an AI-assisted engineering project and must not be represented as independently hand-written work.

See the persistent contract and progress records under `tasks/20260903-215400-rollback-netcode-0.1/`.


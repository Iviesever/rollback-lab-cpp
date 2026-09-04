# Stable C ABI v1

`include/rollback_lab/c_api/rollback_lab_c.h` is a genuine C11 interface. The SDK
DLL owns every C++ object, allocation and exception; the caller receives opaque
`rl_session*` and `rl_live*` handles, status values and copied plain-data records.
No STL, Unreal type, reference, virtual table or allocator crosses the boundary.

## Version and layout

API version is 1; SDK package is 0.2.0 Candidate. Simulation, protocol and replay
remain version 1. `rl_get_version` reports these, capabilities and the compiled
source SHA. Each input/output structure starts with `uint32_t api_version` and
`uint32_t struct_size`. Initialize both before calling. Version 1 requires the
exact size; incompatible additions require a new ABI version rather than reading
past a caller's allocation. Reserved input bytes must be zero.

The supported layout is x64, little endian, natural alignment up to 8 bytes.
Status and peer values use uint32 constants; no C enum size is assumed. Positions
and velocities are signed integer copies in the core's 1/1024 coordinate scale.
The C/C++ layout checks verify field offsets, alignment and complete sizes:
session config 24, input 24, player/projectile record 32 each, world snapshot
2144, metrics 48, version info 88, live config 72, live correction 4312 bytes.
Do not surround the public header with a packing pragma.

## Ownership and failure

Create and destroy each handle through the SDK exactly once. Handles, including
queries and destruction, are affine to their creating thread. Concurrent access
is unsupported. A wrong-thread call returns `RL_WRONG_THREAD` before inspecting
mutable session state. Invalid/stale/forged pointers are outside the C contract;
the SDK cannot validate arbitrary memory supplied as an opaque pointer.

Every exported function catches unexpected exceptions and returns
`RL_INTERNAL_FAILURE`. After an internal failure, release the driver and sessions
and restart. Typed failures distinguish null/invalid arguments, ABI version,
structure size, peer, frame, stale confirmed history, rollback-window overflow,
insufficient output buffer, capacity, timeout, desync, replay mismatch and I/O.

Session APIs support advance, remote input ingestion, batched correction, copied
snapshot, confirmation, metrics, current hash, retained confirmed hash and
canonical-byte serialization. The wrapper rejects all future inputs before they
could overwrite retained history; callers queue future inputs until their frame
is current. Existing core
120-frame fail-closed rollback semantics remain unchanged.

## Live driver

Create two unused handles, A and B, then call `rl_live_create` to borrow them.
While borrowed, session mutation or destruction returns `RL_BORROWED`; snapshot,
hash and metrics queries still work. Destroy the driver before either session.
Borrowing is published only after all construction allocations succeed.

The live driver calls the same incremental production `LiveScenario` used by
`run_seeded_scenario`. It contains no second simulation, prediction, packet codec,
transport scheduler, snapshot or hash implementation. One call to `rl_live_step`
advances one logical transport tick. Scripted mode uses existing seeded input;
interactive mode replaces only A's buttons for the current frame. All inputs
still flow to the other peer through the original versioned packet transport.

The bounded end sequence is the configured tail retransmission interval followed
by 32 drain ticks and replay verification. The C facade caps simulation at 36000
frames, tail at 4096 ticks, latency/jitter at 10000 ticks, queues at 65536 packets
and 64 MiB, bandwidth at 64 MiB per tick, and packet age at 100000 ticks. These
facade bounds do not change the standalone C++ API contract.

`rl_live_get_correction` copies a correction's actual pre/post worlds plus its
earliest frame and resimulation depth. A performed flag describes the most recent
step's real event. The renderer stores that event for its transient ghost/flash.
Predicted world differences are expected; only confirmed hash observations may
produce a desync marker. Controlled desync completes with an explicit report
whose success field is false; it is not treated as convergence.
Final success also requires the peers' final hash to equal the canonical Replay
hash. Two identical faulty variants cannot manufacture successful parity merely
by agreeing with each other; that case returns `RL_REPLAY_MISMATCH`.

## Artifacts and parity

Two-stage copy calls accept null/zero capacity to return required bytes and
`RL_BUFFER_TOO_SMALL`. No bytes are written to an insufficient buffer. JSON
required sizes include the terminal NUL; binary replay sizes do not. Trace can be
queried after a failed transport step; report/replay require completed logical
execution, including a controlled desync demonstration.

`rollback_lab_c_demo <scenario-seed> <transport-seed> <frames> <existing-out-dir>`
is compiled as C11 and generates report, trace and Replay v1 through the DLL.
The ordinary CLI accepts the same full-width seeds through `--scenario-seed` and
`--transport-seed`. Default CLI behavior and the checked 0.1 sample identity are
unchanged. Tests compare 128 delayed-input session scenarios and 128 complete
packet-driven scenarios across direct C++ and C ABI.

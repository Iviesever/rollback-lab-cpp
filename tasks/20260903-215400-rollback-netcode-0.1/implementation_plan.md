# Rollback Netcode 0.1 — Technical Blueprint

## Delivery slices

### PACT-00 — Bootstrap and baseline

Initialize `main` with license, repository policy, CMake project/presets, README contract, and a smoke test. Create the public GitHub repository, push the exact baseline, fetch it, and branch `feat/rollback-netcode-0.1` from the fetched `origin/main`. Record base SHA before feature work.

### PACT-10 — Deterministic simulation

Introduce checked frame/integer primitives, compact input bits, PCG32 scripted inputs, fixed-capacity canonical arena entities, explicit canonical serialization, FNV-1a hash, and deterministic event generation. RED tests cover golden input bytes/hash, arena boundaries, projectile capacity/order, damage/score/respawn, range failures, and extreme/conflicting inputs.

### PACT-20 — Snapshot and rollback session

Implement fixed-capacity frame-indexed histories and snapshots, last-known prediction, late-input ingestion, earliest-dirty coalescing, restore/resimulation, confirmation, hash history, and exact metrics. RED tests cover zero delay, correct prediction, mismatches, multiple late corrections, window limits, ring wrapping, event replacement, convergence, and no shared-state escape hatch.

### PACT-30 — Protocol and seeded transport

Implement checked byte reader/writer, packet model, CRC, packet codec, sequence window, deterministic logical-time transport, queue policies, and deterministic packet trace. RED tests cover packet types, every truncation boundary, corruption, excess counts/lengths, random bytes, duplicate/out-of-order/stale behavior, loss/jitter/reorder/duplicate/burst/overflow/timeout, and repeated byte-stable reports. Add a standalone fuzz-smoke entry point and optional libFuzzer target where supported.

### PACT-40 — Replay, report, and desync

Implement strict binary replay, canonical JSON report, bounded trace, confirmed-hash exchange/comparison, controlled simulation-variant injection, diagnostic artifacts, `simulate`, `replay`, `verify`, `benchmark`, and `compare`. RED tests prove replay reconstruction/rejection, report key order, exact divergent-frame detection, no speculative false positive, and benchmark schema.

### PACT-50 — Localhost UDP multiprocess

Implement portable UDP RAII, Windows process supervision, handshake/session envelope, opaque relay, peer run loop, random port reservation, watchdog, teardown, and supervisor validation. Integration tests exercise normal convergence, bind conflict, missing peer timeout, protocol mismatch, replay reconstruction, and no residual child process. Keep all wall-clock use outside canonical modules.

### PACT-60 — Viewer and portfolio surface

Generate a bounded real trace and self-contained HTML. Implement arena rendering, play/pause, step both directions, scrubber, predicted/confirmed state, rollback and packet markers, HP/score/hash/desync, metrics, responsive sizing, and reduced motion. Produce sample report/replay/trace/viewer, run browser checks at desktop and narrow widths, capture screenshot, and write the complete portfolio documentation set.

### PACT-70 — Hardening and release candidate

Run 10,000 seeds, protocol fuzz smoke, stress loops, MSVC Debug/Release, clean rebuild, a project-local portable Clang build if no installed GCC/Clang exists, and supported sanitizer builds. Run UDP repeatedly, audit core for wall-clock/random/float and peer-sharing violations, confirm docs/artifacts and clean status, push the feature branch, then create a Draft PR with exact evidence and limitations.

## Planned repository structure

```text
include/rollback_lab/{core,simulation,netcode,protocol,transport,replay,report}/*.hpp
src/{core,simulation,netcode,protocol,transport,replay,report,cli}/*.cpp
tests/{unit,integration,property,protocol,udp,fuzz}/*.cpp
tools/*.ps1
viewer/{generate-template.html,sample-viewer.html,sample-trace.json,screenshot.png}
samples/{report.json,replay.rlr,desync-diagnostic.json}
docs/*.md
tasks/20260903-215400-rollback-netcode-0.1/{contracts,plans,progress,evidence}
```

## Interfaces locked before implementation

- `Result<WorldState> simulate_frame(const WorldState&, FrameNumber, const InputPair&, SimulationVariant)`
- `std::vector<std::byte> serialize_canonical(const WorldState&)`
- `StateHash hash_canonical(const WorldState&)`
- `Result<AdvanceResult> RollbackSession::advance(LocalInput)`
- `Result<CorrectionResult> RollbackSession::ingest_remote(RemoteInput)`
- `Result<PacketBytes> encode_packet(const Packet&)`
- `DecodeResult decode_packet(std::span<const std::byte>)`
- `Result<void> SeededTransport::send(Endpoint, Endpoint, PacketBytes, LogicalTick)`
- `std::vector<Delivery> SeededTransport::deliver(LogicalTick)`
- `Result<Replay> read_replay(std::span<const std::byte>)`
- `Result<ReplayVerification> verify_replay(const Replay&)`
- `Result<void> run_udp_demo(const UdpDemoConfig&)`
- `Result<void> write_canonical_report(const RunReport&, const std::filesystem::path&)`
- `Result<void> write_viewer(const Trace&, const std::filesystem::path&)`

Exact namespace and field definitions are fixed in the task plan before RED tests; signature changes require updating contract and dependent tasks together.

## Evidence discipline

Each PACT begins with acceptance rows in `verification_matrix.md`, records observed RED and GREEN commands, commits only the focused slice, and writes stdout/stderr plus environment metadata under `evidence/PACT-*`. `progress.md` is refreshed at every checkpoint with exact HEAD and next action. A failure patched twice creates a root-cause packet before any third attempt.

## Blueprint self-review

- Placeholder scan: no TBD/TODO or deferred P0 behavior.
- Consistency: snapshot semantics, confirmed hashes, protocol redundancy, and UDP boundaries match across contract and architecture.
- Scope: all P0 subsystems remain in one staged product because each later stage validates earlier production APIs; optional Dash, MQB, videos, networking services, and decorative viewer polish are excluded.
- Ambiguity resolution: state frame numbers name pre-frame boundaries; hashes name post-frame boundaries; CRC is explicitly non-security; UDP scheduling is observational, not deterministic identity.


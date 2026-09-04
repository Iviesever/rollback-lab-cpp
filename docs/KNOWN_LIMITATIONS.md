# Known limitations

## Product and engine scope

- This is an educational two-peer rollback laboratory, not production-ready networking or a commercial game. It has one arena, one projectile attack, fixed capacities and a last-known-input prediction policy.
- The UE Arena displays two independent canonical SDK worlds inside one UE scene. Its peers use the existing seeded in-process packet transport. **PACT-85, two separate UE clients over real UDP, is not delivered.** The original CLI relay plus two independent peer child processes remains a real localhost UDP path.
- There is no UE Replication, Iris, Network Prediction Plugin, dedicated server, GAS, full physics engine, large animation system, Unity integration, cloud backend or database.
- Verified engine integration targets Win64 x64, UE 5.8 and a Release MSVC `/MD` SDK. Other engines/platforms/toolchains, actual debug-CRT UE targets and hot/dynamic module reload are outside the supported boundary. BuildPlugin checks Editor Development and Game Development/Shipping; Win64 Shipping is the default demo acceptance target. A script option is not evidence that every configuration has passed.
- Final packaged acceptance, artifact freshness, full regression and release recommendation must come from the [current matrix](../tasks/20260904-100800-ue5-live-integration-0.2/verification_matrix.md) and exact verifier output. Earlier checkpoint success, including working-source Shipping smoke, does not substitute for final clean-HEAD evidence. No v0.2 tag or formal GitHub Release is part of this candidate task.

## Determinism and runtime bounds

- Only canonical state is deterministic. Human input sampled at different wall-clock times can produce different runs. Rendering, frame duration, capture completion and benchmark timings are observations outside canonical identity.
- The fixed-step bridge caps catch-up at eight logical steps per callback and discards excess whole-step wall debt. Under overload it can fall behind real time; it never skips canonical frame numbers. Interactive mode is bounded to 36,000 simulation frames.
- History is 256 frames, rollback accepts at most 120 frames, input/hash redundancy is at most 32 frames and sequence observation is bounded to 64 packet positions. Excess age/capacity fails according to the typed contract.
- Handles are affine to the creating thread, including queries and destruction. Concurrent access and arbitrary stale/forged pointers are not supported. The C ABI is an interoperability boundary, not a memory-safety sandbox.
- Canonical hashing currently builds a byte vector, so a small hot-path allocation can occur. The project does not claim an allocation-free tick or a cross-machine performance SLA.
- Replay v1 assumes the default initial world and contiguous inputs beginning at frame zero. It is not a general save-game or arbitrary-world snapshot format.
- The report writer emits an explicit versioned schema. The CLI `compare` command reads its generated identity field rather than serving as a general JSON equivalence checker; the UE integration verifier separately reparses and compares nested reports.
- Correction visuals are bounded and may display the most recent event within a rendered callback. Trace/metrics remain the run evidence. The demo does not implement a general production audio/VFX event reconciliation system.

## Network and security

- The seeded transport uses integer probabilities and logical ticks. It is a reproducible hostile-network model, not a carrier/ISP-calibrated simulator.
- The CLI relay forwards on localhost. There is no WAN service, NAT traversal, STUN/TURN, matchmaking, lobby, persistence, account, authentication, encryption, congestion-control service or anti-cheat.
- Port discovery reserves a random bind-0 port and releases it before a child binds it, leaving a small TOCTOU race. Conflicts fail safely rather than silently joining or choosing another session.
- Localhost UDP does not test geographic RTT, route changes, MTU diversity, congestion or adversaries. It demonstrates the actual socket/process/packet path, not WAN production readiness.
- CRC-32 detects accidental corruption; FNV-1a is not cryptographic. Equal hashes are not a proof against collisions or shared implementation bugs. SHA-256 manifests detect alteration relative to a trusted manifest but are not signed authentication.

## Verification environment

- Core verification includes local MSVC/portable Clang and supported ASan+UBSan. Windows LeakSanitizer is unsupported and explicitly disabled. An earlier isolated sanitizer UDP failure is retained in [PACT-81 review evidence](../tasks/20260904-100800-ue5-live-integration-0.2/evidence/PACT-81-review.md); its cause was not established, while the subsequent five-configuration matrix and 20-run sanitizer UDP stress passed.
- Ordinary Development startup can expose stock UE trace control on port 1985; it triggered a Windows firewall prompt in the tested run. The Shipping acceptance build uses standard `UE_TRACE_ENABLED=0` and its ordinary run did not prompt. No OS/firewall settings or Engine files were changed. `-notraceserver` prevents automatic Editor trace-server launch, not Development trace-control startup.
- Cold UE shader preparation is a separate bounded warmup operation. Smoke keeps its 45-second inner and 240-second outer bounds. NullRHI Automation does not validate rendered images; PNG materialization does not replace visual inspection.
- Browser QA covers the checked viewer on the tested Chromium surface and viewport sizes, not an exhaustive device/browser matrix. Long traces are bounded/sampled rather than retaining every state forever; embedded JSON is not compressed.
- Engine files are read-only. Stock UBT `Trace.uba` and its backups are the sole explicitly approved external-cache exception. All other task outputs remain repository-local; cleanup targets owned process jobs rather than names.

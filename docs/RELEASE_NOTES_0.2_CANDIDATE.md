# RollbackLab 0.2.0 Candidate

This candidate embeds the existing deterministic C++23 rollback Core in UE 5.8 through stable C ABI v1. It now supports both a seeded dual-view Arena and two separate Shipping clients connected through the real localhost UDP relay. PACT-85 functional closure is verified; final clean-source rebuild, audit and Draft PR acceptance remain in the [verification matrix](../tasks/20260904-100800-ue5-live-integration-0.2/verification_matrix.md). No release tag or formal release has been published.

## Product changes

- C11 SDK with 26 `rl_` exports, fixed-width records, caller-owned buffers, thread-affine opaque handles, typed errors and exception containment. The seven UDP exports and `RL_CAP_UDP=8` extend ABI v1 without changing existing records or exports.
- Runtime plugin with verified SDK staging, explicit DLL loading and driver-before-session teardown. Two sessions belong to a local seeded Runtime; one session belongs to each UDP client Runtime. UE does not copy Core simulation or networking logic.
- Native single/dual-world presentation with bounded 60 Hz adaptation, real correction ghosts/lines/flashes, pooled geometry and a HUD showing canonical and transport observations.
- Seeded auto, interactive local A/scripted B and controlled-desync modes; a separate scripted UDP mode capped at 240 frames.
- Incremental production `PeerDriver` shared by CLI and C ABI, a three-target Shipping supervisor, atomic bound-port readiness, confirmed hash/replay verification and bounded process cleanup.
- SDK/plugin/demo packaging, all-payload manifests and ZIP/SHA checks, seeded three-way verifier and separate real-UDP evidence receipts.

C++ API compatibility, simulation version 1, Protocol v1 and Replay v1 are preserved. The original sample hash remains `0x4B35DC3FD8F6009C`, and its seeded report identity remains `0x8150FEDA020B66A8`.

## Verified checkpoints

The clean P0 checkpoint completed five compiler configurations, repeated 10,000 seeds, SDK consumers, 28 UE tests, BuildPlugin, Shipping BuildCookRun, ordinary controls, six packaged failure cases and 16 seeded three-way checks before optional UDP work began. Its receipt is `artifacts/ue5-0.2/p0-clean-1c2e-checkpoint.json`; [PACT-85-plan.md](../tasks/20260904-100800-ue5-live-integration-0.2/PACT-85-plan.md) records the entry gate.

The UDP SDK checkpoint passed Debug/Release/ASan+UBSan at 7/7 CTest. A later real caught-allocation-failure consistency regression adds an eighth entry; integrated Release now passes 8/8. Behavior runners remain 61 Core cases, 7 session ABI cases, 4 seeded live cases and 6 UDP cases; the isolated exception test is separate. The two 128-scenario seeded C++/C parity sweeps and genuine C11/installed consumers remain.

UE now passes 32/32 Automation: the original 28 plus four UDP cases. The seeded packaged CLI/C11/UE three-way verifier still passes after the extension. Real normal UDP runs `20260904-155457-274` and `20260904-160929-278` each ran the relay and two distinct Shipping processes, one session per client, dynamic bound ports, confirmed frame 240 and the preserved canonical hash. Both peer replays matched and verified through the canonical CLI. All targets exited 0 and were reaped. Aggregate rollback counts were 153 and 141; the centered second run had five inspected captures, with all local corrections on B.

All six actual UDP negative cases pass: MissingPeer, ProtocolMismatch, SimulationMismatch, AbiMismatch, Watchdog and Desync. The watchdog uses the existing five-second outer deadline and still persists failure summary and complete owned-child cleanup. Its finalizer first records failure, retries only Windows sharing/lock errors within the existing cleanup budget and records unavailable hashes as null; successful normal runs require a complete inventory. Supervisor evidence-helper tests pass 38/38.

**The current UDP integration artifacts are working-source evidence.** Final SDK, plugin, Shipping demo, full matrix/sweep, seeded verifier, UDP receipts and independent audit must be regenerated/rechecked from the same clean reviewed HEAD before push/Draft PR acceptance. Functional closure is not a claim that those final delivery gates have already run.

## Contracts and limits

The UDP `engine-udp-v1` digest binds ABI, protocol/simulation versions, seeds and target frame using the existing Hello configuration field. It validates ABI as an aggregate profile, not as an independently readable remote ABI field. The legacy CLI handshake and strict Protocol-v1 wire format remain unchanged.

UDP steps have no worker thread or sleep, poll at most 64 datagrams and advance at most one scripted frame. Caller-provided monotonic elapsed milliseconds control deadlines outside canonical state. UE peer sockets stay bound from ready publication through exit; only the relay reservation has a narrow release/bind handoff, which fails closed on conflict.

Real UDP reports/counters depend on scheduling. Compare agreed profile, confirmed hash and canonical replay; require prediction/correction in aggregate rather than on every endpoint. Do not claim byte-stable UDP reports or apply the seeded emulator's identity to real UDP runs.

Shipping is the default artifact. It uses stock UE tracing-disabled configuration, avoiding the Development trace-control prompt seen in an earlier ordinary run. No OS/firewall settings or Engine files were changed. The minimal engine-plugin profile enables ACLPlugin for stock animation-compression assets and leaves EnhancedInput off.

This is a Win64 x64 UE 5.8 teaching laboratory. It is not UE Replication, Iris, a dedicated server, matchmaking, NAT traversal, authentication, encryption, anti-cheat or WAN production readiness. See [known limitations](KNOWN_LIMITATIONS.md).

## Authorship

The user specified the career goal, product direction, deadline, boundaries and acceptance criteria. Codex GPT-5.6 Sol refined the architecture and produced Core/C ABI/SDK/UE work, UDP integration, tests, debugging, packaging, visual checks and documentation. The user did not hand-write the delivered code in this session and must not claim independent hand-written authorship. Before presenting it, the user must understand C ABI, fixed-step, presentation ownership and rollback flow and personally complete a [live change drill](LIVE_CHANGE_DRILLS.md). See [AI assistance](AI_ASSISTANCE.md).

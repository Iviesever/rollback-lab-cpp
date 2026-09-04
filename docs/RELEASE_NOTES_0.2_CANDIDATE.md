# RollbackLab 0.2.0 Candidate

This candidate embeds the existing deterministic C++23 rollback Core into a native Unreal Engine 5.8 demo through stable C ABI v1. It is not a published release. Final clean-source packaged acceptance, the refreshed full test matrix and Draft PR state are recorded in the [verification matrix](../tasks/20260904-100800-ue5-live-integration-0.2/verification_matrix.md).

## Product changes

- A genuine C11 SDK with 19 `rl_` exports, fixed-width versioned records, caller-owned buffers, thread-affine opaque handles, typed errors and exception containment. CMake install/export supports independent C11 and C++ `find_package` consumers.
- A Win64 UE 5.8 Runtime plugin with automatic verified SDK staging, explicit DLL export loading, owned library leases and driver-before-session teardown. No Core implementation is copied into UE.
- A native dual-view Arena with independent canonical peer worlds, bounded 60 Hz adaptation, real correction ghosts/lines/flashes and a HUD showing confirmation, hashes, network settings and rollback metrics.
- Scripted auto, interactive A/scripted B and controlled confirmed-desync modes; pause, exact step, reset, bounded network presets and short-tap input retention.
- Repository scripts for independent BuildPlugin, native map/material generation, Shipping BuildCookRun, ordinary packaged startup, smoke, three-way parity, manifests, archives and SHA-256 validation.

The original C++ API, CLI defaults, simulation version 1, protocol v1 and Replay v1 remain compatible. Existing sample trace and replay bytes remain unchanged. Default sample hash is `0x4B35DC3FD8F6009C`; report identity is `0x8150FEDA020B66A8`.

## Evidence status

The latest PACT-84 working-source checkpoint passes all six CTest entries in each of five compiler/configuration combinations: 61 Core/CLI/UDP cases, 7 ABI session cases, 4 ABI live cases, true C11 smoke, structured fuzz and property smoke. The two parity sweeps each cover 128 scenarios; installed C11/C++ consumers pass 2/2. The repeated 10,000-seed sweep passes with 9,600 convergences, 200 expected queue-overflow failures, 200 expected timeout failures and zero identity mismatches/crashes/deadlocks/unbounded failures. Final clean-source matrix and sweep refresh remain required.

PACT-82 passed standalone BuildPlugin for Editor Development and Game Development/Shipping. PACT-83 passed 28/28 UE Automation tests with zero warnings, including four real PIE lifecycles and actual PlayerController input, plus six fail-closed process cases. Real Editor RHI smoke reached 240/240 confirmed frames with 189 rollbacks, 857 resimulated frames, matching sample hash/identity and a verified replay; all three screenshots were inspected.

**Packaged Shipping checkpoint gates now pass.** Real BuildCookRun completed in 53.22 seconds with no Warning/Error diagnostics. Rendered smoke confirmed 240/240 with the preserved sample hash, 189 rollbacks and a verified replay; all three Shipping captures were inspected. An ordinary packaged window verified physical pause, movement, attack and exact single stepping, then exited its bounded run successfully. Both smoke and ordinary bootstrap/game processes were fully reaped.

Shipping is the default package because stock Development trace control on port 1985 triggered a Windows firewall prompt during an earlier ordinary launch. No security prompt action, OS setting or Engine file was changed. Standard Shipping disables UE tracing, and the tested Shipping ordinary launch did not prompt. The minimal project explicitly enables ACLPlugin for stock animation-compression assets, leaves EnhancedInput off and has no GameplayCameras dependency.

**The intermediate end-to-end functional checkpoint is green.** The refreshed BuildPlugin passes Editor Development and Game Development/Shipping, and the minimal plugin profile passes 28/28 Automation. The external verifier passes all 16 checks: complete CLI/C11/packaged UE reports and traces are equal, the three replays are byte-identical and each verifies through the CLI, processes exit cleanly, and SDK/Plugin/Demo identities plus all three ZIPs validate. Artifact/evidence regression checks pass 44/44.

**Final clean-source acceptance remains pending.** The current verifier explicitly records `source_clean=false`. Final SDK, plugin, demo, matrix/sweep and verifier evidence must be regenerated from the same clean reviewed HEAD; independent audit and PR acceptance are also outstanding. The authoritative result format remains `artifacts/ue5-0.2/parity/<run>/verification.json`. Packaged negative-process results are tracked separately from the already-passing Editor negatives.

## Deferred scope and limitations

PACT-85, two separate UE clients communicating through the real UDP relay, is not delivered. The UE Arena uses the existing seeded in-process packet transport. The original CLI relay plus two-peer localhost UDP demo remains available. Neither path establishes WAN production readiness. There is no UE Replication, Iris, Network Prediction Plugin, dedicated server, matchmaking, NAT traversal, authentication, encryption or anti-cheat.

The supported UE integration is Win64 x64 with UE 5.8 and the Release `/MD` SDK. Canonical Core stays C++23; engine presentation may use floating point. See [known limitations](KNOWN_LIMITATIONS.md), [SDK packaging](SDK_PACKAGING.md) and [UE testing](UE5_TESTING.md).

## Authorship

The user specified the career goal, product direction, deadline, boundaries and acceptance criteria. Codex GPT-5.6 Sol refined the architecture and produced the C ABI, SDK, UE plugin, demo, tests, debugging, packaging, visual checks and documentation. The user did not hand-write the delivered code in this session and must not claim independent hand-written authorship. Before presenting the project, the user must understand C ABI, fixed-step adaptation, presentation ownership and rollback flow and personally complete at least one [live change drill](LIVE_CHANGE_DRILLS.md). See [AI assistance](AI_ASSISTANCE.md).

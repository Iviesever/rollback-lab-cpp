# UE live integration blueprint

## Architecture

The existing core remains authoritative. A reusable incremental seeded scenario driver replaces the blocking loop without changing phase order or canonical algorithms. The CLI loops this driver. The C ABI adapts owned RollbackSession handles and the driver. UE owns two independent session handles and a driver wrapper, runs bounded fixed steps, and copies immutable presentation snapshots to a native arena and HUD.

## Delivery order

1. PACT-80: recover clean latest main/GitHub state, fresh 0.1 Debug/Release/CLI baseline, lock contracts, and build an isolated DLL/UE BuildPlugin spike. Commit decision separately.
2. PACT-81: add failing C11/ABI/session/parity tests; implement C API and incremental driver; preserve old sample/golden identities; export/install/find_package consumers and checksummed SDK. Full core regression, scoped commit.
3. PACT-82: Runtime plugin, integrity-checked SDK stage/load, native RAII subsystem, fixed-step adapter and focused lifecycle/negative Automation. Standalone BuildPlugin and full core regression, scoped commit.
4. PACT-83: native dual arenas, bounded pooled basic shapes, HUD, real correction ghosts/lines/flash, scripted/interactive/desync modes, pause/step/reset/presets. Automation and visual inspection, scoped commit.
5. PACT-84: shared PowerShell build orchestration, deterministic smoke, JSON/replay/screenshot evidence, five core configurations, full 10,000-seed repeat, fuzz, Editor/BuildPlugin/BuildCookRun/ordinary packaged/packaged smoke. Bind all final artifacts to one clean HEAD.
6. PACT-85: optional two-process UE UDP only after all P0 green, before 2026-09-05 04:00 UTC+8 and remaining quota over about 22%. Stop expanding at 18%, remove incomplete production code before freeze, document deferral accurately.
7. PACT-86: complete portfolio and instructional docs, fresh independent read-only origin/main...HEAD audit; fix only confirmed relevant findings. Final clean SHA rebuild/evidence, push and Draft PR with exact results. No merge/tag/release.

## Acceptance sequence

Each behavior follows contract -> observed RED -> minimal GREEN -> focused tests -> affected full regression -> evidence -> scoped commit. A third patch for one fault requires a root-cause packet. Raw logs live in ignored artifacts; evidence summaries use relative paths and exact SHAs. progress.md is the resume entry point.

## Review

The user's supplied objective already specifies architecture, boundaries, deadline and authorization. Shared DLL versus static ABI is the only bounded feasibility choice; a static fallback requires actual staging failure. No further approval is necessary to make and validate that choice. One primary agent controls builds; at most two disjoint child agents may inspect or implement scoped work. No two Unreal tool processes run concurrently.

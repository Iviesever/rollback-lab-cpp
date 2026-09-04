# UE integration execution tasks

Use executing-tasks with observed RED/GREEN and per-PACT checkpoints. The user has authorized continued implementation; no worktree or second clone is permitted.

## Global constraints

C++23; 60 canonical ticks/s; 1/1024 coordinate units; 120-frame rollback; 256-frame histories; protocol v1; replay v1; two independent sessions. All temporary/runtime outputs use repository-local paths. Engine is read-only. Parent controls all Unreal processes. No merge, tag, release, remote deletion, or credentials in files.

## PACT-80

- [x] Fresh branch/base/GitHub and complete historical contracts.
- [x] Debug/Release/structured fuzz/property/CLI/replay/desync/UDP/viewer baseline.
- [x] Actual C11 DLL consumer and three-configuration UE BuildPlugin spike.
- [x] Commit integration decision (`138c9a4`).

## PACT-81A: incremental production driver

Files: `include/rollback_lab/report/live_scenario.hpp`, `src/report/scenario_runner.cpp`, `tests/integration/live_scenario_test.cpp`.

Interface: `LiveScenario::create(const ScenarioRunConfig&, RollbackSession&, RollbackSession&) -> Result<std::unique_ptr<LiveScenario>>`; `step(std::optional<std::uint8_t> local_buttons = {}) -> Result<bool>`; `artifacts() const -> const ScenarioArtifacts&`; `logical_tick() const -> uint32_t`; `correction(PlayerId) const -> const LiveCorrection&`. bool means finished. Creation borrows distinct pristine A/B sessions. Per-tick correction stores real before/after worlds plus CorrectionResult and peer; no canonical state pointer escapes. A local override changes only scripted A buttons for this exact frame.

- [x] RED: step driver to completion and assert exact old default identity, report metrics, replay bytes, and trace bytes; first step leaves frame 1; supplied handles remain distinct; real correction captures a changed world; final hashes equal; post-completion step is stable. Missing header is expected first RED.
- [x] GREEN: extract existing runner locals to heap-owned driver state; preserve send/deliver/advance/tail/drain/finalize phase order and all helper functions. `run_seeded_scenario` creates two sessions and loops this driver.
- [x] Regress: existing 56-case runner + fuzz + property + checked golden and sample parity; 100+ driver scenarios.

## PACT-81B: C ABI / SDK (bounded child ownership)

Files: `include/rollback_lab/c_api/rollback_lab_c.h`, `src/c_api/session_api.cpp`, `src/c_api/internal.hpp`, `tests/c_api/*`, CMake export/install, `scripts/{SdkCommon,BuildSdk,VerifySdk}.ps1`.

- [x] RED: genuine C11 and C++ consumer plus session invalid-version/size/null/peer/frame/buffer/isolation/parity tests.
- [x] GREEN: opaque SDK allocation/destruction, caller-owned fixed-width layouts, explicit status mapping and exception guard, native session borrow ownership for live facade.
- [x] Verify independent find_package consumer against install tree, ZIP license/README/header/DLL/import lib/manifest/source SHA/checksums. Preserve public C++ target.

## PACT-81C: live C facade and relative UDP path

Files: `src/c_api/live_api.cpp`, C header live declarations, `tests/c_api/live_test.cpp`, `tests/udp/udp_demo_test.cpp`, `src/udp/demo.cpp`.

- [x] RED: C facade same seeded inputs/transport -> same complete report/replay/hash as direct C++ across >=100 scenarios; live session mutation/destruction while borrowed fails; size query and buffer-too-small are exact; interactive input changes only A; two handles prove isolation.
- [x] GREEN: adapt LiveScenario through C structs and sized JSON/replay copy exports; create/destroy guards borrow lifetime.
- [x] RED relative UDP output: use a relative path from CTest build cwd and verify supervisor plus all peer files.
- [x] GREEN relative UDP output: resolve once to an absolute output directory before subprocess composition, retain all protocol behavior.
- [ ] Full Core MSVC Debug/Release and SDK regressions; evidence + scoped commit.

## PACT-82

Files: UE project/plugin descriptors, Build.cs/Target.cs, native module and subsystem, fixed-step wrapper, Automation tests, SDK staging script.

- [ ] RED Automation: missing/wrong manifest/DLL/ABI/export/SHA, two sessions, repeated reset/deinitialize and bounded accumulator.
- [ ] GREEN Runtime module resolves DLL exports after manifest/file validation; release owned sessions before unloading; subsystem wraps native RAII, fixed step 1/60 with cap 8.
- [ ] Focused Automation, independent BuildPlugin, Core regression, scoped commit.

## PACT-83

Files: native GameMode, arena actor/component pool, HUD, controller, generated map script.

- [ ] RED mode/control/smoke state machine tests: scripted real rollback, pause exact zero steps, single-step one tick, reset clears resources, network presets bounded, confirmed-only desync.
- [ ] GREEN two side-by-side arenas, readonly pooled shapes, full metrics/seeds/version HUD, actual correction pre/post ghosts/arrows/flash.
- [ ] Editor ordinary interactive launch; keyboard controls; Automation; capture and inspect screenshots; scoped commit.

## PACT-84

Files: shared Unreal orchestration and BuildUnrealPlugin/BuildUnrealDemo/TestUnrealIntegration/PackageUnrealDemo/VerifyUnrealIntegration scripts.

- [ ] RED smoke verifier rejects missing screenshot, wrong hash, stale SHA, no rollback, failed replay, malformed JSON and watchdog.
- [ ] GREEN bounded process orchestration, actual RHI screenshot state machine, failure trace, correct exits and teardown.
- [ ] Fresh full Core five-config matrix, 10000-seed full repeat, structured fuzz, Automation, Editor build/load, three-target BuildPlugin, BuildCookRun, ordinary packaged launch, packaged smoke, three-way parity/replay/screenshot content audit.
- [ ] Clean exact HEAD manifests and checksums; scoped commit and final artifact rerun after production changes.

## PACT-85 (conditional)

- [ ] Evaluate actual green P0, time and quota; only then begin two UE client UDP integration.
- [ ] If ineligible or unfinished at freeze, preserve investigation and record deferral without unfinished production code.

## PACT-86

- [ ] Update all required design/packaging/testing/interview/drill/AI/candidate release docs to actual evidence.
- [ ] Fresh read-only independent full-diff audit; fix confirmed Blocker/High and low-risk related Medium only.
- [ ] Verify clean exact HEAD artifacts and source tree, push feature branch, create/update Draft PR, check CI and remote status, deliver prescribed final report. No merge/tag/release.

# RollbackLab 0.2.0 Candidate

A C++23 rollback laboratory embedded in Unreal Engine 5.8 through a stable C ABI. Two independent canonical peer worlds predict missing inputs, correct late mismatches, and converge while Unreal shows the difference.

```text
C++23 Rollback Core
        ↓
Stable C ABI v1 SDK (DLL)
        ↓
UE 5.8 Runtime Plugin
        ↓
Two Independent Peer Worlds
        ↓
Visible Prediction / Correction / Convergence
        ↓
Packaged Hash-Parity Evidence
```

The final step is an acceptance gate: the CLI, C11 consumer and packaged UE executable must produce the same confirmed hash, report identity and replay. The Shipping checkpoint passes BuildCookRun, ordinary interactive launch, rendered smoke and all 16 external three-way verification checks. Final clean-source artifacts, audit and PR acceptance remain tracked in the [0.2 verification matrix](tasks/20260904-100800-ue5-live-integration-0.2/verification_matrix.md). This is a release candidate, with no release tag or published release.

## See the rollback

![Real packaged UE Shipping correction showing the two peer views](docs/images/ue5-correction.png)

The UE image is a real **packaged Shipping preview** from the intermediate checkpoint. Final artifact proof comes from the verifier JSON. The native scene displays two canonical SDK worlds in one UE scene; Actors own presentation only. Ghosts, correction lines and flashes come from actual Core correction results.

![Self-contained rollback timeline generated from a real trace](viewer/screenshot.png)

Open the checked-in [interactive viewer](viewer/sample-viewer.html) to scrub predictions, confirmations, packet events, projectiles and corrections. It embeds a bounded [production trace](viewer/sample-trace.json) and needs no CDN or Node runtime.

## Shortest build and run

Run from the repository in PowerShell 7. Core requires CMake 3.28+, Ninja and a Visual Studio C++ Developer PowerShell; SDK scripts discover MSVC themselves. UE commands require a local, read-only UE 5.8 installation. Use a clean checkout for deliverable packages.

```powershell
# Core: configure, build and test.
cmake --workflow --preset msvc-debug-ci

# SDK: build, install, package, then verify independent C11/C++ consumers.
./scripts/BuildSdk.ps1
$revision = (git rev-parse HEAD).Substring(0, 12)
$Sdk = "artifacts/sdk/0.2.0-$revision/install"
./scripts/VerifySdk.ps1 -SdkRoot $Sdk

# Standalone Runtime plugin and cooked Win64 Shipping demo.
$EngineRoot = '<UE 5.8 root>'
$Plugin = "artifacts/ue5-0.2/plugin/0.2.0-$revision"
$Demo = "artifacts/ue5-0.2/demo/0.2.0-$revision-shipping"
./scripts/BuildUnrealPlugin.ps1 -EngineRoot $EngineRoot -SdkRoot $Sdk
./scripts/PackageUnrealDemo.ps1 -EngineRoot $EngineRoot -SdkRoot $Sdk

# Ordinary interactive packaged executable, then bounded automated smoke.
./scripts/LaunchPackagedUnrealArena.ps1 -EngineRoot $EngineRoot -DemoRoot $Demo
./scripts/LaunchPackagedUnrealArena.ps1 -EngineRoot $EngineRoot -DemoRoot $Demo -Smoke

# Use the run directory printed by the smoke command.
./scripts/VerifyUnrealIntegration.ps1 -SdkRoot $Sdk -PluginRoot $Plugin -DemoRoot $Demo -SmokeRun '<smoke run directory>'
```

Packaging generates the map and material automatically. SDK/DLL staging needs no manual copying. `-AllowDirty` produces diagnostic artifacts only; it cannot satisfy final acceptance. The verifier writes `artifacts/ue5-0.2/parity/<run>/verification.json`, which is the authority for the supplied artifacts' three-way parity and integrity checks.

For the Editor and detailed prerequisites, see [UE integration](docs/UE5_INTEGRATION.md). Controls: **1** auto, **2** interactive, **3** controlled desync; **WASD/arrows** move A; **Space** attacks; **P** pauses; **.** steps one logical tick; **R** resets; **N** cycles Local/Default/Hostile network presets; **Esc** exits. Preset and mode changes restart the scenario.

The original CLI paths remain available:

```powershell
build\msvc-debug\rollback_lab.exe simulate --scenario default --frames 240 --out artifacts\demo
build\msvc-debug\rollback_lab.exe udp-demo --frames 120 --out artifacts\udp-demo
build\msvc-debug\rollback_lab.exe replay samples\input.rlr
```

## Evidence and identity

The preserved default sample uses scenario seed `12648430`, transport seed `5351397`, 240 frames, 5-tick latency, 3-tick jitter, 5% loss, 10% reorder, 5% duplicate and 1% burst loss.

| Observation | Preserved sample / verified Shipping checkpoint |
| --- | --- |
| Confirmed boundary A / B | 240 / 240 |
| Final hash A / B | `0x4B35DC3FD8F6009C` / `0x4B35DC3FD8F6009C` |
| Report identity | `0x8150FEDA020B66A8` |
| Rollbacks / resimulated frames / maximum depth | 189 / 857 / 9 |
| Replay | Canonical Replay v1 verifies; existing sample bytes are preserved |
| CLI / C API / UE Editor | Same sample hash and identity; [PACT-81](tasks/20260904-100800-ue5-live-integration-0.2/evidence/PACT-81.md), [PACT-83](tasks/20260904-100800-ue5-live-integration-0.2/evidence/PACT-83.md) |
| Packaged Shipping checkpoint | BuildCookRun, ordinary controls and rendered smoke passed; both owned processes exited |
| CLI / C API / packaged UE | 16/16 verifier checks passed: equal complete reports/traces and byte-identical verified replays; intermediate source, with final clean-source evidence pending in the [matrix](tasks/20260904-100800-ue5-live-integration-0.2/verification_matrix.md) |

Checked artifacts: [report](samples/report.json), [replay](samples/input.rlr), [trace](samples/trace.json), [viewer](samples/viewer.html). A matching hash is evidence of agreement, not proof against collisions or every possible shared bug; replay reconstruction, golden samples, ownership tests and compiler checks provide additional constraints.

## What is tested

- **Core/CLI/UDP:** 61 behavior cases, preserving the original 56; integer state, tagged history, prediction/correction, strict packets, replay, bounded transport and real localhost relay + two peer child processes.
- **C ABI:** 7 session cases and 4 live-driver cases, including 128 delayed-input and 128 complete packet-driven C++/C parity scenarios; 19 C exports and a true C11 consumer. Installed `find_package` C11/C++ consumers pass separately, 2/2.
- **CTest:** 6 entries per configuration, including the 100,000-input structured packet/replay fuzz smoke and 100-seed property smoke. The current five-configuration checkpoint passes MSVC Debug/Release, Clang Debug/Release and ASan+UBSan. The repeated 10,000-seed sweep passes with 9,600 convergences, 400 expected bounded failures and no identity mismatches/crashes/deadlocks/unbounded failures; final clean-source refresh remains required.
- **UE:** 28/28 Automation tests passed with zero warnings at PACT-83, including four real PIE lifecycles, held/short-tap movement and attack, exact stepping, reset and zero retained SDK resources. Six real failure-process cases return 1, write false traces and leave no owned children.

See [Core testing](docs/TESTING.md), [UE testing](docs/UE5_TESTING.md), and the [candidate notes](docs/RELEASE_NOTES_0.2_CANDIDATE.md) for scope, evidence and outstanding gates.

## Architecture and study guide

The canonical transition receives frame + input pair at a 60-tick/s-compatible cadence. State uses 1/1,024-world-unit integers and checked intermediates. Each peer owns its world, input history, 256-slot snapshots, decisions and metrics. Late mismatches restore the earliest dirty boundary and resimulate within a 120-frame fail-closed window. The UE bridge accumulates wall time outside Core and performs at most eight steps per callback.

- [Architecture](docs/ARCHITECTURE.md), [rollback algorithm](docs/ROLLBACK_ALGORITHM.md), [determinism](docs/DETERMINISM_CONTRACT.md)
- [C ABI](docs/C_ABI.md), [SDK packaging](docs/SDK_PACKAGING.md), [UE integration](docs/UE5_INTEGRATION.md)
- [Fixed-step adapter](docs/FIXED_STEP_ADAPTER.md), [presentation ownership](docs/ENGINE_PRESENTATION_BOUNDARY.md)
- [Protocol](docs/PROTOCOL.md), [UDP](docs/UDP_DEMO.md), [replay](docs/REPLAY_FORMAT.md), [desync diagnosis](docs/DESYNC_DIAGNOSIS.md)
- [Code walkthrough](docs/CODE_WALKTHROUGH.md), [interview guide](docs/INTERVIEW_GUIDE.md), [live change drills](docs/LIVE_CHANGE_DRILLS.md)

## Limits and AI assistance

This is an educational portfolio laboratory. The UE demo uses the seeded packet transport inside one process; the separate CLI UDP demo uses real localhost child processes. Two UE clients over UDP (PACT-85) are not delivered. There is no UE Replication, Iris, production network service, WAN readiness, matchmaking, NAT traversal, authentication, encryption or anti-cheat claim. See [known limitations](docs/KNOWN_LIMITATIONS.md).

The user set the career goal, product direction, deadline, boundaries and acceptance criteria. **Codex GPT-5.6 Sol** refined the architecture and produced the Core/C ABI/SDK/UE implementation, tests, debugging, packaging, visual inspection and documentation. The user did not hand-write the delivered code in this session and must not describe it as independently hand-written. Before an interview, personally understand the ownership and rollback flow and complete at least one [exercise](docs/LIVE_CHANGE_DRILLS.md). Full disclosure: [AI assistance](docs/AI_ASSISTANCE.md).

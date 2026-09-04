# UE 5.8 integration

The Runtime plugin is [RollbackLabBridge](../examples/ue5/RollbackArena/Plugins/RollbackLabBridge/). It calls the installed C ABI header and shared DLL. The existing C++23 Core remains the only owner of simulation, prediction, rollback, protocol, transport decisions and hashing. UE owns wall-clock adaptation, input sampling and presentation.

## Build and launch

From repository PowerShell 7, with a clean checkout and a read-only UE 5.8 installation:

```powershell
$EngineRoot = '<UE 5.8 root>'
./scripts/BuildUnrealDemo.ps1 -EngineRoot $EngineRoot
./scripts/GenerateUnrealContent.ps1 -EngineRoot $EngineRoot
./scripts/TestUnrealIntegration.ps1 -EngineRoot $EngineRoot
./scripts/BuildUnrealPlugin.ps1 -EngineRoot $EngineRoot
./scripts/LaunchUnrealArena.ps1 -EngineRoot $EngineRoot

# Cold shader compilation may be prepared separately before bounded smoke.
./scripts/LaunchUnrealArena.ps1 -EngineRoot $EngineRoot -Warmup
./scripts/LaunchUnrealArena.ps1 -EngineRoot $EngineRoot -Smoke

# Cook and package a normal executable, with no manual Blueprint or map setup.
./scripts/PackageUnrealDemo.ps1 -EngineRoot $EngineRoot
$revision = (git rev-parse HEAD).Substring(0, 12)
$Demo = "artifacts/ue5-0.2/demo/0.2.0-$revision-shipping"
./scripts/LaunchPackagedUnrealArena.ps1 -EngineRoot $EngineRoot -DemoRoot $Demo
./scripts/LaunchPackagedUnrealArena.ps1 -EngineRoot $EngineRoot -DemoRoot $Demo -Smoke
```

Scripts stage a verified SDK for the exact current Git HEAD. If absent, they build it from clean source. Generated files live under the plugin's ignored `Binaries/ThirdParty/RollbackLab`; manual DLL copying is unnecessary. The native content commandlet creates and reopens the map/material. BuildPlugin builds Editor Development, Game Development and Game Shipping. Win64 Shipping is the default packaged demo and final acceptance configuration.

## Arena modes and controls

| Control | Behavior |
| --- | --- |
| `1` | Deterministic auto demo: scripted A and B, 240 frames by default |
| `2` | Interactive A, scripted B; bounded to 36,000 simulation frames |
| `3` | Controlled B damage-bias variant; seed 1 exposes confirmed divergence |
| WASD or arrows / Space | A movement / projectile attack in interactive mode |
| `P` / `.` | Toggle pause / execute exactly one logical tick |
| `R` | Reset the selected scenario and all transient input/correction state |
| `N` | Cycle the three network presets and restart the scenario |
| Esc | Exit; an incomplete smoke exits as failure |

Ordinary launch defaults to interactive mode. `-AutoDemo`, `-Desync` and `-Smoke` select scripted entry paths. Seed options are complete uint64 decimal tokens; suffixes and overflow are rejected. Human input is sampled outside Core and does not imply that separately timed human runs will match across machines. Short presses are latched in a bounded button mask until an actual fixed step consumes them; reset clears the mask.

| Preset | Latency / jitter (ticks) | Loss / reorder / duplicate / burst (%) |
| --- | --- | --- |
| Local | 0 / 0 | 0 / 0 / 0 / 0 |
| Default | 5 / 3 | 5 / 10 / 5 / 1 |
| Hostile | 12 / 6 | 20 / 20 / 10 / 3 |

The HUD shows logical tick, predicted/current and confirmed boundaries, hashes, rollback count/depth/start, total resimulation, network settings, seeds and SDK/ABI version. Ghosts and correction lines come from real SDK before/after snapshots; flashes correspond to correction revisions. The desync mode reports the earliest confirmed divergent boundary, 91 for its default seed, instead of treating normal prediction differences as desync.

## Loading and ownership

Build.cs validates SDK versions and every manifest-listed payload hash, sets include/import-library paths and stages DLL/manifest as NonUFS dependencies. Although the import library is configured, calls use a complete explicit export function table; there are no direct or delay-load C calls. Missing exports fail before invoking the SDK.

At startup the loader checks manifest digest, exact clean source identity, architecture/CRT/linkage, DLL digest, required exports and the loaded SDK's version/capabilities/source SHA. It rejects mismatches instead of silently loading another binary. In dual-view mode, each native `FRuntime` owns two different `rl_session` handles and a live driver that borrows them. In UDP mode, a Runtime owns exactly one session and one borrowing UDP driver; the other peer exists in another process. In either mode, the driver dies first, then the owned session handles, then the last DLL lease. All calls stay on the creating game thread.

`URollbackLabSubsystem` owns the native Runtime through a UE unique pointer and stops it on Deinitialize. The presentation Actor, `ARollbackArenaView`, stops the run and clears capture callbacks at EndPlay. Module pre-exit/shutdown stops registered runtimes; dynamic module reload is disabled. Restart reconstructs the scenario rather than reusing stale handles. Tests cover multiple runtimes, repeated stops, subsystem teardown, external DLL preload and actual PIE restart.

Each SDK lease acquires its own Win64 `LoadLibraryExW` reference to the verified full path. UE 5.8's `GetDllHandle` can return a borrowed already-loaded module handle; treating two borrowed handles as two owned leases caused an actual unload crash during development. The owned-reference fix and external-preload regression prevent one runtime from unloading code another still needs.

## Engine boundary

The dual-view mode has two independent **canonical peer worlds** displayed side by side in one UE scene. UDP mode instead runs one canonical world per UE client process, with both players projected from that local session. Neither mode uses UE replication. The native view owns a fixed pool of two floors, four player components, 128 projectile components, six shared material instances and one camera; the HUD caches its view reference. Core owns position, velocity, HP, score, history and decisions. Actor transforms never flow back into Core.

`FRuntime::TickWallClock` accumulates wall seconds outside Core at 1/60 second, with at most eight steps per callback. It retains the fractional remainder and records discarded whole-step debt; canonical frames are never skipped. Pause adds no debt, and single step preserves pause. See [fixed-step](FIXED_STEP_ADAPTER.md) and [presentation ownership](ENGINE_PRESENTATION_BOUNDARY.md).

## Two Shipping clients over localhost UDP

```powershell
$revision = (git rev-parse HEAD).Substring(0, 12)
$Sdk = "artifacts/sdk/0.2.0-$revision/install"
$Demo = "artifacts/ue5-0.2/demo/0.2.0-$revision-shipping"
./scripts/RunUdpUnrealDemo.ps1 -EngineRoot $EngineRoot -SdkRoot $Sdk -DemoRoot $Demo -Case Normal

# Negative runs deliberately return nonzero and retain diagnostics.
./scripts/RunUdpUnrealDemo.ps1 -EngineRoot $EngineRoot -SdkRoot $Sdk -DemoRoot $Demo -Case MissingPeer
```

`EngineRoot`, `SdkRoot` and `DemoRoot` are required. The demo must be Shipping and match the verified SDK/source. `-AllowDirty` permits labelled diagnostic artifacts only. Public cases are `Normal`, `MissingPeer`, `ProtocolMismatch`, `SimulationMismatch`, `AbiMismatch`, `Watchdog` and `Desync`. The default normal profile uses scenario seed 12648430, transport seed 1430540336 and 240 scripted frames; `Frames` is bounded to 1..240. Handshake/run deadlines default to 15,000 ms and are capped at 60,000 ms. The supervisor also has bounded startup, peer-exit and outer watchdog budgets. Desync defaults to scenario seed 1 with a controlled B variant.

The three target processes are the existing SDK CLI relay and two copies of the packaged Shipping inner executable. Each client starts with `-RollbackLabUdpPeer=A/B`, one SDK session and one bounded UDP driver. Each displays its own copied world, containing both players. Local input is scripted; this is not an additional human-controlled network game mode. The original dual-view interactive controls remain available in their original mode.

The supervisor first holds a relay-port reservation and starts both SDK sockets with listen port zero. Each client atomically publishes healthy `ready.json` with its actual bound port before any Core step. The SDK keeps that socket bound until teardown. Once both peers are ready, the supervisor releases only the relay reservation and starts the unchanged relay with those exact peer ports. A competing bind in this narrow relay handoff window fails closed; there is no retry that silently substitutes new ports or sockets.

The SDK `PeerDriver` is shared with CLI `run_peer`, polls at most 64 datagrams and advances at most one scripted frame per step. The `engine-udp-v1` aggregate configuration digest binds ABI/protocol/simulation versions, both seeds and target frame before input exchange. ABI is not a separate remote wire field. Protocol v1, Replay v1 and the legacy CLI profile remain unchanged. This transport is real loopback UDP; the transport seed is part of its identity, not a promise of the seeded emulator's packet schedule.

Normal completion verifies distinct PIDs/ports, one session per client, the requested profile, equal confirmed hashes and equal peer replay bytes, then verifies both replays through the canonical CLI. It requires aggregate prediction/correction across the pair and an actual local correction capture where correction happened. A client with zero rollback need not produce a fake correction image. UDP report counters/identities can differ with scheduling and must not be compared as byte-stable seeded reports.

The supervisor sends `RLSTOP` to the relay and tears down exact owned process handles. An outer Win64 job owns the complete descendant tree even if the supervisor fails. Raw child journals, per-client ready/report/replay/trace/captures and group `summary.json` stay in the printed `artifacts/ue5-0.2/runs/<run>-udp-ue-<case>/` directory. No OS security setting or firewall prompt is modified.

Two normal working-source runs passed with confirmed 240, hash `0x4B35DC3FD8F6009C`, equal verified peer replays and all three targets exited. They recorded 153 and 141 aggregate rollbacks; the second had all corrections on B and five genuine screenshots. All six real UDP negatives now pass: MissingPeer, ProtocolMismatch, SimulationMismatch, AbiMismatch, Watchdog and Desync. PACT-85 functional closure is achieved; final clean-source matrix/artifacts/audit/PR remain required. See [UE testing](UE5_TESTING.md) and the [bounded UDP plan](../tasks/20260904-100800-ue5-live-integration-0.2/PACT-85-plan.md).

## Processes and evidence

One repository lock serializes UE pipelines. Each launched root begins suspended inside an owned Win64 job; descendants, logs, watchdog and exit status belong to that invocation. Cleanup never targets processes by name. Engine files are read-only. TEMP, cache, UAT, DDC, shader work, Editor UserDir, crash logs, screenshots and reports are repository-local, except the explicit user allowance for stock UBT `Trace.uba` and backups.

Automation runs with process-local English culture because stock UE UnifiedError smoke assertions compare English strings; no tests are disabled. Configuration is not written to user settings. Packaging uses Pak files and disables Zen Store rather than mixing incompatible storage modes. Default engine plugins are disabled; the explicit ACLPlugin dependency is required by stock default animation-compression assets, while EnhancedInput remains off and GameplayCameras is not a dependency.

The original Editor baseline was 28/28 Automation; the UDP extension now passes 32/32, including four new UDP ownership/mode/deadline tests. The working-source Shipping checkpoint additionally passed real BuildCookRun, ordinary physical-window controls and rendered smoke with sample hash `0x4B35DC3FD8F6009C`, report identity `0x8150FEDA020B66A8`, 189 rollbacks and a verified replay. All three Shipping captures were inspected, and bootstrap/game processes exited cleanly. The refreshed plugin passed all three BuildPlugin configurations, and the minimal plugin profile passed 28/28 Automation. The external CLI/C11/UE verifier passes all 16 checks, including complete report/trace equality and byte-identical verified replays. This completes the intermediate end-to-end functional checkpoint; final clean-source artifacts, audit and PR acceptance remain tracked in [UE testing](UE5_TESTING.md) and the [verification matrix](../tasks/20260904-100800-ue5-live-integration-0.2/verification_matrix.md).

Shipping was selected after ordinary Development startup exposed stock UE trace control on port 1985 and triggered a Windows firewall prompt. No prompt approval or OS/security change was made. Stock Shipping sets `UE_TRACE_ENABLED=0`; Engine files were not edited. The shared `-notraceserver` launch argument prevents automatic trace-server launch for Editor tools, but does not disable Development trace control.

The seeded dual-view path and real UDP client path both remain available. PACT-85 has verified normal three-target runs and all six negative cases; final clean-source acceptance remains required. The [CLI UDP demo](UDP_DEMO.md) shares the production peer driver. None of these loopback checks establishes WAN production readiness.

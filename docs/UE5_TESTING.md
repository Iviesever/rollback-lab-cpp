# UE 5.8 testing

UE acceptance checks the actual DLL, engine lifecycle, input path, rendering and packaged executable. Core tests alone cannot establish those properties. The [verification matrix](../tasks/20260904-100800-ue5-live-integration-0.2/verification_matrix.md) records each final gate; the [PACT-83 evidence](../tasks/20260904-100800-ue5-live-integration-0.2/evidence/PACT-83.md) records the current Editor baseline.

## Commands

Run these serially from repository PowerShell 7 with a clean checkout and a read-only UE 5.8 installation:

```powershell
$EngineRoot = '<UE 5.8 root>'
./scripts/BuildUnrealDemo.ps1 -EngineRoot $EngineRoot
./scripts/GenerateUnrealContent.ps1 -EngineRoot $EngineRoot
./scripts/TestUnrealIntegration.ps1 -EngineRoot $EngineRoot
./scripts/BuildUnrealPlugin.ps1 -EngineRoot $EngineRoot

# Optional one-time cold shader preparation; it is separate from smoke.
./scripts/LaunchUnrealArena.ps1 -EngineRoot $EngineRoot -Warmup
./scripts/LaunchUnrealArena.ps1 -EngineRoot $EngineRoot -Smoke
./scripts/TestUnrealArenaFailures.ps1 -EngineRoot $EngineRoot -IncludeWatchdog

./scripts/PackageUnrealDemo.ps1 -EngineRoot $EngineRoot
$revision = (git rev-parse HEAD).Substring(0, 12)
$Demo = "artifacts/ue5-0.2/demo/0.2.0-$revision-shipping"
./scripts/LaunchPackagedUnrealArena.ps1 -EngineRoot $EngineRoot -DemoRoot $Demo
./scripts/LaunchPackagedUnrealArena.ps1 -EngineRoot $EngineRoot -DemoRoot $Demo -Smoke
./scripts/TestUnrealArenaFailures.ps1 -EngineRoot $EngineRoot -DemoRoot $Demo -IncludeWatchdog
./scripts/VerifyUnrealIntegration.ps1 -SdkRoot "artifacts/sdk/0.2.0-$revision/install" -PluginRoot "artifacts/ue5-0.2/plugin/0.2.0-$revision" -DemoRoot $Demo -SmokeRun '<smoke run directory>'
```

`PackageUnrealDemo` reuses the Editor build and content generator before BuildCookRun. The ordinary launch defaults to interactive mode. Exit it with Esc before starting smoke. `-ExitAfterSeconds 10` can bound an ordinary startup check; it does not replace an interactive check. Automation uses NullRHI; screenshot smoke uses real rendering.

## Current Automation baseline

PACT-83 passed **28/28**, with zero warnings, failed tests or skipped tests:

| Group | Cases | What it exercises |
| --- | ---: | --- |
| Bridge loader | 7 | Missing/malformed manifest, version/source/manifest hash mismatch, missing DLL and DLL hash mismatch |
| Bridge clock | 3 | Bounded catch-up, pause/single-step and invalid delta rejection |
| Bridge runtime | 3 | Independent handles, real correction/convergence/artifacts and reset |
| Bridge lifecycle | 5 | External DLL preload, shared lease, subsystem deinitialize, pre-exit and three real PIE restart cycles |
| Arena model | 7 | Modes, sample parity, interaction, presets, confirmed desync, invalid settings and borrowed lifetime |
| Actual PlayerController input | 1 | A fourth real PIE lifecycle; held movement, short D/Space taps, attack, pause, exact step, reset and teardown |
| Generated content | 2 | Reopened map and native unlit tint material |

The later PACT-84 rerun using the minimal engine-plugin profile also passed 28/28, with zero failed or not-run tests. The input test first failed on short taps with the real PlayerController. The bounded pending-button mask now retains presses until a fixed step consumes them. All four real PIE lifecycles check that SDK sessions, drivers and library leases return to baseline. These are actual engine-world start/end checks, not only repeated native constructor calls.

## Smoke must prove a run

The default scripted sample confirms 240/240 with hash `0x4B35DC3FD8F6009C`, 189 rollbacks, 857 resimulated frames and report identity `0x8150FEDA020B66A8`. PACT-83 observed this in the Editor and externally verified Replay v1. Start, correction and convergence images were opened and inspected. The later Shipping checkpoint also passed BuildCookRun in 53.22 seconds with zero Warning/Error diagnostics, then reached the same confirmed sample result in rendered packaged smoke. Both bootstrap/game processes exited, and all three Shipping images were opened and inspected. These are intermediate-source results; final artifacts must be regenerated together from one clean source.

A passing packaged smoke requires a successful root process and complete child teardown, the requested full-width seeds, loaded SDK/source identity, prediction, sent/delivered packets, real corrections, target confirmation on both peers, equal final hashes and a verified replay. It writes `ue-trace.json`, `report.json`, `input.rlr` and `captures/{start,correction,convergence}.png` in its printed run directory. The ordered UE JSON embeds the actual Core report and trace. Before its start capture, smoke waits for the floor/player components to finish PSO precaching and have ready scene proxies/render state; this avoids accepting a blank first image while preserving the same watchdog.

The external verifier reparses nested JSON, compares CLI/C API/UE reports and identities, checks replay bytes and reconstruction, validates PNG files, and validates SDK/Plugin/Demo manifests, all three complete ZIP inventories and SHA-256. `VerifySdk` separately exercises installed C11/C++ consumers. The artifact/evidence regression suite has passed 44 cases, including the SDK archive checks. Its `artifacts/ue5-0.2/parity/<run>/verification.json` is the machine-readable result for those exact inputs. A screenshot file check does not replace human visual inspection.

The current intermediate verifier result passes all 16 checks: SDK/plugin/demo inventories and ZIPs, exact bootstrap inputs and clean process exit, staged SDK identity, CLI and C11 execution, deep equality of all three canonical reports/traces, byte-identical replays, three independent CLI replay verifications and unchanged source identity during verification. Evidence: `artifacts/ue5-0.2/parity/20260904-134325-034-98663f04/verification.json`. Its `source_clean=false` is explicit; this proves the working-source functional path, not final clean-source release acceptance.

## Ordinary Shipping control checkpoint

The actual packaged window was paused at logical tick 5,360. Holding D + Space and pressing period advanced exactly to 5,361, moved A right and produced one projectile. Releasing those keys and stepping to 5,362 left A stationary with the same single projectile. The ordinary process then completed its bounded 150-second run with root exit 0, no timeout and both owned processes exited. No firewall prompt appeared in this Shipping run.

The earlier ordinary Development launch triggered a Windows firewall prompt from stock trace control on port 1985. No security-dialog action or OS setting change was taken. Shipping uses the engine standard `UE_TRACE_ENABLED=0` configuration. `-notraceserver` prevents automatic Editor trace-server launch; it is not a Development trace-control disable switch.

## Negative paths and process ownership

Six PACT-83 Editor real-process negatives passed: zero target frames, numeric seed suffix, uint64 overflow, early exit, wrong requested source SHA, and an actual screenshot watchdog under NullRHI. Each returned exactly 1, wrote a false failure trace and left no owned child process. The inner smoke watchdog remains 45 seconds; the outer normal smoke process bound remains 240 seconds. Cold shader warmup is a separate operation and cannot extend those acceptance budgets. The failure runner also accepts `-DemoRoot` to exercise these cases in the actual packaged executable; its packaged results must be recorded separately, and are not inferred from the earlier Editor negatives.

The native process runner starts the root suspended, assigns it to a Win64 job, then resumes it. It owns descendants, writes direct logs, preserves the root exit status and checks job completion. It does not terminate processes by name. Only one repository UE pipeline may run at a time. Separate script regressions cover quoting, nonzero exit propagation, descendants, timeout, failed-start handle stability and guarded artifact paths.

## Build and release gates

BuildPlugin checks UnrealEditor Development, UnrealGame Development and UnrealGame Shipping. The default demo acceptance configuration is Win64 Shipping through real BuildCookRun, ordinary packaged startup and packaged smoke. Successful Editor tests do not prove cooked map inclusion, package DLL staging, executable startup or the shipped runtime environment.

Final evidence must bind SDK, Plugin and Demo to one exact clean Git SHA. Dirty diagnostic builds, earlier PACT results, stale screenshots and old ZIPs cannot satisfy that gate. Review raw logs for Error/Warning diagnostics and retain their classification. The SDK uses Release `/MD`; actual debug-CRT UE targets are rejected. Windows LeakSanitizer is unsupported, while Core ASan+UBSan remains enabled in the [Core matrix](TESTING.md).

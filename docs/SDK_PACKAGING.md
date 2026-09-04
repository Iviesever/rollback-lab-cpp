# SDK and engine artifact packaging

Run from a clean repository checkout in PowerShell 7:

```powershell
./scripts/BuildSdk.ps1
$revision = (git rev-parse HEAD).Substring(0, 12)
$Sdk = "artifacts/sdk/0.2.0-$revision/install"
./scripts/VerifySdk.ps1 -SdkRoot $Sdk
```

`BuildSdk` discovers MSVC, sets repository-local temporary paths, configures Ninja Release with `/MD`, runs all six CTest entries, installs, hashes and archives the SDK. It refuses a dirty source tree by default. `-AllowDirty` labels an intermediate artifact with `-working`; such an artifact cannot satisfy final evidence or the UE bridge's clean SDK contract.

## What the SDK contains

| File/target | Purpose |
| --- | --- |
| `include/rollback_lab/c_api/rollback_lab_c.h` | Genuine C11 public boundary |
| `bin/rollback_lab_c.dll` | Shared implementation of the C ABI and owned C++ Core objects |
| `lib/rollback_lab_c.lib` | Windows import library; it identifies DLL imports, not a static copy of Core |
| `lib/rollback_lab_core.lib` | Original static C++ Core for consumers that accept its C++/toolchain contract |
| `bin/rollback_lab.exe`, `bin/rollback_lab_c_demo.exe` | CLI and artifact-producing C11 consumer |
| `lib/cmake/rollback_lab/` | Exported targets and package config |
| `LICENSE`, `README.md`, `README-SDK.md` | License and usage information |
| `manifest.json`, `checksums.sha256` | Complete payload identity and integrity inventory |

Consumers use `find_package(rollback_lab 0.2.0 EXACT CONFIG REQUIRED)` and the exported `rollback_lab::rollback_lab_c` or `rollback_lab::rollback_lab` target. `VerifySdk` configures a separate consumer project outside the SDK's original build directory, builds genuine C11 and C++23 executables and runs both with a source-SHA check. Their two CTest entries are separate from the main six-entry suite.

## Runtime and compatibility

The supported engine SDK is Release, Win64 x64, MSVC, shared linkage, `/MD`. The CRT is the C/C++ runtime providing facilities such as allocation and standard I/O. The DLL creates and destroys its own objects, so Unreal never frees SDK allocation with a different allocator. C ABI isolates C++ class/STL layout; it does not remove CPU architecture, OS runtime prerequisites, API semantics or deployment requirements.

UE Development and Shipping use this Release SDK. Build.cs rejects actual debug-CRT targets. Release-CRT DebugGame is permitted by that guard but is not part of the three verified BuildPlugin combinations. Consumers of the static C++ Core must match their toolchain/CRT/configuration deliberately. Other platforms and engine versions are not verified integration targets.

## Integrity and source identity

The SDK manifest records schema/SDK/API/simulation/protocol/replay versions, exact source SHA and cleanliness, compiler version, x64 architecture, Release configuration, `/MD`, shared linkage, relative payload paths and SHA-256 for each payload. The checksum list also covers the manifest. The adjacent SDK ZIP has its own `.sha256` file.

`VerifySdk` rejects wrong identity/version/CRT, absent or altered payloads, unsafe or duplicate paths, extra files, incomplete checksum lists and ZIP contents that differ from the install tree. SHA-256 provides integrity relative to a trusted manifest; it is not a signature and cannot authenticate an attacker-replaced file set and checksum set.

## UE plugin and demo

```powershell
$EngineRoot = '<UE 5.8 root>'
./scripts/BuildUnrealPlugin.ps1 -EngineRoot $EngineRoot -SdkRoot $Sdk
./scripts/PackageUnrealDemo.ps1 -EngineRoot $EngineRoot -SdkRoot $Sdk
```

The scripts stage the verified exact-source SDK automatically. BuildPlugin produces Editor Development, Game Development and Game Shipping plugin builds. `PackageUnrealDemo` builds the native Editor tools, generates map/material content, and uses real Win64 BuildCookRun to build, cook, stage, create Pak files and archive the demo. Shipping is the default and final demo acceptance configuration. The working-source Shipping checkpoint passed BuildCookRun, ordinary interactive startup and rendered smoke. Development remains available explicitly, but its trace-control startup triggered a Windows firewall prompt in the tested ordinary launch. Shipping uses the stock engine configuration with UE tracing disabled; no engine or OS/firewall setting was changed.

Default deliverable locations are source-specific:

```text
artifacts/sdk/0.2.0-<HEAD12>/install/
artifacts/sdk/0.2.0-<HEAD12>/rollback_lab-sdk-0.2.0-<HEAD12>-win64.zip
artifacts/ue5-0.2/plugin/0.2.0-<HEAD12>/
artifacts/ue5-0.2/plugin/0.2.0-<HEAD12>.zip
artifacts/ue5-0.2/demo/0.2.0-<HEAD12>-shipping/
artifacts/ue5-0.2/demo/0.2.0-<HEAD12>-shipping.zip
```

Plugin/Demo archives have adjacent `.sha256` files. Each tree contains a version/source/configuration/engine/ABI manifest and complete checksum list. Verification rejects path traversal, junctions/symlinks, colliding paths, missing/unmanifested files, altered ZIP bytes and missing required EXE/DLL/manifest payloads. Plugin evidence includes all three build configurations. The runtime DLL and SDK manifest are staged as NonUFS files beneath the packaged plugin.

Final SDK, plugin and demo must bind to the same exact **clean** Git HEAD. Commit reviewed changes first, rebuild all three, then run ordinary launch, packaged smoke and `VerifyUnrealIntegration`. That verifier checks the SDK ZIP as well as plugin/demo ZIP bytes and writes `artifacts/ue5-0.2/parity/<run>/verification.json`; its printed path and the [task matrix](../tasks/20260904-100800-ue5-live-integration-0.2/verification_matrix.md) identify accepted artifacts. Do not substitute an earlier PACT ZIP because it has the same product version.

Generated binaries, UE content, logs, caches and archives stay in ignored repository directories and are not committed. Engine files remain read-only. Stock UBT's `Trace.uba` and backups are the sole user-approved external-cache exception; all other task output stays inside the repository.

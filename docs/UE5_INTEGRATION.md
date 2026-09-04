# UE 5.8 integration

The Runtime plugin lives under `examples/ue5/RollbackArena/Plugins/RollbackLabBridge`.
It calls only the installed C ABI header and shared DLL. Core C++ implementation
files are not copied into the UE module; the original SDK remains their owner.

## Build and validate

From PowerShell 7, supply the read-only UE 5.8 installation directory:

```powershell
./scripts/BuildUnrealDemo.ps1 -EngineRoot '<UE 5.8 root>'
./scripts/TestUnrealIntegration.ps1 -EngineRoot '<UE 5.8 root>'
./scripts/BuildUnrealPlugin.ps1 -EngineRoot '<UE 5.8 root>'
```

The scripts automatically stage a verified SDK for the current exact Git HEAD.
If no such SDK exists, BuildSdk must build it from a clean source tree. Staging
copies generated files under the plugin's ignored `Binaries/ThirdParty/RollbackLab`
directory; no manual DLL copy is required. BuildPlugin builds Editor Development,
Game Development and Game Shipping with the stock UE tool.

## Loading and ownership

Build.cs validates all SDK manifest payload hashes and versions, includes the C
header, names the import library, and stages the DLL/manifest as NonUFS runtime
dependencies. The bridge uses explicit dynamic export resolution rather than
static import calls. Before creating any handle, it validates the compiled
manifest SHA, source identity, architecture/CRT, DLL SHA, every required export,
and the loaded SDK's version/capabilities/source SHA. Invalid SDKs fail closed.

Each native Runtime owns a live driver and two different opaque session handles.
The driver borrows the sessions and is destroyed first; session destruction then
occurs inside the SDK, before the final DLL lease is released. A subsystem owns
the native Runtime through a UE unique pointer and stops it during Deinitialize.
Module pre-exit and shutdown stop every remaining registered runtime. Calls are
restricted to the creating game thread.

UE 5.8's Windows GetDllHandle helper can return GetModuleHandle's borrowed handle
when an image is already loaded. The bridge therefore acquires its own Win64
LoadLibraryExW reference with the verified full path. Tests cover simultaneous
runtimes, repeated stops, pre-exit, and a library preloaded by an external owner.
Destroying one runtime cannot unload code still needed by another.

## Tool process boundaries

One repository lock serializes UE tool pipelines. A Win64 job owns each launched
tool and its complete descendant tree, beginning while the root is suspended.
Logs go directly to files; root exit codes survive child/crash-monitor cleanup.
Watchdogs fail nonzero, and successful orchestration requires zero active job
processes. Cleanup never targets other projects by process name.

TEMP, build cache, UAT logs, DDC, shader work, Editor UserDir, crash-reporter log
and reports are redirected into the repository. The stock UBT global Trace.uba
path has no supported redirect; the user explicitly permits only that trace and
its backups in the default user cache. Engine files remain read-only.

Automation runs with process-local English culture because stock UE5.8 UnifiedError
smoke tests compare English strings. No tests are disabled. Configuration files
are not written to user settings. The repository's packaging configuration
disables Zen Store when using ordinary Pak files rather than IoStore.

The five-configuration core suite and current engine acceptance status are
recorded in the task verification matrix. Standalone BuildPlugin and focused
Automation do not substitute for the later cooked, packaged and interactive gates.

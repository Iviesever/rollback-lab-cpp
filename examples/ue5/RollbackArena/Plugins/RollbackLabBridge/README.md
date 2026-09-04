# RollbackLab Bridge 0.2.0 Candidate

The Win64 UE 5.8 Runtime module calls the staged RollbackLab C ABI v1. It does not
compile Core sources or define simulation, prediction, transport, rollback or
hash algorithms. Stage the complete verified Release x64 `/MD` SDK install tree
under `Binaries/ThirdParty/RollbackLab` before invoking UBT or BuildPlugin.

`RollbackLabBridge.Build.cs` verifies the SDK manifest and listed file checksums,
sets the public C include path and import-library path, and embeds the expected
source SHA and manifest SHA-256. The import library is configured explicitly,
while calls use a complete `GetDllExport` function table. There are no direct
import calls or delay-load policy. This allows missing exports to fail before
calling the SDK. RuntimeDependencies stages the DLL and manifest as NonUFS files.

Runtime checks the manifest schema, SDK/ABI and canonical format versions,
clean source identity, architecture/CRT/linkage, manifest digest and DLL digest
before loading. It then checks all exports and the DLL's reported source SHA,
versions and capabilities. SHA-256 uses the bundled OpenSSL implementation.
Development, Shipping and release-CRT DebugGame consume the Release SDK;
debug-CRT engine targets are rejected.

`URollbackLabSubsystem::GetRuntime()` exposes a game-thread-affine native RAII
wrapper. `Start` creates two independent session resources and a live driver
that borrows them. Each native resource holds a shared DLL lease. Stop,
Deinitialize, destruction and module pre-exit release the driver before its
sessions, then unload the DLL after the final lease. Dynamic module reload is
disabled. `Reset` reconstructs the most recently validated scenario and clears
clock, metrics and correction revisions.

Each SDK lease acquires an owned Win64 `LoadLibraryExW` reference using the
verified absolute DLL path and the DLL-directory/default-directory search flags.
The final lease releases exactly that reference. UE 5.8's `GetDllHandle` can
return a borrowed `GetModuleHandle` result for an already loaded path; pairing
separate borrowed handles with unconditional unload caused the first two-runtime
pre-exit test to access an unloaded SDK. The explicit owned reference also
preserves a DLL loaded earlier by an external consumer. The lifecycle tests keep
the two-runtime and external-preload cases as regression coverage.

`TickWallClock` advances at 1/60 second with at most eight logical steps per call.
It discards excess whole-step wall debt observationally and retains a fractional
remainder. Pause adds no wall debt. `SingleStep` executes exactly one live-driver
tick while preserving pause and fractional debt. Integer canonical state never
receives wall-clock values. A finished or failed runtime does not advance;
failure keeps the resources available for `CopyTrace` until `Stop`.

`GetPeer` returns cached copied presentation records. CorrectionRevision changes
only after an actual SDK correction and identifies when to show a ghost/flash.
The numerical HandleIdentity is a process-local ownership diagnostic and must
never enter scenario, hash, trace or replay identity. Report, trace and replay
copy methods preserve the SDK's serialized bytes/text using bounded two-stage
buffers. The Editor-only tests are under `RollbackLab.Bridge`.

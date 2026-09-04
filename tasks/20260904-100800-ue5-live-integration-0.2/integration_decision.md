# PACT-80 integration decision

## Bounded spike acceptance

Compile a C-callable DLL against the existing production core; consume it from an independently packaged UE Runtime plugin. BuildPlugin must build Editor Development, Game Development, and Game Shipping, and preserve the DLL in its output. All spike source and output stay in ignored artifacts. A failed prerequisite is recorded, never called a successful spike.

## Alternatives

| Choice | Benefit | Cost / acceptance |
| --- | --- | --- |
| Shared DLL + C ABI (preferred) | STL, exceptions, allocator and CRT implementation stay private; UE only sees fixed C layout | Explicit DLL stage/load and integrity validation; actual BuildPlugin spike required |
| Static library + C ABI | No DLL deployment | CRT/toolset matching and duplicate linkage risk; only permitted if actual package spike establishes DLL staging as a blocker |
| Direct C++/STL interface or copied UE simulation | None within the contract | Rejected: unstable ownership/ABI or duplicate algorithms |

## Locked boundary

- API v1: `rl_` prefix, fixed-width fields, opaque session and live-run handles, status values, API version and struct_size headers, caller-owned outputs and sizing calls. Header compiles as C11.
- Ownership: SDK creates and destroys resources. A live driver borrows two distinct caller-owned session handles; release driver before sessions. Single-thread-affine handles; no concurrent calls, including destruction. UE uses native RAII inside a subsystem; UObject owns the wrapper, never a naked C resource.
- Errors: typed invalid/null/version/size/peer/frame/window/buffer/internal failures. No exception crosses any exported function. Successful queries copy presentation data; no mutable storage pointer escapes.
- Layout: explicit reserved/padding fields and compile-time C/C++ size/offset checks; Win64 x64 little endian, natural 8-byte maximum alignment. No enum layout, bool, size_t, STL, Unreal, allocator or exception ABI.
- Runtime: MSVC release DLL uses dynamic release CRT. UE Development/Shipping consume this same SDK; DebugGame supports release CRT, true debug-CRT Editor is outside the verified combination. Core MSVC Debug remains independently tested.
- Link/stage: SDK install tree includes header, DLL/import library, CMake package, license, README and manifest. UE Build.cs stages DLL/manifest through RuntimeDependencies; startup checks version/source SHA/file SHA before creating sessions. Scripts stage automatically from current clean HEAD.
- Fixed step: accumulator uses 1/60 seconds externally, maximum 8 steps per display tick, clamps excessive debt and reports discarded wall time observationally. Pause adds no debt; step does one logical tick; reset destroys and recreates driver and peers. Floats never enter canonical state.
- Reuse: extract current seeded runner into an incremental production driver; run_seeded_scenario loops that driver. Preserve phase order, 64 tail ticks, 32 drain ticks, report/replay/trace schemas, sample identities and golden hash. Driver accepts scripted or sampled A inputs and projects both independent sessions.
- Automation: C11/C++ consumers, ABI negatives, isolation, 100+ parity sweep, existing matrix, UE Automation/lifecycle/fixed-step cases, real Editor launch and BuildPlugin, BuildCookRun, ordinary packaged launch and smoke with real rollback/screenshots/parity/replay.
- Manifest: source SHA, clean status, SDK/API/simulation/protocol/replay versions, toolchain/CRT/config, relative file paths and SHA-256. Final SDK/plugin/demo/trace manifest references the same clean source HEAD. Checksummed local artifacts are ignored; tracked evidence is portable and relative.
- Non-goals: original objective exclusions, including WAN production, Replication/Iris/Network Prediction, new simulation logic, marketplace content and formal publication.

## Spike result and decision

Choose shared DLL + C ABI. The actual C11 consumer returns production initial hash `479350575277098922`. UE 5.8.0 CL 55116800 BuildPlugin completed Editor Development, Game Development, Game Shipping with exit 0 in 33 seconds. Original and packaged DLL SHA-256 both equal `E909A3A63C28B9D12ACB992E10F7138F16E621D5FF72C093AC6705579D9481ED`. No static fallback is justified.

SDK spike uses MSVC 19.51 with /MD; Unreal selects MSVC compiler 14.44.35228 from tool directory 14.44.35207. C ABI isolates those C++ implementations. An initial export-declaration mismatch failed compilation and was corrected by applying import/export attributes consistently in the C header; no core change occurred.

Raw evidence: `artifacts/ue5-0.2/spike/`. All three manifests contain plugin products; the Engine game executable timestamps remain the installed July build. UAT log folders and configuration cache are redirected to repository paths. UE 5.8 always uses its UBA executor even with -NoUBA and emitted a default user-cache trace outside the repository during discovery. Source inspection confirmed no normal-entrypoint override for that global trace. The user explicitly authorized a narrow exception for `UnrealBuildTool/Trace.uba` and its backups; UBA storage is still redirected using `UBA_ROOT`, and every other output remains in the repository. The spike proves DLL staging, not completed runtime/package acceptance.

# RollbackLab SDK 0.2.0 Candidate

This package contains a Win64 shared C ABI v1 (`bin/rollback_lab_c.dll`,
`lib/rollback_lab_c.lib`, `include/rollback_lab/c_api/rollback_lab_c.h`) and the
original C++23 static core (`lib/rollback_lab_core.lib`). Simulation, protocol,
and replay versions remain 1. This is an educational release candidate.

Use PowerShell 7 to build and verify from the source repository:

```powershell
./scripts/BuildSdk.ps1
./scripts/VerifySdk.ps1 -SdkRoot artifacts/sdk/0.2.0-<source-sha-prefix>/install
```

The manifest binds the source Git SHA, source cleanliness, compiler, Release
configuration, dynamic CRT (`/MD`), and every payload hash. `checksums.sha256`
also covers the manifest. The adjacent ZIP has its own `.sha256` checksum file.
`-AllowDirty` exists for intermediate development evidence and cannot satisfy
the final artifact gate. Regenerate from the final clean source commit.

An independent CMake consumer uses:

```cmake
find_package(rollback_lab 0.2.0 EXACT CONFIG REQUIRED)
target_link_libraries(my_c_or_cpp_app PRIVATE rollback_lab::rollback_lab_c)
# C++23 applications may instead or additionally use the original static core:
target_link_libraries(my_cpp_app PRIVATE rollback_lab::rollback_lab)
```

Pass the install directory as `CMAKE_PREFIX_PATH`. Keep `rollback_lab_c.dll`
beside the application or on its controlled DLL search path. The Windows
Microsoft Visual C++ x64 runtime must be installed. C ABI consumers need no
C++ class/STL ABI agreement; static-core consumers must match compiler/CRT and
build configuration. The SDK is MSVC x64 Release `/MD`, intended for
UE Development/Shipping; a debug-CRT UE build is unverified. Consult the
repository's current UE verification matrix for completed engine tests.

Zero-initialize each input/output struct and set `api_version=RL_API_VERSION`
and `struct_size=sizeof(the exact type)`. Check every returned `rl_status`.
Create/destroy handles in the SDK, on one owning thread. No concurrent calls
are supported. The live driver borrows two independent, untouched sessions;
destroy it before the sessions. Snapshots are copied presentation values;
never mutate a simulated world through renderer or engine state.

The exported header documents layout, ownership, input bounds, sizing calls,
and all status values. The repository's `tests/c_api/consumer` demonstrates
standalone C11/C++ linkage, with runtime source-SHA checks.

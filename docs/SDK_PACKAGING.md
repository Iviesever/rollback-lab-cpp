# SDK packaging

Run PowerShell 7 from the repository:

```powershell
./scripts/BuildSdk.ps1
./scripts/VerifySdk.ps1 -SdkRoot artifacts/sdk/0.2.0-<source-sha-prefix>/install
```

BuildSdk discovers MSVC, sets repository-local process temporary paths, configures
Ninja Release with `/MD`, runs all CTest cases, installs, hashes and archives the
SDK. It refuses a dirty source tree by default. `-AllowDirty` is for explicitly
labelled intermediate artifacts only and cannot satisfy final evidence.

The install tree contains C/C++ headers, shared C ABI DLL, import library, original
static core, CLI and C11 demo, CMake exported targets/config, license, repository
README, standalone `README-SDK.md`, `manifest.json`, and `checksums.sha256`.
Generated SDK binaries remain under ignored artifacts and are not committed.

The manifest records schema/SDK/API/simulation/protocol/replay versions, exact
source SHA and cleanliness, toolchain version, x64 architecture, Release
configuration, `/MD` CRT, shared linkage, relative paths and each payload SHA-256.
The checksum list also covers the manifest; the adjacent ZIP has a separate
`.sha256` file. SHA-256 detects accidental modification against the manifest; it
is not a signature or an authenticity claim against an attacker who replaces all
files and checksums together.

VerifySdk rejects wrong identity/version/CRT, missing or altered payloads, unsafe
or duplicate paths, extra files, incomplete checksum lists, and ZIP contents that
differ from the install tree. It configures a separate `find_package(rollback_lab
0.2.0 EXACT CONFIG REQUIRED)` project, builds genuine C11 and C++23 consumers, and
executes both with a runtime source-SHA check. This verifies exported target
usability beyond the original build directory.

The shared boundary isolates C++ class/STL and allocator ownership. It does not
eliminate CPU architecture requirements, the Windows runtime prerequisite, API
semantic compatibility, or the need to stage the correct DLL. Applications that
link the original static core must still match toolchain, CRT and configuration.
UE consumes the Release SDK even in its Development configuration; a true
debug-CRT UE configuration is outside the verified integration combination.

# PACT-00 Bootstrap Evidence

## Contract

- Objective: establish a reproducible C++23 baseline on `main` before feature work.
- Gap: the target directory was empty and the GitHub repository did not exist.
- Guardrail: no netcode feature implementation on `main`; no other repository touched.
- Done when: MSVC Debug config/build/CTest passes, baseline is pushed to public `origin/main`, and the feature branch points at that exact base.

## RED

Environment: Visual Studio 2026 Developer PowerShell, MSVC 19.51.36248, CMake 4.1.2, Ninja 1.13.1.

Command:

```powershell
cmake --preset msvc-debug --fresh
cmake --build --preset msvc-debug
```

Observed expected failure:

```text
tests/test_smoke.cpp(3): fatal error C1083: cannot open include file:
'rollback_lab/version.hpp': No such file or directory
ninja: build stopped: subcommand failed.
```

An earlier command-line quoting failure never reached CMake and is explicitly excluded from RED evidence.

## GREEN

Command:

```powershell
cmake --preset msvc-debug --fresh
cmake --build --preset msvc-debug
ctest --preset msvc-debug --output-on-failure
```

Observed:

```text
MSVC 19.51.36248.0
rollback_lab_tests.exe linked successfully
1/1 Test #1: rollback_lab_tests ... Passed
100% tests passed, 0 tests failed out of 1
```

Warnings are elevated to errors for project targets.


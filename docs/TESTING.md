# Testing

## Local commands

```powershell
cmake --workflow --preset msvc-debug-ci
cmake --workflow --preset msvc-release-ci
ctest --preset msvc-debug --output-on-failure
build\msvc-debug\rollback_lab_protocol_fuzz_smoke.exe
build\msvc-debug\rollback_lab.exe verify --frames 600
build\msvc-debug\rollback_lab.exe udp-demo --frames 120
```

Clang and sanitizer presets are described in `CMakePresets.json`. `tools/bootstrap-llvm.ps1` obtains the audited portable LLVM 22.1.8 archive without a system install; `tools/run-verification.ps1 -Full` runs the complete local matrix. CI is supplementary.

## Coverage map

| Area | Representative invariants |
| --- | --- |
| Simulation | repeated/golden hashes, wrap math, checked overflow, arena clamp, capacity, damage, score, respawn, stable IDs |
| Rollback | zero latency, matching prediction, earliest dirty coalescing, depth 120/121, ring wrap, no double effect, independent convergence, exact metrics |
| Protocol | all packet fields, every truncation, bad header/count/length/CRC, random bytes, sequence wrap |
| Transport | 0/1/5/20% loss, latency, jitter, reorder, duplicate, burst, queue overflow, age, bandwidth, repeated identity |
| Replay/report | strict versions/CRC/order, checkpoints, final hash, fixed JSON order, timing-excluded identity |
| Desync | no speculative false positive, exact confirmed divergent boundary, bounded diagnostic |
| UDP | separate PIDs/ports, relay, convergence, reports/replays, bind conflict, missing peer, protocol mismatch, child reap |
| Viewer | embedded real trace, no external resources, required controls/markers, empty-trace failure, browser console/interactions/responsiveness |

The lightweight test runner has 56 cases: 22 core/session, 15 protocol/transport, 12 replay/report/CLI/viewer, and 7 real UDP. CTest runs that executable, the structured 100,000-input packet/replay fuzz smoke, and a 100-seed property smoke as three tests.

## Property sweep

The authoritative MSVC Release run completed two full, equal 10,000-seed sweeps in 96.953 seconds. Per sweep: 9,600 normal scenarios converged; 200 explicit queue-overflow and 200 explicit max-age timeout scenarios returned the exact expected type; 0 crashes/deadlocks/unbounded failures/identity mismatches. It covers 1/32/120/121/255/256/257-frame edges and loss buckets 0/1/5/20% containing 2,741/2,400/2,377/2,482 scenarios. See `evidence/PACT-70/property-sweep-10000.json`.

## Verified local matrix

| Configuration | Result |
| --- | --- |
| MSVC 19.51 Debug | 3/3 CTest passed, 14.38 s |
| MSVC 19.51 Release | 3/3 CTest passed, 5.18 s |
| Clang 22.1.8 Debug | 3/3 CTest passed, 14.44 s; golden hash matches |
| Clang 22.1.8 Release | 3/3 CTest passed, 4.36 s |
| Clang 22.1.8 RelWithDebInfo + ASan + UBSan | 3/3 CTest passed, 9.20 s |
| MSVC Release CMake clean target + rebuild | 117 outputs cleaned; 3/3 passed, 5.36 s |

Windows LeakSanitizer reports itself unsupported; `ASAN_OPTIONS=detect_leaks=0` is explicit. ASan and UBSan remain enabled with halt-on-error, including fuzz/property smoke and UDP integration.

## Failure discipline

Every behavior begins with a RED test. A second failed patch triggers renewed root-cause investigation; a third attempt requires a root-cause packet. Evidence is stored under `tasks/20260903-215400-rollback-netcode-0.1/evidence/` with exact commands and precursor/final SHAs.

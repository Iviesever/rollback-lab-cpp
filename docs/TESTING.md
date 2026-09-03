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

Clang/GCC and sanitizer presets are described in `CMakePresets.json`. PACT-70 evidence records the exact locally available matrix; CI is supplementary.

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

The lightweight test runner prints the number of behavior cases; CTest treats it and the standalone fuzz smoke as separate tests. The final evidence matrix, not this prose, is authoritative for exact counts and toolchain results.

## Property sweep

PACT-70 runs 10,000 bounded scenario/transport seeds. A run is valid only if each seed either converges or returns one of the explicitly declared typed failures, with no crash, deadlock, queue growth, or nondeterministic repeat identity.

## Failure discipline

Every behavior begins with a RED test. A second failed patch triggers renewed root-cause investigation; a third attempt requires a root-cause packet. Evidence is stored under `tasks/20260903-215400-rollback-netcode-0.1/evidence/` with exact commands and precursor/final SHAs.


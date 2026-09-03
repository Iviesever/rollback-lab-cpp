# PACT-70 Final-Source Hardening Evidence

## Identity

```text
Final source SHA: 88134266c2bbb8a68015b148b2bf9ebd24289b80
Branch: feat/rollback-netcode-0.1
Base: b1671c9f162a92512aea23040a309d4f27003912
MSVC: 19.51.36248 x64
Clang: 22.1.8, x86_64-pc-windows-msvc, ca7933e47d3a3451d81e72ac174dcb5aa28b59d1
CMake/Ninja: 4.1.2 / 1.13.1
```

## Independent review

Three read-only review rounds covered `origin/main..source HEAD`. The first found three Critical and seven Important gaps. Fixes added real zero-latency phase semantics, dirty confirmation atomicity, strict property oracles, bounded hash windows, peer-local UDP diagnostics, strict hello payload, force-reap, structured fuzz, trace budgeting, and complete report identity. The second requested full sweep repeat and UDP peer-local evidence. The third review of `ce7101d` returned **Yes**, with no Critical/Important findings. `8813426` only changes supervisor error precedence to preserve an already-written desync diagnostic; the exact change is covered by the final matrix.

## Portable LLVM provenance

```text
Official release: llvmorg-22.1.8 (non-prerelease, 2026-06-16)
Archive: clang+llvm-22.1.8-x86_64-pc-windows-msvc.tar.xz
Bytes: 862053924
SHA256: d96c2cc1736f4eb7fa43cb9bbdf56d93551a9ae0a9aadb9c99c3c3b2b712a234
GitHub attestation verify: exit 0
Install mode: extracted under ignored .tools; no installer/system PATH change
```

## Final compiler and sanitizer matrix

| Configuration | Result |
| --- | --- |
| MSVC Debug `/W4 /WX /permissive- /EHsc /utf-8` | 3/3 CTest, 14.38 s |
| MSVC Release | 3/3 CTest, 5.18 s |
| Clang Debug `-Wall -Wextra -Wpedantic -Werror` | 3/3 CTest, 14.44 s |
| Clang Release | 3/3 CTest, 4.36 s |
| Clang RelWithDebInfo ASan+UBSan | 3/3 CTest, 9.20 s |
| MSVC Release CMake clean + rebuild | 117 outputs cleaned; 3/3, 5.36 s |

Each CTest set contains the 56-case production-API runner, structured packet/replay fuzz smoke, and property smoke. The Clang run enforces the same `0xA263C92E66A0AA27` 600-frame golden hash.

Sanitizer environment:

```text
ASAN_OPTIONS=halt_on_error=1:detect_leaks=0
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
```

Windows Clang reports LeakSanitizer unsupported; LSan is not claimed. ASan and UBSan execute the behavior, fuzz, property, viewer-generation, and real UDP tests.

## Strict 10,000-seed full repeat

Command:

```powershell
build\msvc-release\rollback_lab_property_sweep.exe --seeds 10000 --repeat-samples 128 --repeat-full --out tasks\20260903-215400-rollback-netcode-0.1\evidence\PACT-70\property-sweep-10000.json
```

```text
Elapsed for both complete sweeps: 96.953 s
Total seeds per sweep: 10000
Normal converged: 9600
Exact typed failures: 200 queue_overflow + 200 timeout
Full result objects equal: true
Additional repeated identity samples inside each sweep: 522
Identity mismatches/crashes/deadlocks/unbounded failures: 0/0/0/0
Frame edges: 1/32/120/121/255/256/257
Loss 0/1/5/20% scenarios: 2741/2400/2377/2482
Identity digest: 6852576945316271377
```

Every normal scenario uses two production sessions, packet encode/decode, SeededTransport, replay verification, and report identity. Explicit failure cases are repeated and must return the exact declared code. `--repeat-full` reruns all 10,000 and compares every result field, not only the digest.

## Fuzz, UDP, benchmark, and audits

```text
Structured fuzz under MSVC Release: 100000 inputs, 0 crashes
Structured fuzz under Clang ASan+UBSan: 100000 inputs, 0 crashes
Composition: 33334 random packets + 33333 structured packets + 33333 structured replays
CRC rewrites reaching post-integrity parsing: 50000
UDP Release stress: 20/20 passed, 0 residual rollback_lab processes
UDP injected desync: typed desync, nonzero CLI, peer-local diagnostic boundary 91
Browser desktop/narrow: console 0 errors, controls/markers/layout passed
Canonical forbidden-token audit: 0
Relay forbidden-dependency audit: 0
```

MSVC Release benchmark observation:

```json
{"simulation":{"total_ticks":100000,"total_microseconds":7232,"ticks_per_second":13827433},"rollback_stress":{"total_ticks":2000,"total_microseconds":130030,"rollback_count":1389,"resimulated_frames":9749,"maximum_depth":12}}
```

Timing is a local observation, not a cross-machine threshold.


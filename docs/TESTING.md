# Testing

The current main suite has six CTest entries: **61 Core/CLI/UDP behavior cases, 7 C ABI session cases, 4 C ABI live-driver cases, a true C11 consumer, a structured 100,000-input fuzz smoke and a 100-seed property smoke**. Installed C11/C++ consumer checks and UE Automation are separate suites. Use the [0.2 verification matrix](../tasks/20260904-100800-ue5-live-integration-0.2/verification_matrix.md) for final candidate status and [UE testing](UE5_TESTING.md) for engine/package evidence.

## Local commands

From a Visual Studio C++ Developer PowerShell:

```powershell
cmake --workflow --preset msvc-debug-ci
cmake --workflow --preset msvc-release-ci
ctest --preset msvc-debug --output-on-failure
build\msvc-debug\rollback_lab_protocol_fuzz_smoke.exe
build\msvc-debug\rollback_lab.exe verify --frames 600
build\msvc-debug\rollback_lab.exe udp-demo --frames 120 --out artifacts\udp-demo
```

For the complete five-configuration local matrix and the explicitly located full sweep:

```powershell
./tools/bootstrap-llvm.ps1
./tools/run-verification.ps1
New-Item -ItemType Directory -Force artifacts/ue5-0.2 | Out-Null
build\msvc-release\rollback_lab_property_sweep.exe --seeds 10000 --repeat-samples 128 --repeat-full --out artifacts\ue5-0.2\property-sweep-10000.json
```

The bootstrap obtains the version-locked portable LLVM 22.1.8 without a system install. `run-verification.ps1` discovers MSVC and runs Debug/Release, Clang Debug/Release and Clang RelWithDebInfo ASan+UBSan. Its legacy `-Full` option writes the sweep to the 0.1 evidence location; the explicit command above gives this candidate its own output. CI is supplementary to the local engine/artifact gates.

## Suite inventory

| CTest entry | Scope |
| --- | --- |
| `rollback_lab_tests` | 61 cases: 22 Core/session, 15 protocol/transport, 16 report/replay/CLI/viewer/live-scenario, 8 real UDP/process |
| `c_abi_c11_consumer` | Genuine C11 compilation/runtime ABI smoke |
| `c_abi_session_tests` | 7 cases, including 128 delayed-input direct C++/C parity scenarios |
| `c_abi_live_tests` | 4 cases, including 128 complete packet-driven direct C++/C parity scenarios |
| `protocol_fuzz_smoke` | 100,000 structured/random packet and replay inputs |
| `property_sweep_smoke` | 100 deterministic seeds, with repeated samples |

The original 56 behavior cases remain. The five added Core-runner cases cover incremental sample compatibility, interactive replay, invalid/shared live sessions, full-width CLI seeds and relative-path UDP supervision. Do not count scenario iterations as separate unit-test cases or the installed consumer suite as part of six CTest entries.

## Coverage map

| Area | Representative invariants |
| --- | --- |
| Simulation | repeated/golden hashes, wrap math, checked overflow, clamp, capacity, damage, score, respawn and stable IDs |
| Rollback | matching prediction, earliest-dirty coalescing, pre-input boundaries, depth 120/121, tagged ring wrap, no double effect and independent convergence |
| Protocol | all fields/types, every truncation, bad header/count/length/CRC, random input and sequence wrap |
| Transport | 0/1/5/20% loss, delay/jitter/reorder/duplicate/burst, queue overflow, age, bandwidth and repeated identity |
| Replay/report | strict versions/CRC/order, canonical checkpoints/final hash, stable JSON and timing-excluded identity |
| Live integration | existing sample trace/replay byte preservation, local input captured into replay and no shared/used session acceptance |
| C ABI | real C11, version/size/reserved validation, output preservation, typed errors, thread affinity, borrowing, copied-buffer bounds and two parity sweeps |
| Desync | no speculative false positive, earliest confirmed divergence and rejection of two equally wrong peers against canonical replay |
| UDP | separate relay/peer PIDs and ports, handshake, reports/replays, bind conflict, missing peer, version mismatch, relative output and complete child reap |
| SDK/artifacts | independent `find_package`, full manifest/checksum/ZIP validation and unsafe/missing/altered/extra payload rejection |
| UE | loader integrity, owned DLL leases, real PIE/input, fixed-step, copied presentation, native content, RHI captures, packaged process and three-way parity |
| Viewer | actual embedded trace, no external resources, controls/markers, empty-trace failure, bounded sampling and browser interaction |

## Recorded baseline and final refresh

The latest PACT-84 working-source matrix repeats the earlier [PACT-81 coverage](../tasks/20260904-100800-ue5-live-integration-0.2/evidence/PACT-81.md), with all six CTest entries passing in each configuration:

| Configuration | PACT-84 intermediate-source result |
| --- | --- |
| MSVC Debug | 6/6 passed, 60.89 s |
| MSVC Release | 6/6 passed, 16.15 s |
| Clang 22.1.8 Debug | 6/6 passed, 85.64 s |
| Clang 22.1.8 Release | 6/6 passed, 11.90 s |
| Clang RelWithDebInfo + ASan + UBSan | 6/6 passed, 49.95 s |

The repeated 10,000-seed checkpoint also passes. These results come from intermediate source and do not establish final clean-HEAD acceptance. The candidate requires the final-source matrix/sweep refresh and final artifact verification; see the matrix for their current status. PACT-83 separately passed 28/28 UE Automation, including four actual PIE lifecycles, and six process-negative cases. PACT-84 Shipping BuildCookRun, ordinary controls and rendered smoke have passed as intermediate package gates.

Windows LeakSanitizer reports itself unsupported, so `ASAN_OPTIONS=halt_on_error=1:detect_leaks=0` is explicit. ASan and UBSan remain enabled, including fuzz/property smoke and UDP integration. An earlier single sanitizer UDP failure remains unclassified in [review evidence](../tasks/20260904-100800-ue5-live-integration-0.2/evidence/PACT-81-review.md); the later full matrix and separate 20-run sanitizer UDP stress passed without widening timeouts or leaving child processes.

## Property and fuzz evidence

The current working-source full property gate passed 10,000 seeds twice with identical deterministic outcomes: `full_sweep_repeated=true`, 522 repeated identity samples, seven edge cases and digest `6852576945316271377`. It reported zero identity mismatches, crashes, deadlocks and unbounded failures. Its scenario set includes 9,600 normal convergence cases, 200 explicit queue-overflow cases and 200 explicit packet-age timeout cases, with frame-count edges 1/32/120/121/255/256/257 and loss buckets 0/1/5/20%. Expected typed failures count as correct only when they match the specified outcome; a crash, deadlock, unbounded failure or identity mismatch is a failure. The fresh JSON records actual counters and the deterministic digest; task command/log evidence binds that result to the tested source and build. The successful intermediate JSON is `artifacts/ue5-0.2/pact84-property-10000-working.json`; its counters do not replace the final clean-source sweep.

The structured fuzz smoke includes 33,334 random packets, 33,333 structured packet mutations and 33,333 structured replay mutations. It also performs 50,000 CRC rewrites so malformed fields can reach validation beyond the integrity check. There is no claim of exhaustive state-space or adversarial-security verification.

## Sample and packaged parity

The preserved 240-frame sample has final hash `0x4B35DC3FD8F6009C`, report identity `0x8150FEDA020B66A8`, 189 rollbacks and 857 resimulated frames. Core tests compare the existing sample replay and trace byte-for-byte. C API parity compares complete report/replay data as well as hashes. UE Editor and the current packaged Shipping smoke have matched that sample; their replay checks pass. The current three-way external verifier passes all 16 checks, including complete report/trace equality and byte-identical replays independently verified by the CLI. Final clean-source verification remains a separate gate.

Final acceptance additionally runs the packaged executable with the same inputs/seeds and verifies CLI/C API/UE reports, replay, nested trace, real screenshots, exact source identity and SDK/Plugin/Demo archives. `VerifyUnrealIntegration.ps1` prints its result at `artifacts/ue5-0.2/parity/<run>/verification.json`. Manifest identity and successful replay strengthen hash agreement; none proves a hash cannot collide or every shared gameplay rule is correct.

## Failure discipline

Every behavior change follows RED -> GREEN -> focused regression -> relevant full regression. A third attempted patch for one failing issue requires a root-cause packet. Tests must not be disabled, retries must be bounded, and timeout changes cannot manufacture success. The [task evidence](../tasks/20260904-100800-ue5-live-integration-0.2/evidence/) preserves precursor/source identities, commands, failures and corrections. Final progress/matrix entries are updated only from fresh command evidence.

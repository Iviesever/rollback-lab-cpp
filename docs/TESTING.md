# Testing

The current main suite has **eight CTest entries**. Its behavior runners contain **61 Core/CLI/UDP cases, 7 C ABI session cases, 4 seeded live-driver cases and 6 UDP driver cases**. A true C11 smoke, structured 100,000-input fuzz smoke, 100-seed property smoke and isolated caught-allocation-failure test complete the entry list below. Installed `find_package` consumers and UE Automation are separate suites. See [UE testing](UE5_TESTING.md) and the [verification matrix](../tasks/20260904-100800-ue5-live-integration-0.2/verification_matrix.md) for final-source acceptance.

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

For the complete five-configuration matrix and full repeated sweep:

```powershell
./tools/bootstrap-llvm.ps1
./tools/run-verification.ps1
New-Item -ItemType Directory -Force artifacts/ue5-0.2 | Out-Null
build\msvc-release\rollback_lab_property_sweep.exe --seeds 10000 --repeat-samples 128 --repeat-full --out artifacts\ue5-0.2\property-sweep-10000.json
```

The bootstrap obtains portable LLVM 22.1.8 without a system install. The matrix covers MSVC Debug/Release, Clang Debug/Release and Clang RelWithDebInfo ASan+UBSan. The script's legacy `-Full` option writes to the 0.1 evidence directory; the explicit command above gives this candidate its own output. CI supplements local engine/artifact verification.

## Suite inventory

| CTest entry | Scope |
| --- | --- |
| `rollback_lab_tests` | 61 cases: 22 Core/session, 15 protocol/transport, 16 report/replay/CLI/viewer/live-scenario, 8 real CLI UDP/process |
| `c_abi_c11_consumer` | Genuine C11 compilation/runtime ABI smoke |
| `c_abi_session_tests` | 7 cases, including 128 delayed-input direct C++/C parity scenarios |
| `c_abi_live_tests` | 4 cases, including 128 complete seeded packet-driven direct C++/C parity scenarios |
| `c_abi_udp_tests` | 6 cases, including real C11 calls and loopback relay traffic |
| `c_abi_udp_exception_test` | Isolated one-shot real `bad_alloc`, caught-step phase/error consistency and terminal non-mutation |
| `protocol_fuzz_smoke` | 100,000 structured/random packet and replay inputs |
| `property_sweep_smoke` | 100 deterministic seeds, with repeated samples |

The original 56 behavior cases remain. UDP scenario iterations are not additional unit-test cases, and the isolated exception executable is not a seventh case in `c_abi_udp_tests`. The exception test uses a test-executable-only global allocation override; production code contains no injected failure hook. Installed C11/C++ `find_package` consumers have their own 2/2 CTest result.

## Coverage

| Area | Representative invariants |
| --- | --- |
| Simulation/session | integer bounds, golden hashes, stable IDs, pre-input snapshots, earliest dirty correction, depth 120/121, tagged rings and independent ownership |
| Protocol/transport | all fields/types and truncations, strict CRC/version/identity rejection, sequence wrap, bounded loss/delay/reorder/queue/age/bandwidth |
| Replay/report | strict versions/order/checkpoints, canonical reconstruction, seeded byte stability and timing-excluded identity |
| C ABI | C11 layout, version/size/reserved fields, output preservation, thread affinity, borrowing, sized buffers and two 128-scenario parity sweeps |
| Incremental UDP | one borrowed session, 64-datagram poll bound, at most one scripted frame per step, monotonic deadline clock, profile/version rejection, timeout, confirmed desync and sticky terminal state |
| Exception containment | an actual allocation throws inside production step work; typed internal failure, failed phase and diagnostic context agree; later calls do not continue simulation |
| Process/package | real distinct PIDs/ports, atomic bound-port readiness, relay handoff, complete ownership teardown, ZIP/SHA/source validation and ordinary Shipping startup |
| UE/presentation | 32 Automation cases, real PIE/input, fixed-step, DLL leases, single/dual-world projection, local correction events, materialized and inspected captures |

## Evidence checkpoints

The clean P0 checkpoint `1c2ed8eaa792e093a7c81b2088e527d3593dbe7c` passed the five-configuration six-entry matrix, repeated 10,000 seeds, SDK consumers, UE Automation, BuildPlugin, Shipping BuildCookRun, ordinary controls, six packaged failure cases and seeded three-way parity. Its immutable receipt is `artifacts/ue5-0.2/p0-clean-1c2e-checkpoint.json`. This completed baseline was required before starting the optional UDP extension.

The UDP SDK checkpoint then passed the seven-entry suite before the isolated exception regression was added:

| Configuration | UDP checkpoint result |
| --- | --- |
| MSVC Debug | 7/7, 60.63 s |
| MSVC Release | 7/7, 23.26 s |
| Clang ASan+UBSan | 7/7, 45.31 s |
| MSVC Release after caught-exception fix/test | 8/8 |

The six UDP cases also passed five consecutive focused runs. The current UE suite passes 32/32; its initial RED had the original 28 pass and the four new cases fail. Seeded packaged three-way parity still passes after UDP integration. Two real relay + two Shipping-client normal runs and all six real UDP negative cases pass. The current extension evidence is working-source evidence; the final eight-entry whole matrix, sweep, SDK/plugin/demo, both integration receipts and audit must be refreshed from one clean reviewed HEAD.

Windows LeakSanitizer is unsupported, so `ASAN_OPTIONS=halt_on_error=1:detect_leaks=0` is explicit. ASan and UBSan remain active. An earlier isolated UDP failure is retained in [PACT-81 review evidence](../tasks/20260904-100800-ue5-live-integration-0.2/evidence/PACT-81-review.md); its cause was not established, while the later matrix and 20-run sanitizer UDP stress passed. The PACT-85 initial sanitizer loader failure was traced to the evidence command omitting the matching Clang runtime directory; the corrected environment passed without widening timeouts. See [PACT-85 Core evidence](../tasks/20260904-100800-ue5-live-integration-0.2/evidence/PACT-85-core.md).

## Property and fuzz

The repeated 10,000-seed checkpoint reports 9,600 convergences, 200 expected queue-overflow failures, 200 expected packet-age timeouts, 522 repeated identity samples, seven edge cases and digest `6852576945316271377`. Both complete sweeps agree; identity mismatches, crashes, deadlocks and unbounded failures are zero. Frame edges are 1/32/120/121/255/256/257 and loss buckets are 0/1/5/20%. Expected typed failures count as correct only when they match their specified outcome. The raw JSON records counters/digest; command/log receipts bind it to the tested source/build. The prior successful sweep does not replace the final post-UDP clean-source sweep.

Fuzz covers 33,334 random packets, 33,333 structured packet mutations and 33,333 structured replay mutations, plus 50,000 CRC rewrites to reach validation beyond the integrity gate. This is bounded robustness evidence, not exhaustive state-space or security verification.

## Two different parity contracts

The seeded 240-frame sample preserves hash `0x4B35DC3FD8F6009C`, report identity `0x8150FEDA020B66A8`, 189 rollbacks and 857 resimulated frames. Core compares the original sample trace/replay bytes; the external CLI/C11/UE verifier compares complete reports, traces and replays. Its receipt is `artifacts/ue5-0.2/parity/<run>/verification.json`.

Real UDP uses the `engine-udp-v1` aggregate profile and actual OS scheduling. The two normal UE runs confirm 240 with the same canonical hash and equal peer replay bytes, verified separately by the CLI. They observed 153 and 141 aggregate rollbacks. Packet counts, per-peer report identities and correction distribution can change; one peer may have zero local prediction/correction. The group requires aggregate prediction/rollback and a real local correction capture where it occurred. Its separate receipt is `artifacts/ue5-0.2/runs/<run>-udp-ue-<case>/summary.json`.

Equal hashes alone do not exclude collisions or shared bugs. Source/profile checks, canonical replay reconstruction, golden samples, negative tests and ownership review add independent constraints. Neither localhost path proves WAN production readiness.

## Failure discipline

Behavior changes follow RED -> GREEN -> focused regression -> relevant full regression. A third attempted patch for one failing issue requires a root-cause packet. Tests remain enabled, retries stay bounded and timeout changes cannot manufacture success. [Task evidence](../tasks/20260904-100800-ue5-live-integration-0.2/evidence/) preserves source identity, commands, failures and fixes. Final progress/matrix entries require fresh command evidence.

# Rollback Netcode 0.1 Walkthrough

## Verified outcome

The project connects two independently owned rollback peers through either a deterministic hostile-network transport or an opaque localhost UDP relay. Local inputs apply immediately, missing remote inputs use last-known prediction, late mismatches restore the earliest dirty pre-frame snapshot, and resimulation converges confirmed canonical state. Replay, confirmed-hash diagnosis, reports, traces, and the browser viewer consume the same production data.

Final source verification SHA: `88134266c2bbb8a68015b148b2bf9ebd24289b80`.

## Real final-HEAD test log

The docs-only delivery head was freshly configured/built with MSVC Release and produced:

```text
Test project D:/program/RollbackLab/build/msvc-release
    Start 1: rollback_lab_tests
1/3 Test #1: rollback_lab_tests ...............   Passed    3.46 sec
    Start 2: protocol_fuzz_smoke
2/3 Test #2: protocol_fuzz_smoke ..............   Passed    0.55 sec
    Start 3: property_sweep_smoke
3/3 Test #3: property_sweep_smoke .............   Passed    0.65 sec

100% tests passed, 0 tests failed out of 3
Total Test time (real) = 4.66 sec
```

The full source matrix on `8813426` passed 3/3 CTest under MSVC Debug, MSVC Release, Clang Debug, Clang Release, and Clang RelWithDebInfo + ASan + UBSan. A CMake clean target removed 117 outputs before a successful Release rebuild and 3/3 rerun.

## Module audit

- `include/rollback_lab/core`, `src/core` — typed errors, modular frames, PCG32, stable hashing.
- `include/rollback_lab/simulation`, `src/simulation` — integer arena, stable-ID projectile processing, canonical serialization.
- `include/rollback_lab/netcode`, `src/netcode` — move-only session, 256 tagged slots, prediction, dirty confirmation gate, 120-frame rollback.
- `include/rollback_lab/protocol`, `src/protocol` — checked LE codec, 32-input/hash windows, strict hello payload, CRC, sequence window.
- `include/rollback_lab/transport`, `src/transport` — seeded logical transport, Winsock/BSD UDP, RAII process lifecycle and POSIX force-reap.
- `include/rollback_lab/replay`, `src/replay` — bounded replay v1 and checkpoint/final verification.
- `include/rollback_lab/report`, `src/report` — canonical JSON, property sweep, peer-local desync, bounded trace, self-contained viewer.
- `include/rollback_lab/udp`, `src/udp` — opaque relay, peer process, supervisor, watchdog, diagnostic artifacts.
- `src/cli` — thin command composition, file I/O, and observational timing only.
- `tests` — 56 behavior cases, structured 100,000-input fuzz, property smoke, real UDP negative/positive paths.

## Verification effects

- Zero transport latency now delivers current inputs before advance: 0 predictions and 0 rollbacks.
- A dirty input cannot advance confirmation or expose its hash until resimulation completes.
- Two complete 10,000-seed sweeps compare equal: 9,600 normal convergence, 200 exact queue overflows, 200 exact timeouts, and no crash/deadlock/mismatch.
- Structured fuzz includes random packet bytes, CRC-rewritten packet mutations, and CRC-rewritten replay mutations under ASan+UBSan.
- Real UDP stress passes 20/20 with no residual process. Controlled damage divergence writes a peer-local diagnostic at boundary 91 and returns typed desync.
- Final sample replay reconstructs frame 240 hash `0x4B35DC3FD8F6009C`.
- Browser QA at 1440×900 and 390×844 verifies scrub/step/play, 189 rollback markers, 1,340 packet events, no overflow, and zero console errors.
- Independent read-only review progressed from No to With fixes to Yes; the final review found no Critical or Important issue.

## Delivery state

Public repository: `https://github.com/Iviesever/rollback-lab-cpp`  
Draft PR: `https://github.com/Iviesever/rollback-lab-cpp/pull/1`  
The branch is intentionally unmerged. No tag or GitHub Release exists.


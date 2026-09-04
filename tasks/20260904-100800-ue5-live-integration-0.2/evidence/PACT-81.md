# PACT-81 SDK, ABI and incremental integration evidence

Pre-commit HEAD: `138c9a4e02b8ad38ee0446dffedf543647045728`. Production changes
are the scoped PACT-81 diff; binaries embed this precursor SHA. Clean SDK artifacts
must be regenerated after the commit. Raw evidence stays under
`artifacts/ue5-0.2/pact81` and `artifacts/sdk/c-api-agent`.

## Delivered boundary

- SDK 0.2.0 Candidate, stable C ABI v1, 19 undecorated `rl_` exports, genuine C11
  header and consumer. Existing C++ core target and CLI retained.
- Fixed-width version/size records, explicit layout checks, owned opaque handles,
  same-thread checks, borrowed-session mutation/destruction guards, typed errors,
  exception containment and two-stage copied outputs.
- `LiveScenario` extracts the existing seeded runner's phase sequence; the CLI
  loops it and the C ABI steps it. No duplicate simulation/rollback/transport.
- Real pre/post correction snapshots; scripted and local-A override input;
  completed reports, trace and Replay v1 via C-owned buffers.
- CMake install/export/package config; ZIP, version/source/CRT manifest and
  SHA-256; independent C11 and C++ find_package consumers; integrity negatives.
- Full-width CLI seed options and a real C11 artifact-producing demo. Existing
  relative-output UDP defect fixed by resolving supervisor paths before spawning.

## RED/GREEN

- `live-red.txt`: missing incremental driver header.
- `live-api-red.txt`: missing live ABI source after toolchain validation.
- C11 RED in `artifacts/sdk/c-api-agent/verification.txt`: missing C ABI header.
- `core-red.txt`: new relative-output UDP test fails with original production code.
- `seeds-red.txt`: explicit uint64 seeds were ignored by original CLI.
- `review-red.txt`: all three independent review findings reproduced; see
  PACT-81-review.md for before/after details.
- `review-green.txt`: 7 session + 4 live + 61 core cases pass; 6/6 CTest.

## Final changed-source matrix

Command: `tools/run-verification.ps1` with process TEMP/TMP under repository
artifacts, portable LLVM22.1.8 and explicit ASAN/UBSAN halt-on-error settings.
Log: `artifacts/ue5-0.2/pact81/final-matrix.txt`.

| Configuration | Fresh result |
| --- | --- |
| MSVC Debug | 6/6 CTest, 119.96s |
| MSVC Release | 6/6 CTest, 17.89s |
| Clang Debug | 6/6 CTest, 130.65s |
| Clang Release | 6/6 CTest, 14.93s |
| Clang RelWithDebInfo ASan+UBSan | 6/6 CTest, 84.00s |

Every configuration includes C11 smoke, 7 C API session cases (including 128
delayed-input parity scenarios), 4 live cases (including 128 packet-driven parity
scenarios), 61 core/CLI/UDP behavior cases, structured 100000-input fuzz and
100-seed property smoke. The original 56 behavior cases remain. Windows LSan
remains unsupported and is explicitly disabled; ASan and UBSan are enabled.
The final 10000-seed full repeat remains a PACT-84 gate.

## Identity

The checked sample Replay v1 and Trace JSON compare byte-for-byte in current
tests; report identity stays `0x8150FEDA020B66A8`. Default CLI/C run at boundary240
has hash `0x4B35DC3FD8F6009C`, 189 corrections and 857 resimulated frames. Replay
SHA-256 is `8C86C75B4EF84C1B02EFA9B31FA300D246B55D50604848D53DFDB76E80D13051`.

An explicit scenario seed `18446744073709551615` and transport seed `4294967301`
also match CLI/C full reports, hash `0xF3E3377A25BB2D24`, 200 corrections and
byte-equal verified replay. These file-level runs are in `parity/summary.json`;
the full matrix independently repeats normal and negative ABI behavior after
the review fixes.

## SDK and audit

Intermediate BuildSdk/VerifySdk completed all payload/ZIP SHA checks, 2/2 installed
standalone consumers and 9 manifest checks. The DLL has exactly 19 stable exports
and Release MSVC/UCRT dependencies, no debug CRT. This intermediate package is
labelled dirty and must not be used as final-HEAD evidence.

Fresh read-only review found 1 High and 2 Medium; all were reproduced and fixed.
Narrow recheck reports no remaining Blocker/High/Medium. The earlier isolated
sanitizer UDP failure remains recorded in PACT-81-review.md; the final changed-source
sanitizer matrix passes, and the separate 20-run sanitizer UDP stress passed
without timeout changes or remaining child processes. Root cause of that earlier
single failure remains unclassified.

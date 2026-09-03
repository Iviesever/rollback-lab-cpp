# Verification Matrix

| ID | Requirement | Proof command/artifact | Status |
|---|---|---|---|
| V-001 | Main baseline and feature branch lineage | `git merge-base`, `git rev-list`, remote refs | Passed at `b1671c9f162a92512aea23040a309d4f27003912` |
| V-010 | Pure integer deterministic simulation and golden trace | CTest simulation suite on MSVC and Clang/GCC | Passed at `8813426` on MSVC/Clang; golden hash equal |
| V-020 | Prediction, earliest rollback, rings, limits, metrics | CTest rollback suite | Passed at `8813426`, including pre-flush confirmation gate |
| V-030 | Seeded transport features and repeated identity | CTest transport suite and canonical report checksums | Passed at `8813426`, including enqueue-order overflow policy |
| V-031 | Strict protocol codec and sequence behavior | CTest protocol suite | Passed at `8813426`: all types, 32 input/hash windows, hello contract |
| V-032 | Truncation, corruption, random bytes, fuzz | protocol exhaustive test and fuzz-smoke log | Passed under MSVC Release and Clang ASan+UBSan at `8813426` |
| V-040 | Replay rebuild and rejection | replay tests plus sample replay CLI | Passed at `8813426`; checked-in sample replay verifies frame 240 |
| V-041 | Confirmed desync diagnosis and no speculative false positive | desync tests and diagnostic JSON | Passed at `8813426`: packet-driven + real UDP peer-local diagnostic |
| V-042 | Canonical JSON/trace stable schema and bounds | report tests and schema inspection | Passed at `8813426`: PCG/policy identity and bounded omitted counters |
| V-050 | Real relay plus two independent peer processes | UDP integration suite and `udp-demo` report | Passed at `8813426`; 20/20 Release stress |
| V-051 | Timeout, mismatch, port conflict, child reap | negative UDP integration tests | Passed at `8813426`; 0 residual children, POSIX fallback reviewed |
| V-060 | Real-trace self-contained viewer | browser console/interaction/responsive QA and screenshot | Passed from `8813426`: desktop/narrow console 0 errors |
| V-061 | Sample report/replay/trace/viewer | checksummed files under `samples/` and `viewer/` | Regenerated from `8813426`; checksums recorded in evidence |
| V-070 | 10,000 bounded seeds | property-sweep report with exact success/failure counts | Passed twice at `8813426`: 9,600 converge + 400 exact failures |
| V-071 | MSVC Debug and Release | configure/build/CTest logs | Passed at `8813426`: 3/3 each |
| V-072 | Clang or GCC | configure/build/CTest logs | Clang 22.1.8 Debug/Release passed at `8813426`: 3/3 each |
| V-073 | ASan and UBSan where supported | configure/build/CTest logs or explicit platform limitation | Clang ASan+UBSan passed 3/3 at `8813426`; Windows LSan unsupported |
| V-074 | Clean rebuild and clean worktree | rebuild log and `git status --short` | CMake clean + 117-output rebuild passed; pre-delivery worktree clean |
| V-075 | Benchmark schema and observations | simulation and rollback benchmark JSON | Passed at `8813426`; 100,000 ticks + 2,000-frame stress |
| V-080 | Documentation matches implementation | documentation audit checklist | 14 required docs, JSON/replay/schema/links/placeholders all audited |
| V-081 | Public remote, pushed branch, Draft PR body | GitHub URL, refs, PR number | Public remote, branch pushed, Draft PR #1; push/PR CI runs green |

No row may be marked Passed without fresh evidence from the exact final or explicitly recorded precursor HEAD. A precursor result is rerun if later changes touch its dependency cone.

# Verification Matrix

| ID | Requirement | Proof command/artifact | Status |
|---|---|---|---|
| V-001 | Main baseline and feature branch lineage | `git merge-base`, `git rev-list`, remote refs | Passed at `b1671c9f162a92512aea23040a309d4f27003912` |
| V-010 | Pure integer deterministic simulation and golden trace | CTest simulation suite on MSVC and Clang/GCC | MSVC Passed at `d173fdc`; cross-compiler pending PACT-70 |
| V-020 | Prediction, earliest rollback, rings, limits, metrics | CTest rollback suite | Passed at `79cd6d1`; final-head rerun pending |
| V-030 | Seeded transport features and repeated identity | CTest transport suite and canonical report checksums | Transport behavior passed at `3629bd1`; report checksum pending PACT-40 |
| V-031 | Strict protocol codec and sequence behavior | CTest protocol suite | Passed at `3629bd1`; final-head rerun pending |
| V-032 | Truncation, corruption, random bytes, fuzz | protocol exhaustive test and fuzz-smoke log | 100,000-input smoke passed at `3629bd1`; sanitizer rerun pending |
| V-040 | Replay rebuild and rejection | replay tests plus sample replay CLI | Pending |
| V-041 | Confirmed desync diagnosis and no speculative false positive | desync tests and diagnostic JSON | Pending |
| V-042 | Canonical JSON/trace stable schema and bounds | report tests and schema inspection | Pending |
| V-050 | Real relay plus two independent peer processes | UDP integration suite and `udp-demo` report | Pending |
| V-051 | Timeout, mismatch, port conflict, child reap | negative UDP integration tests | Pending |
| V-060 | Real-trace self-contained viewer | browser console/interaction/responsive QA and screenshot | Pending |
| V-061 | Sample report/replay/trace/viewer | checksummed files under `samples/` and `viewer/` | Pending |
| V-070 | 10,000 bounded seeds | property-sweep report with exact success/failure counts | Pending |
| V-071 | MSVC Debug and Release | configure/build/CTest logs | Pending |
| V-072 | Clang or GCC | configure/build/CTest logs | Pending |
| V-073 | ASan and UBSan where supported | configure/build/CTest logs or explicit platform limitation | Pending |
| V-074 | Clean rebuild and clean worktree | rebuild log and `git status --short` | Pending |
| V-075 | Benchmark schema and observations | simulation and rollback benchmark JSON | Pending |
| V-080 | Documentation matches implementation | documentation audit checklist | Pending |
| V-081 | Public remote, pushed branch, Draft PR body | GitHub URL, refs, PR number | Pending |

No row may be marked Passed without fresh evidence from the exact final or explicitly recorded precursor HEAD. A precursor result is rerun if later changes touch its dependency cone.

# 0.2 verification matrix

This table records completed validation checkpoints. It does not relabel an older
artifact as final: deliverable SDK, Plugin, Demo and smoke records must agree on
one clean source SHA. The final integration JSON records that SHA and hashes all
three ZIPs, reports, replay files and screenshots. Raw outputs remain under the
ignored `artifacts/ue5-0.2` directory; the scoped evidence documents are committed.

| Gate | Verified checkpoint | Result |
| --- | --- | --- |
| Fresh base / branch / GitHub | fetched `5b250ebc985f8e098e7d613e9cab7b0897482cc9`; PR1 merged; feature branch | Passed; fetch repeated after PACT84 |
| Original baseline / CLI / replay / desync / UDP / viewer | [PACT80](evidence/PACT-80/baseline.md), preserved sample identities | Passed |
| C11 / C++ consumers, ABI negatives and independent handles | [PACT81](evidence/PACT-81.md); two installed consumers | Passed, including clean `3dce71e5` SDK |
| Direct C++ / C ABI parity | 128 session and 128 live packet scenarios | Passed |
| SDK install/export/manifest/ZIP/SHA | BuildSdk + VerifySdk; separate C11/C++ find_package builds | Passed; final source identity is in the SDK manifest |
| Core MSVC Debug / Release | clean `3dce71e5` configure/build/CTest | 6/6 in each configuration |
| Core Clang Debug / Release | clean `3dce71e5` configure/build/CTest | 6/6 in each configuration |
| ASan + UBSan / structured fuzz | clean `3dce71e5` sanitizer CTest; 100000 structured inputs | Passed |
| Full 10000 seeds, whole sweep repeated | clean-3dce-property-10000.json | 9600 convergences +400 expected bounded failures; zero identity mismatches/crashes/deadlocks/unbounded failures |
| UE Automation / lifecycle / fixed-step / isolation / integrity | P0 clean checkpoint plus UDP run152940 | 32/32, including four added UDP cases; final rebuild refresh required |
| Editor build / launch / rendering | PACT83 real RHI and controls; clean `3dce71e5` Editor build135329 | Passed |
| BuildPlugin Editor Development / Game Development / Game Shipping | run134114 passed; global-mutex contention135501 retained; WaitMutex fix135911 passed all3 | Passed after bounded serialization fix; [investigation](evidence/PACT-84-build-mutex.md) |
| Win64 BuildCookRun | Development and Shipping; clean `3dce71e5` Shipping run135414 | Passed; clean Shipping cook/build29.04s |
| Ordinary packaged controls | Shipping run133759, actual pause/move/fire/step/release | Passed; both processes exited |
| Packaged smoke / actual rollback / confirmed convergence | Shipping run133714 | Passed: confirmed240/240, hash0x4B35DC3FD8F6009C, rollback189 |
| CLI / C11 / packaged UE | parity/20260904-134325-034-98663f04/verification.json | 16/16 checks: complete report/trace equality and byte-identical replays; explicitly intermediate source |
| JSON / replay / screenshot inspection | all three Shipping PNGs opened by parent and independent reviewer | Passed; fresh final captures remain mandatory |
| Failure process codes / watchdog | six Editor and six actual Shipping cases | Every case returned1, false failure JSON, no retained child |
| Manifest / archive verifier negatives | evidence-tests-20260904-133829-158-2963cacc/results.json | 44/44, including missing/multiple/corrupt SDK ZIP |
| Log audit | [PACT84](evidence/PACT-84.md) | Project input/compression warnings fixed; stock GPU/preload and metadata-mode diagnostics classified, retained |
| Documentation / AI attribution | README and the required design/testing/teaching documents, real Shipping preview | 15 required interview answers; 14 UE/C ABI drills; authorship explicit |
| Fresh independent full review | base...`3dce71e5`; later narrow WaitMutex recheck | No confirmed Blocker/High/directly related low-risk Medium; [audit](evidence/PACT-86-audit.md) |
| One clean source for final SDK/Plugin/Demo/receipts | automatic source, complete tree, ZIP and process checks | Refresh after the serialization/verification-record commit; never reuse working artifacts as final |
| Clean pushed feature / Draft PR | final Git/GitHub acceptance | Final delivery step; no merge/tag/release authorized |
| Two-client UDP-UE | [PACT85](evidence/PACT-85-ue.md): Normal155457/160929, six actual negative cases | Functional closure passed; same-source final rebuild still required |

Latest PACT85 SDK adds seven operations (26 exports total) without changing ABI1,
Protocol1 or Replay1. Current full Core suite has8 CTest entries:61 Core/CLI/UDP
behavior cases,7 C-session,4 C-live,6 C-UDP cases, genuine C11 smoke, isolated
exception regression, structured fuzz and property smoke. Integrated Release
passed8/8 after the exception fix; final all-configuration rebuild follows.
The supervisor helper suite passed38/38 and the actual5-second UE Watchdog now
preserves a fail-closed partial summary while reaping all owned processes.

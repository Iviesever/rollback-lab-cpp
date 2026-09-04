# 0.2 verification matrix

| Gate | Proof | Status |
| --- | --- | --- |
| Fresh base / branch / GitHub | fetched base 5b250ebc985f8e098e7d613e9cab7b0897482cc9; PR 1 merged; CI success; no tags/releases/issues | Passed |
| Baseline Debug / Release / fuzz / property / CLI / replay / desync / UDP / viewer | PACT-80 baseline.md and ignored raw logs | Passed; relative UDP output defect recorded |
| DLL + UE standalone spike | PACT-80 BuildPlugin outputs, 3 configs and equal DLL SHA | Passed; user permits only default UBT trace/backups outside repository |
| C11 / C++ consumers, ABI negatives / isolation | CTest | Pending |
| Direct C++ / C ABI 100+ scenarios | parity test | Pending |
| SDK install/export/find_package/ZIP/manifest/SHA | BuildSdk + VerifySdk | Pending |
| Core MSVC Debug / Release | fresh configure/build/CTest | Pending |
| Core Clang Debug / Release | fresh configure/build/CTest | Pending |
| ASan + UBSan / structured fuzz | sanitizer CTest | Pending |
| 10,000 seeds twice | full property sweep JSON | Pending |
| UE Automation / lifecycle / fixed-step / isolation / integrity | TestUnrealIntegration | Pending |
| Editor build / load | BuildUnrealDemo + launch log | Pending |
| BuildPlugin Editor / Game Development / Shipping | BuildUnrealPlugin | Pending |
| BuildCookRun Win64 | PackageUnrealDemo | Pending |
| Ordinary packaged launch | bounded process and screenshot | Pending |
| Packaged smoke / rollback / confirmation / 3-way parity | VerifyUnrealIntegration | Pending |
| JSON reparse / replay / 3 screenshots inspected | trace + images | Pending |
| Stage / manifests / SHA / exact clean HEAD | artifact audit | Pending |
| Logs Error / Warning audit | explicit classified log findings | Pending |
| Documentation / AI authorship / independent review | PACT-86 audit | Pending |
| Clean pushed feature / Draft PR | Git and GitHub fresh checks | Pending |
| Optional UDP-UE | eligibility gate and evidence or deferral | Not started |

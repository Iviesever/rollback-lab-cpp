# Debug allocation probe root cause

The final2888db76 matrix passed7/8 MSVC Debug entries; the isolated allocation
regression exited3 before printing a caught exception. Release and ASan versions
had passed, so their success was not treated as proof for Debug.

Minimal reproduction: run build/msvc-debug/rollback_lab_udp_exception_tests.exe.
The initial trace printed `Injected allocation size=16` and then terminated.
MSVC Debug creates iterator proxies for default container construction; failure
inside that noexcept construction terminates instead of propagating bad_alloc
to the Core step. This was a test injection location problem, not evidence of
an exception escaping the C ABI.

An initial attempt to select sizeof(InputFrame) still selected16 bytes: that
record is also16 bytes in this build and did not exclude the iterator proxy.
The retained logs show the same termination. No third blind patch was made:
the input record layout and packet codec were inspected before choosing a
materially different allocation point.

The probe now targets production buffer growth of at least32 bytes.
This excludes the small proxy/default-construction allocations and still throws
a real one-shot bad_alloc during Core buffer growth (canonical-state or packet
serialization; the size predicate alone does not identify which). All original
status, FAILED phase, native context/error code, unchanged retry frame/hash/tick
and byte-identical retry diagnostic assertions remain. Debug iterator checking,
compiler flags and production code are unchanged; no production fault hook was
added. The test reports the injected payload allocation size.

Fresh focused MSVC Debug, Release, Clang Debug and ASan+UBSan pass. Focused
Clang results are recorded in exception-final-clang-test.txt and
exception-final-asan-test.txt under artifacts/ue5-0.2 before the final test commit.
The independent read-only recheck confirms the original assertions are intact
and this test-only Medium is closed. The complete final matrix must run again
from the resulting clean source.

Independent full audit of base...2888db76 found no additional production
Blocker/High/Medium. It identified this same test issue and requires the narrow
delta/RED-GREEN recheck. Final artifact receipts must use the later source SHA,
not relabel the successfully built2888 SDK/Plugin/Demo.

# PACT-81 independent review

Scope: current SDK/C ABI/incremental driver/CLI/UDP changes from origin/main,
including untracked new files. Fresh read-only context, no inherited implementation
conversation, no file edits, builds or tests by the reviewer. Parent retained
the source and ran reproductions.

## Findings and resolution

| Severity | Finding | Observed RED | Minimal resolution |
| --- | --- | --- | --- |
| High | Two damage-bias peers could agree while replay rebuilt canonical state, yet report success | Symmetric biased-session case reached matching peer hashes different from canonical replay and incorrectly returned OK | Final success requires peer hash equal canonical replay hash; explicit RL_REPLAY_MISMATCH=16; error persists, trace remains queryable |
| Medium | Accepted future frame current+255 could overwrite last known remote input in 256-slot ring | At boundary1, remote frame256 accepted instead of INVALID_FRAME | Public C session rejects all future frames before mutation; caller queues them until current; last-known prediction regression preserved |
| Medium | Present-but-empty seed argument silently used default identity | Missing scenario seed returned0 instead of2 | Distinguish absent option from missing/empty value; retain full uint64 parsing and default absent behavior |

Raw reproduction: `artifacts/ue5-0.2/pact81/review-red.txt` has all three exact
failures. `review-green.txt` then passes 7 session, 4 live and 61 core cases,
6/6 CTest, 18.05 seconds. Narrow read-only recheck confirms all three fixes and
documentation, with no remaining Blocker/High/Medium in this scope. Final
five-configuration regression is separately recorded; this is not the complete
UE end-of-goal audit.

## Sanitizer UDP investigation

The initial five-configuration run had one failure in the existing positive UDP
case under ASan+UBSan, without an ASan/UBSan diagnostic. Its assertion omitted the
typed underlying error, and its output directory was empty. The same independent
90-frame sanitizer UDP command passed immediately. The positive test was enhanced
to report error code/detail/context on any recurrence; no production timeout,
sleep, retry, socket or process semantics were changed for this anomaly.

The full identical-source sanitizer rerun passed 6/6 (80.82 seconds), followed by
20/20 independent sanitizer UDP runs with zero exits and no remaining children.
The original cause remains unclassified; these passes do not establish a root
cause. Preserve `full-matrix.txt`, `asan-rerun.txt`, `asan-udp-repro.txt` and
`asan-udp-stress.json` under ignored artifacts. The final matrix must run again
after review fixes and cannot cite this precursor as current-source coverage.

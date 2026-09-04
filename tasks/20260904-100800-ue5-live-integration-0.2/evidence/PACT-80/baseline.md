# Fresh 0.1 baseline and DLL feasibility

Source: `5b250ebc985f8e098e7d613e9cab7b0897482cc9` (fresh origin/main).
Branch: `feat/ue5-live-integration-0.2`.

GitHub PR #1 is MERGED (2026-09-04 01:23:28 UTC), merge commit equals base. CI run 33825581956 succeeded. No tags, GitHub releases, or open issues were returned by fresh commands. Initial worktree was clean. No .agents folder exists in the repository or workspace ancestor paths.

| Command | Exit / observation |
| --- | --- |
| `cmake --workflow --preset msvc-debug-ci` | 0; 3/3 CTest; 17.76 s |
| `cmake --workflow --preset msvc-release-ci` | 0; 3/3 CTest; 5.94 s |
| `ctest --preset msvc-debug -V` with repository TEMP/TMP | 0; 56 behavior cases; 3/3 CTest; 21.01 s |
| `ctest --preset msvc-release -V` with repository TEMP/TMP | 0; 56 behavior cases; 3/3 CTest; 6.73 s |
| structured fuzz within each CTest | 100000 inputs: 33334 random packets, 33333 structured packets, 33333 structured replays, 50000 CRC rewrites; 0 crashes |
| property smoke within each CTest | 100 seeds; 96 convergence + 4 exact declared failures; identity mismatches/crashes/deadlocks/unbounded failures 0 |
| `rollback_lab simulate --scenario default --frames 240 --out <baseline>/simulate` | 0; hashes A/B `0x4B35DC3FD8F6009C`; report/replay/trace/viewer generated |
| `rollback_lab replay <baseline>/simulate/input.rlr` | 0; frame 240, decimal hash 5419479893391311004 |
| `rollback_lab desync-demo --out <baseline>/desync.json` | 0; real controlled diagnostic written |
| `rollback_lab udp-demo --frames 120 --out <absolute-baseline>/udp-absolute` | 0; relay and separate peers; confirmed 120, hash 9581097322576948979 |
| `rollback_lab verify --frames 600` | 0 |

Baseline replay and checked sample SHA-256 both `8C86C75B4EF84C1B02EFA9B31FA300D246B55D50604848D53DFDB76E80D13051`.

## Findings preserved for regression

- `udp-demo --out <relative-path>` fails relay_start with exit 6 because Windows ChildProcess selects executable.parent_path() as child cwd while supervisor passes a relative ready path. An absolute output passes. Add a focused relative-output regression and normalize once before spawning during PACT-81.
- Directly launching the behavior runner from repository root failed three UDP cases because that test helper intentionally expects the CLI in current_path(). Standard CTest sets the build directory and passes. This was an invocation error, not a core regression; both logs are retained.
- Existing tests use temp_directory_path(). Initial discovery inherited system temporary storage; fresh confirmation redirects TEMP/TMP under artifacts, and every future tool/test must inherit repository-local temporary paths.
- Engine always uses UBA in UE 5.8. Initial BuildPlugin wrote its default trace outside repository despite -NoUBA; redirection remains an explicit integration issue. No Engine file was modified.

Raw logs/artifacts are ignored under `artifacts/ue5-0.2/baseline` and `artifacts/ue5-0.2/spike`; portable evidence is this document.

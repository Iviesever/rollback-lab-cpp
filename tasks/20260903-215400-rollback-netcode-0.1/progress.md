# Progress

## 2026-09-03 22:39 UTC+8

- Current PACT: PACT-20 complete; entering PACT-30
- Exact HEAD: `79cd6d14f5eaef7aed7501fe6b19a9319368706f` on `feat/rollback-netcode-0.1`
- Changed contracts: added 256-slot tagged histories/snapshots, 120-frame fail-closed limit, last-known prediction, batched earliest-dirty correction, exact metrics, confirmation boundary, and move-only sessions
- Commands run: observed missing-netcode-header RED; built/reran MSVC Debug; diagnosed Windows `0xC00000FD` with systematic-debugging; moved fixed storage behind one RAII allocation; ran CTest and internal runner
- Tests passed/failed: 19/19 internal tests passed, 1/1 CTest passed; zero latency has zero rollback; matching prediction no rollback; mismatches coalesce at frame 5/depth 7; two independent sessions converge at frame 180
- Known blocker: no system Clang/GCC and no WSL distribution; MSVC 14.51 is installed. Plan is a project-local, version-locked portable LLVM toolchain for Clang and sanitizer validation if needed.
- Quota/feature-freeze state: no token budget reported; before 2026-09-05 feature freeze; all P0 remains active
- Next action: commit PACT-20 evidence, then write strict binary protocol and seeded transport RED tests

# Repository Instructions

## Scope

- Work only inside this repository.
- Treat `tasks/20260903-215400-rollback-netcode-0.1/product_contract.md` as the acceptance contract.
- Keep canonical simulation free of wall-clock, OS, thread scheduling, global randomness, and floating-point state.
- Peers may communicate only through versioned packets; never copy one peer's internal state into another.

## Workflow

- Use RED -> GREEN -> focused regression -> relevant full regression for every behavior change.
- Update `verification_matrix.md` only from fresh command evidence.
- Update `progress.md` at every PACT checkpoint with exact HEAD, commands, results, blockers, and next action.
- A third attempted patch for the same failing issue requires a root-cause packet under `evidence/`.
- Keep commits scoped to one PACT and leave the worktree clean before handoff.

## Safety

- Do not merge pull requests, delete remote branches, create tags, publish releases, or persist credentials.
- Do not modify SeedForge, MQB, or any repository outside this directory.
- Do not use unbounded queues, detached threads, infinite retries, or long sleeps to hide concurrency failures.


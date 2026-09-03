# Benchmarking

Run both benchmark workloads with:

```powershell
build\msvc-release\rollback_lab.exe benchmark --frames 100000
```

The command emits one JSON object:

- `simulation.total_ticks`, `total_microseconds`, and integer `ticks_per_second` for direct canonical simulation;
- `rollback_stress.total_ticks`, `total_microseconds`, `rollback_count`, `resimulated_frames`, and `maximum_depth` for an 8-tick base-latency / 4-tick jitter session run.

Wall clock is read only in the CLI benchmark wrapper. It never enters canonical state, replay, hashes, or report identity. Durations are local observations; CI checks that the command runs and that structural invariants hold, not an absolute speed threshold.

The session allocates its fixed history/snapshot storage once during construction. Current canonical serialization returns a vector and therefore may allocate while hashing; a trustworthy hot-path allocation counter is not included in v0.1. This is reported as a limitation rather than guessed as zero.

When comparing changes, use the same executable, build type, compiler, machine power state, frame count, and seeds. Report medians across several runs if performance—not mere smoke validity—is under review.


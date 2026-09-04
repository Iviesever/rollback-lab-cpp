# Shared UBT mutex contention

## Observable failure

The clean `3dce71e5e2c7442addd1d19fe34265b5d52e2c5f` BuildPlugin run
`artifacts/ue5-0.2/runs/20260904-135501-122-plugin` built Editor Development
successfully, then its Game Development UBT process returned10 with
`ConflictingInstance`. UAT labels that numeric result `Error_SDKNotFound`, but
the underlying diagnostic explicitly reports an already-owned global UBT mutex.
The supervisor recorded all51 descendants exited.

## Facts and ownership

Another user task was independently using the same installed UE toolchain.
RollbackLab's repository lock serializes its own pipelines, and its Editor
preflight refuses an already-running Editor. Neither can prevent another task
from starting between BuildPlugin's three sequential UBT invocations.
No process belonging to another project was stopped or modified.

The SDK/toolchain was not missing: the same run completed the first target,
and earlier three-target builds passed. This is not a simulation, compiler,
DLL-lifecycle or memory-safety failure.

## Minimal correction

Use stock UBT's `-WaitMutex` for every builder invocation, including inherited
BuildPlugin invocations and BuildCookRun's explicit UBT arguments. The existing
direct Editor build already used that option; it now receives it from the shared
argument list instead of duplicating it. UE's own single-instance mechanism
serializes acquisition. Existing owned-job outer timeouts still bound a wait;
no timeout or sleep was increased, and no Engine file was changed.

The retained failure is the RED evidence. The follow-up real three-target build
and read-only audit recheck supply the GREEN evidence before committing this fix.

GREEN: run135911 completed all three BuildPlugin targets, and the independent read-only recheck found no qualifying issue. The outer watchdogs are unchanged.

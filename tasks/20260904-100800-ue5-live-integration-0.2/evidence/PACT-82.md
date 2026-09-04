# PACT-82 Runtime Bridge evidence

Pre-commit SDK/source HEAD: `c4b37358eb2506fd6159ffc476ac4ef5c89c4db0`.
UE sources and scripts are this scoped working change; current engine binaries
are intermediate evidence until the final clean-head rebuild.

## Delivered

- UE5.8 Runtime plugin and minimal native project, C ABI-only SDK dependency.
- Manifest/source/version/CRT/architecture/export/DLL SHA validation; automatic
  staging; explicit owned Win64 DLL references and RAII driver-before-peers cleanup.
- Game-instance subsystem, module pre-exit/shutdown hooks, real copied views and
  correction revisions; bounded 60Hz external clock, pause, step and reset.
- Shared PowerShell build/test orchestration with repository paths, exclusive tool
  lock and a suspended-root Win64 job supervisor with direct log handles.
- Safe dedicated plugin output, no reparse traversal, verified same-root staging
  no-op and partial-overlap rejection.

## RED and investigation

The initial Runtime scaffold compiled and ran 16 actual UE Automation cases.
All 16 failed from the explicit NotImplemented boundary; index.json had zero
skipped/not-run cases. Evidence: `runs/20260904-112259-771-automation` under
`artifacts/ue5-0.2`.

The first implementation exposed the shared-DLL lifetime crash recorded in
PACT-82-root-cause-dll-lease.md. Owned LoadLibraryEx references fixed it; two-runtime
and external-preload regressions remain in the suite.

The first actual PIE-cycle test successfully started a PIE world, then crashed
because its teardown called CancelRequestPlaySession before EndPlayMap. In stock
UE5.8, Cancel clears the active optional session metadata still needed by Slate's
window teardown. The test now requests normal EndPlayMap for active sessions and
only cancels queued, not-active requests. The same three cycles and deadlines
remain; Engine source and Runtime production behavior were not changed.

## GREEN

| Gate | Fresh result / raw evidence |
| --- | --- |
| Editor Development | Built via BuildUnrealDemo; latest source used for PIE fix run |
| UE Automation | 18/18 succeeded, 0 warnings/failed/notRun/inProcess |
| Actual PIE | Three real world start/SDK step/EndPIE cycles; handles/libraries returned to baseline every cycle |
| Automation process | root exit0, watchdog false, all 9 job processes exited |
| Standalone BuildPlugin | Editor Development + Game Development + Game Shipping all succeeded; UAT exit0, 62s |
| Core MSVC Debug | Configure/build + 6/6 CTest, 59.93s |
| Core MSVC Release | Configure/build + 6/6 CTest, 16.12s |
| Native supervisor | Five cases: quoting/direct logs, exit7, descendant cleanup, watchdog1460, failed-start handles stable |
| Path guards | Original seven failures reproduced; final zero failures, same-root DLL SHA preserved |

Authoritative UE report/log/process record:
`artifacts/ue5-0.2/runs/20260904-121024-926-automation/`.
The Editor log has zero Warning/Error lines, and explicitly records three world
start/teardown cycles. BuildPlugin:
`artifacts/ue5-0.2/runs/20260904-121156-248-plugin/`.
Core: `artifacts/ue5-0.2/pact82-core-regression.txt`.
Supervisor: `artifacts/process-runner-tests/{results.json,evidence.txt}`.
Path guards: `artifacts/ue5-0.2/{pact82-path-red.txt,pact82-path-green.txt,path-tests.json}`.

## Log audit and environment

- Missing Content directory and inconsistent ZenStore/Pak settings were corrected
  in project configuration, not suppressed.
- Stock UE UnifiedError smoke tests assert English messages; process-local
  `-culture=en` resolves their failures under the OS Chinese locale. Tests remain enabled.
- Direct UBT build arguments no longer leak into WriteMetadata. Stock BuildPlugin
  lacks a UBT-argument pass-through, so its supported UBT_EXTRA_ARGS environment
  reaches the metadata child, which lists those builder-only options as unused
  `Invalid arguments` while successfully writing metadata. The parent build applies
  them and succeeds. This known stock-tool diagnostic is retained in raw output.
- UBT's default global Trace.uba/backups are the sole user-approved external-output
  exception; UBA storage, DDC, shader work, process/crash logs and reports are local.
- Other projects' UE processes were observed and respected; cleanup is job-owned,
  never a process-name termination.

## Review

Fresh read-only review found two Medium script path hazards. Both were reproduced
without invoking destructive UAT paths and fixed. Narrow recheck reports no
remaining findings. This checkpoint does not claim the Arena, screenshots,
BuildCookRun or final packaged parity gates; those are the next PACTs.

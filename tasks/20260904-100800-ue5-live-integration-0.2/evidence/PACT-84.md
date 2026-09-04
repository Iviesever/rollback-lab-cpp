# PACT-84 packaged integration evidence

Intermediate source/SDK checkpoint: `40f98b8eaed19b79f09743e128a1ebd706d27f04`.
Working-source packages below are explicitly labelled `-working` and are not the
final clean-commit artifacts. Final gates will regenerate all artifacts together.

## Acceptance and tools

The package scripts reuse the Editor build and native content generator, then
run stock BuildCookRun with repository-local cook/stage/archive/log/cache paths.
Dedicated demo path guards reject source directories, traversal and junctions.
The actual absent API was RED before the guard implementation; path tests passed.

Artifact and smoke verifier tests were RED before implementation. The final
44-case focused suite checks file trees, manifest identity, archive bytes,
missing/corrupt/duplicate SDK ZIPs, JSON structure, hashes, rollback and replay
claims, missing PNGs and failed/timed-out process records. Evidence:
`artifacts/ue5-0.2/evidence-tests-20260904-133829-158-2963cacc/results.json`.
These fixtures are verifier tests, not substitute packaged evidence.

## Actual package investigations

- First Development BuildCookRun: `runs/20260904-131618-464-package-demo`,
  363.98 seconds, root exit0, all87 descendants exited, no Error/Warning lines.
- Its actual packaged Smoke: `runs/20260904-132346-271-packaged-arena`,
  exit0, all2 bootstrap/game processes exited, confirmed240/240,
  hash `0x4B35DC3FD8F6009C`, rollback189, replaytrue.
- Human inspection of every PNG caught the Start image containing HUD but no
  floor/player meshes. The cooked renderer can defer scene proxies while PSOs
  precache. The capture now waits for the six visible components' real PSO and
  scene-proxy readiness, then the existing three rendered warmup frames.
  The 45-second smoke watchdog did not change.
- Development rebuilt in `runs/20260904-132703-198-package-demo` (162.25 seconds).
  The subsequent `runs/20260904-133016-980-packaged-arena` Smoke passed; all three
  images were inspected and Start now includes actual floor and player meshes.
- Default GameplayCameras pulled EnhancedInput back into the packaged game and
  produced an irrelevant missing PlayerInput warning. Engine plugins now default
  off in the project. ACLPlugin is explicitly retained because UE's default
  animation compression settings refer to its stock assets. No Engine files changed.
- Ordinary Development launch `runs/20260904-133119-322-packaged-arena` returned0
  after its bounded exit, but Windows displayed a firewall prompt from the stock
  UE Trace control listener on TCP1985. No security UI or firewall settings were
  changed. Engine source exposes this control through a build-time trace option,
  not a runtime disable flag. Standard Shipping disables that trace subsystem.
  `-notraceserver` separately prevents the diagnostic Trace Server auto-launch;
  it is not claimed to disable Development's embedded control listener.

## Shipping candidate

Shipping BuildCookRun `runs/20260904-133446-394-package-demo` passed in53.22 seconds,
with no Error/Warning lines. Shipping is the default demonstration package;
Development remains selectable and its diagnostic logs remain part of the audit.

`runs/20260904-133714-538-packaged-arena` passed actual Shipping Smoke with exit0,
confirmed240/240, the unchanged golden hash, rollback189 and verified replay.
Start/correction/convergence PNGs were all opened and checked. The checked-in
documentation image is a preview from this intermediate checkpoint; final
verification uses newly generated ignored captures and their hashes.

Ordinary Shipping launch `runs/20260904-133759-884-packaged-arena` produced a
normal interactive window without the firewall prompt. Native keyboard checks:
P paused at5360; D+Space+period advanced to5361, moved A right and created one
projectile; period after release advanced to5362 with no further A movement and
only the existing projectile. The bounded150-second session returned0 and the
job reaped both processes. Earlier actual PIE tests cover N, mode switches, R,
held inputs and release past attack cooldown.

Full UE Automation under the explicit plugin configuration:
`runs/20260904-134102-106-automation`, 28 passed, zero failed/not-run.

## Core regression and full property sweep

`tools/run-verification.ps1` completed all five configurations with6/6 CTest:
MSVC Debug60.89s, Release16.15s; Clang Debug85.64s, Release11.90s;
ASan+UBSan49.95s. This includes the structured100000-input fuzz smoke.
Log: `artifacts/ue5-0.2/pact84-core-matrix-working.txt`.

The actual10000-seed `--repeat-samples128 --repeat-full` sweep passed:
9600 convergences plus400 declared bounded failures (200 queue overflow,
200 timeout), zero identity mismatches/crashes/deadlocks/unbounded failures,
522 repeated identity samples and7 edge frame cases. The whole sweep repeated;
identity digest `6852576945316271377`. JSON:
`artifacts/ue5-0.2/pact84-property-10000-working.json`.

## Log classification

Development runtime emitted the engine's three old-GPU-driver notices for the
installed551.61 driver and its D3D12 async-queue workaround, plus occasional
millisecond shader-preload waits. Those environmental notices were retained,
not suppressed or resolved by changing the system driver. The missing-input
and missing-ACL-asset warnings were project configuration defects and were fixed.
Shipping does not expose the same verbose engine diagnostics, so absence of its
engine log is not used as proof that Development had no warnings.

Fresh BuildPlugin and complete CLI/C11/packaged parity results follow before
this PACT's scoped commit; final clean-source reruns remain required.

## Completed precommit acceptance

Fresh BuildPlugin `runs/20260904-134114-376-plugin` passed UnrealEditor Development,
UnrealGame Development and UnrealGame Shipping. Its immutable working ZIP SHA is
`1f6e433ed0269b472ca122a6b741b139848b7641da1a58b4d019df7a05286edc`.
The stock WriteMetadata child again prints the known unused builder-argument
diagnostic inherited through UBT_EXTRA_ARGS; the top-level builder consumes those
options, and all three target processes returned0. This diagnostic is retained
and explained in the PACT-82 evidence, not silently ignored as an unknown warning.

The actual three-way verifier passed all16 checks:
`artifacts/ue5-0.2/parity/20260904-134325-034-98663f04/verification.json`.
Installed CLI, genuine C11 consumer and Shipping UE have deeply equal complete
reports and traces, byte-identical Replay v1 files, and all three replays pass
the independent CLI verifier. Confirmed240, hash `0x4B35DC3FD8F6009C`,
rollback189, resimulation857, identity `0x8150FEDA020B66A8`. SDK, Plugin and Demo
ZIP checksums and complete decompressed file trees passed. Source_clean is false
by design for this intermediate run; it is not the final release evidence.

The same six process negatives also passed against the actual Shipping package,
including the inner45-second watchdog: every bootstrap returned1 with a false
failure trace and no remaining child. Results:
`artifacts/ue5-0.2/pact84-packaged-negatives.json` (runs134457 through134504).

Fresh origin/main fetch still resolves to
`5b250ebc985f8e098e7d613e9cab7b0897482cc9`. No other checkout, Engine source,
system driver or firewall setting was changed.

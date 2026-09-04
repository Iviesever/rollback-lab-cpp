# PACT-83 live Arena evidence

Source/SDK baseline: `20fa0f5d25e521d553ceb8d4dda3f3d1dfe5df77`.
The Arena changes are intermediate working-source evidence. Final PACT-84
artifacts must be regenerated together from the final clean commit.

## Native scene and ownership

The generated map selects a native GameMode, which creates a presentation Actor.
It owns a fixed pool of two floor components, four player components and 128
projectile components, six shared material instances and one camera. A Model
borrows the game-instance subsystem's Runtime; it does not own another simulator.
The SDK is the only source of positions, projectiles, hashes and correction events.
The HUD uses a cached Actor reference, with no per-frame actor discovery.

Three bounded modes use the same production C facade: automatic scripted parity,
interactive A input with scripted B, and a controlled damage-bias Desync example.
Network changes reset the scenario. Pause accumulates no debt; period advances
one logical tick. The Desync banner only uses confirmed SDK divergence.

## RED to GREEN

- Seven Model tests: actual RED `runs/20260904-123808-564-automation`, followed
  by actual SDK implementation. No test skipped or removed.
- Map/material commandlet: actual failure `runs/20260904-123602-289-generate-content`,
  followed by native package generation, reopen validation and repeat generation.
- Combined Bridge/Model/Content Automation: 27 passed, zero failed in
  `runs/20260904-124357-319-automation` (before final input/exit corrections).
- Real RHI warmup first exhausted the outer startup bound during shader compile;
  cold-shader warmup is now a separate command. Smoke still has its original
  45-second inner watchdog and 240-second outer process bound.
- A timed-out Smoke wrote false but stock graceful UE shutdown returned zero.
  Failure now saves evidence, unregisters screenshot callbacks, releases SDK
  resources and requests explicit nonzero termination.
- Review reproduction `runs/20260904-130301-220-editor-arena`: zero target returned
  one but wrote no trace. Output paths now initialize before option validation.
- Review reproduction `runs/20260904-130318-449-editor-arena`: early automatic exit
  returned zero with false trace. Both Quit and EndPlay now fail incomplete smoke.
- Review reproduction `runs/20260904-130424-741-editor-arena`: `1,garbage` silently
  became scenario seed one. Complete numeric tokens and uint64 range are now
  validated; launch verification also compares the requested seeds.

## Actual rendering and interaction

`runs/20260904-125547-011-editor-arena` completed real RHI Smoke with process
exit zero, all nine job processes exited, confirmed frames 240/240, 189 rollbacks,
857 resimulated frames, hash `0x4B35DC3FD8F6009C` and report identity
`0x8150FEDA020B66A8`. Its Replay v1 passed the external SDK CLI verifier.
Start, correction and convergence PNGs were each opened and visually inspected:
legible metrics, separate peers, real correction ghosts/lines/flash and equal
final confirmed hashes. These are Editor screenshots, not packaged evidence.

Physical-window checks in `runs/20260904-130038-094-editor-arena` verified P pause
at tick1685, period increments to1686/1687, N hostile preset/reset, mode3 Desync
converging confirmation to240/240 with earliest divergence91, R restart and Esc
clean exit. Very short movement/fire chords did not visibly apply, so a separate
actual PlayerController/PIE input regression was added rather than claiming
movement verification from Model-only tests.

## Review and remaining gate

Independent read-only PACT-83 review found one High (failure process status) and
two Medium issues (early failure trace and numeric truncation), all reproduced
above. Narrow recheck also found separator handling on string paths/SHA; those
reads now retain complete values. There were no other ownership or projection
findings. Final input RED/GREEN, negative-process checks and full Automation
results follow below before the scoped commit.

## Final focused gates

Actual PlayerController/PIE input RED in `runs/20260904-130847-749-automation`
failed only the short D and Space assertions; held movement, pause, step, reset
and resource teardown passed. The View now retains sampled presses until an
actual fixed step consumes them, then clears the bounded bitmask. Reset clears
pending inputs. Full UE Automation in `runs/20260904-131106-787-automation`
passed all 28 tests, including that unchanged input test and its release checks.

`scripts/TestUnrealArenaFailures.ps1 -EngineRoot <UE5.8> -IncludeWatchdog`
passed all six actual-process negative cases. Every root returned exactly one,
with false failure JSON and all descendants exited: zero frames, suffix seed,
overflow seed, early exit, wrong requested SHA and the real 45-second inner
screenshot watchdog under NullRHI. Full results are in
`artifacts/ue5-0.2/pact83-negative-green.json`; no outer timeout was used to
manufacture the expected inner-watchdog status.

Final precommit RHI regression: runs/20260904-131148-230-editor-arena returned0, confirmed240/240, hash0x4B35DC3FD8F6009C, rollback189, replaytrue, all3captures written.

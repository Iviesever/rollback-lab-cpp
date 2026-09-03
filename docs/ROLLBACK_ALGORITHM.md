# Rollback Algorithm

## Prediction

The local input for frame `N` is available immediately. If the actual remote input is missing, the session searches backward through its bounded remote history and reuses the last known button value. With no known remote input it predicts neutral. Frame/player metadata is rewritten for `N`; prediction equality compares the gameplay button value, not transport sequence metadata.

Last-known input is a useful default because held movement is more common than arbitrary direction changes. It is not universally optimal; a fighting game may prefer input decay or character-specific rules.

## Advance

```text
if a correction is pending:
    flush it first
store snapshot[current boundary]
store local input[current frame]
remote = confirmed input or last-known prediction
store exactly the remote value used
simulate one canonical frame
store hash[new boundary]
advance the contiguous confirmed boundary
```

Zero latency therefore uses no predictions. A late actual input that equals the stored used prediction only fills confirmation history; it does not cause meaningless work.

## Earliest dirty frame

Incoming late inputs are written to the remote-confirmed ring. If an input differs from the value actually used for that frame, the session updates:

```text
earliest_dirty = min_in_modular_frame_order(earliest_dirty, input.frame)
```

All inputs from one redundant packet can be ingested before `flush_corrections()`. Frames 7 and 5 arriving together produce one rollback from 5, not two rollbacks.

## Restore and resimulate

```text
dirty = earliest_dirty
depth = current_frame - dirty
require depth <= 120
state = snapshot[dirty]
for frame in [dirty, current_frame):
    local = local_history[frame]
    remote = actual_remote[frame] or prediction_recomputed_from_actual_history
    replace used_remote[frame]
    replace snapshot[frame]
    state = simulate_frame(state, frame, {A, B})
    replace hash[state.frame]
update rollback metrics once
```

Recomputing prediction during resimulation matters: an actual frame 5 input may become the prediction for missing frame 6. Reusing the old prediction array would leave a logically inconsistent replay.

Gameplay side effects are represented by canonical state changes. Resimulation replaces the affected state/hash range; it never appends a second score, damage, or projectile spawn. A regression test verifies that `next_projectile_id` is not incremented twice.

## Confirmation

The confirmed value is a boundary, not the last input index. Boundary `N` means actual local and remote inputs for `[0, N)` are present and incorporated. Hashes are exchanged only for such boundaries. A speculative local frame cannot trigger desync.

## Capacity and trade-off

- History/snapshot capacity: 256 boundaries.
- Maximum accepted rollback: 120 frames (2 seconds at 60 Hz).
- Inputs older than the maximum fail with `rollback_window_exceeded`.
- Snapshot memory grows with `sizeof(WorldState) × history capacity`.
- A larger window tolerates more delay but increases memory and worst-case resimulation cost.

Metrics are rollback count, total resimulated frames, maximum depth, predicted input count, late input count, and confirmed boundary.


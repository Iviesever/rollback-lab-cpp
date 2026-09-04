# Fixed-step adapter

The canonical Core receives one integer frame and its inputs per transition. It never receives Unreal DeltaSeconds, a clock, display frame rate or discarded wall time. The UE bridge alone accumulates wall seconds and invokes a C ABI logical tick for each complete 1/60-second interval.

## Bounded scheduling

`FRuntime::TickWallClock` performs at most **eight** logical steps per callback. It keeps a fractional remainder below 1/60 second and records excess whole-step debt as discarded wall seconds. A long rendered frame therefore cannot cause an unbounded catch-up loop. This policy sacrifices wall-time catch-up under sustained overload: the simulation falls behind real time. It never skips canonical frame numbers, stretches a simulation step or edits a WorldState to compensate.

A high display rate may produce zero steps in a callback; a slower rate may produce several. Identical ordered inputs and logical steps still produce identical canonical states. The same interactive button sample applies to the steps executed in that callback. Human presses sampled at different wall-clock times are different inputs, so independently timed interactive runs have no cross-machine identity guarantee.

The cap of eight is an engine scheduling budget. The rollback limit of 120 and history capacity of 256 are separate Core contracts: they bound accepted correction age and retained tagged history. Increasing catch-up work cannot repair an input older than the rollback window.

## Pause, step, reset and input

| Operation | Wall clock | Canonical effect |
| --- | --- | --- |
| Normal tick | Accumulate nonnegative finite delta, cap work, retain fractional remainder | Execute zero to eight sequential logical ticks |
| Pause | Retain existing fractional remainder; add no paused wall time | No automatic ticks |
| Single step | Add no wall time; preserve pause and remainder | Exactly one logical tick if the run is active |
| Reset | Clear accumulator, discarded-time observation and pause | Destroy driver before peers, create independent fresh sessions, reset metrics/corrections |
| Invalid delta/buttons | Reject before stepping | No mutation from invalid scheduling input |

The Arena retains short movement/attack presses in a bounded pending-bit mask until an actual fixed step consumes them. Sampling a key during a zero-step render callback must not lose the press. Reset clears pending input. A real PlayerController/PIE regression covers short taps, held buttons, paused input and exact step consumption.

## UDP scheduling and deadlines

The UDP runtime uses the same bounded external fixed-step accumulator, but each C ABI call drives one local session over the actual socket. A step polls at most 64 ready datagrams and advances at most one scripted local frame. Handshake and confirmation can consume calls without adding a gameplay frame. The driver has no worker thread or sleep; the CLI blocking entry point wraps the same production driver outside this incremental step.

UE supplies real monotonic elapsed milliseconds since driver creation for handshake/run deadlines. These are sampled from the outer clock, not fabricated as `logical_tick * step_duration`, and never enter canonical state. Discarding accumulator debt therefore does not extend a network timeout. Reversed elapsed time is rejected without advancing; failed and successful terminal states stop canonical work. The public UDP run is scripted and bounded to 240 frames; the dual-view pause/step/input controls do not imply an interactive network feature.

## Tick versus boundary

During the configured simulation interval, a logical tick advances the two independent sessions through the production packet driver. During the bounded retransmission tail and drain, ticks deliver packets and apply corrections without adding gameplay frames. Therefore logical tick, current/predicted state boundary and confirmed boundary are distinct observations. The default 240-frame sample finishes after its delivery/drain phase rather than assuming tick 240 is already confirmed.

Rendering copies snapshots after each successful step. A correction revision increments only when the SDK reports a performed correction. The wrapper keeps its real pre/post worlds for the transient ghost/flash; rendering duration is never part of canonical state or hash. Finished runs stop advancing. Failed runs retain resources only long enough to copy failure trace, then release them on Stop.

Tests cover bounded catch-up, fractional/pause/single-step behavior, reset, invalid/nonfinite deltas, real correction and input retention. See [UE testing](UE5_TESTING.md) and the implementation in [RollbackLabRuntime.cpp](../examples/ue5/RollbackArena/Plugins/RollbackLabBridge/Source/RollbackLabBridge/Private/RollbackLabRuntime.cpp).

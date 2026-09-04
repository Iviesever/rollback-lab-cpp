# Fixed-step adapter

The canonical core receives one integer frame and its inputs per transition. It
does not receive Unreal DeltaSeconds, a clock, display frame rate or discarded
wall time. The UE bridge alone accumulates wall seconds and invokes one C ABI
logical tick for each complete 1/60-second interval.

`FRuntime::TickWallClock` performs at most 8 steps per call. It retains the
fractional remainder below 1/60 second and records excess whole-step debt as
discarded wall seconds. A long display frame therefore cannot create an
unbounded catch-up loop. Discarding wall debt delays the simulation relative to
real time; it never skips canonical frame numbers or edits a WorldState.

Pause retains the existing fractional remainder but accumulates no paused wall
time. SingleStep executes exactly one logical tick, including when paused, and
does not add wall time. Reset destroys the driver before its peers, creates new
independent sessions, and clears the clock, discarded-time observation, metrics,
correction revisions and pause state. Invalid or non-finite deltas are rejected
before stepping.

During the configured simulation interval a logical tick advances both peer
sessions through the existing packet driver. During the bounded tail/drain it
only delivers/retransmits packets and applies real corrections. Thus logical
tick, predicted state boundary and confirmed boundary are distinct values.

The maximum catch-up count (8) is unrelated to the rollback limit (120) and
history capacity (256). The former bounds display-thread work per callback; the
latter two belong to the original deterministic core and remain unchanged.

Rendering copies snapshots after each step. Correction revisions increment only
when the C API reports a performed correction, preserving the real pre/post
world pair for a transient presentation event. Interactive A input is sampled
outside the core and supplied as the current frame's button bits; no cross-machine
determinism claim is made for human input sampled at different wall-clock times.

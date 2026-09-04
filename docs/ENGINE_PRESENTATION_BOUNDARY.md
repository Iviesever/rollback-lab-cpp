# Engine presentation boundary

The SDK owns canonical player/projectile positions, velocities, HP, score, input
history, predictions, snapshots, correction decisions and hashes. UE owns input
sampling, the external wall-clock accumulator, visible geometry and HUD state.

Data flows in one direction after input sampling:

```text
UE keys -> button bits -> C ABI live step -> existing Core sessions and packets
                                      -> copied C snapshots and correction results
                                      -> UE transforms, geometry and HUD
```

A transform is a projection of integer coordinates, not a source of simulation
truth. Moving an Actor, changing camera projection, changing frame rate, or fading
a correction ghost must never write position/velocity back into a Core session.
Rendering arithmetic may use float/double; the canonical C++ state still cannot.

Two views reference two different handles. The production driver exchanges only
versioned packets, and each session owns its World/history/rollback decisions.
The renderer may compare snapshots or place both visual arenas in one UE scene;
it cannot copy one canonical state into the other to manufacture convergence.

Correction presentation consumes a real performed correction's pre/post copied
worlds, start frame and resimulation depth. The UE wrapper increments a revision
when that event is observed, allowing a renderer to display one transient flash
per actual event. Predicted hash differences are expected and are distinct from
confirmed-only desync diagnosis.

The same scripts and seeds pass through CLI, C ABI and UE, and final hashes are
compared with a verified canonical Replay v1. This demonstrates a shared
implementation path; it is not proof that hashing is collision-free or that all
possible gameplay logic is correct. Golden samples, negative tests, compiler
parity and ownership review provide additional independent constraints.

# Interview Guide

## Netcode families

**Deterministic lockstep** waits until all peers have each frame's input, keeping state aligned but adding input latency. **Snapshot interpolation** renders delayed server snapshots smoothly and is common for many entities. **Client prediction** immediately simulates local action, then reconciles against authority. **Rollback** predicts missing remote inputs, simulates immediately, and restores/resimulates when actual input differs. This lab demonstrates peer-style rollback, not an authoritative production server.

## Why canonical state avoids floating point

Floating-point expressions can vary with compiler, target ISA, contraction, rounding mode, and operation order. Integer subunits make range and overflow behavior explicit. The lab uses 1/1,024 world-unit scaling and checked 64-bit intermediates before narrowing to 32-bit state.

## Why last-known input

Players often hold a direction across frames, so repeating the last confirmed buttons usually predicts better than neutral. It is simple, deterministic, and explainable. It is a policy, not a universal truth; games can use input decay or action-specific prediction.

## Earliest dirty frame

The session stores the exact remote value used at every simulated frame. A late actual input is dirty only when its button value differs. Multiple corrections take the earliest modular frame, then one resimulation incorporates every available actual input.

## Snapshot boundary

A snapshot labeled `F` is the state before applying frame `F`. Restoring it and replaying `[F,current)` is unambiguous. Storing post-frame state under the same number commonly creates off-by-one damage or movement duplication.

## Avoiding duplicate events

Canonical effects are replaced by the resimulated state/hash range, not appended. A production presentation layer would key noncanonical audio/VFX by stable `(frame,event_id)` and suppress already-present IDs. This lab's regression verifies projectile identity is not incremented twice.

## Confirmed frame

Confirmed boundary `N` means both actual input streams for `[0,N)` are known and incorporated. It does not mean the current speculative boundary is confirmed. Hash exchange and desync checks are restricted to confirmed boundaries.

## Packet loss versus input loss

A UDP datagram can be lost while its input survives: subsequent packets resend a bounded window. Input loss occurs only when every packet covering that frame is lost or arrives outside the accepted rollback/history window.

## Why redundant input windows help

They provide application-level recovery without waiting for a retransmission round trip. The trade-off is bandwidth. The lab caps the window at 32 and the datagram at 1,200 bytes.

## Sequence and ack

The sequence window distinguishes newest, unseen out-of-order, duplicate, and stale packets across `uint32` wrap. The ack field reports the sender's contiguous observation boundary. It is not TCP reliability; input redundancy and bounded completion states provide the recovery used here.

## Rollback window trade-off

A larger window tolerates worse delay and loss but stores more snapshots and increases worst-case resimulation. This lab stores 256 tagged slots but accepts at most 120 frames of rollback. Older corrections fail closed.

## Desync versus prediction difference

Prediction difference is expected on unconfirmed frames and is repaired by rollback. Desync is an unequal canonical hash for the same confirmed boundary. Treating the former as the latter would create false alarms whenever latency exists.

## What a hash proves

Equal hashes are a compact indication that serialized canonical states likely match. Unequal hashes prove bytes differ. FNV-1a does not prove security, origin, absence of collision, fairness, or cheat resistance.

## Why in-process tests do not replace UDP

They cannot expose socket API semantics, port binding, process startup, serialization integration, OS buffering, child cleanup, or shutdown races. The Windows `WSAECONNRESET` and asymmetric final-confirm bugs in this project are examples only the real path exposed.

## Why localhost UDP does not prove WAN readiness

Loopback omits routing, NAT, MTU variation, congestion, bandwidth competition, path changes, firewalls, adversaries, and geographic RTT. It proves the OS/process/packet path, not production deployment.

## Why timing is outside the hash

Wall-clock duration depends on machine, load, scheduler, and compiler. Hashing it would make identical gameplay nondeterministic. Timing remains an observation in benchmark/report output.

## Unreal integration without coupling the core

An Unreal adapter would translate enhanced-input actions into `InputFrame`, call the core on a fixed tick, mirror canonical entities into Actors/Components, and route packet bytes through the engine transport. The lab stays engine-agnostic so determinism and rollback can be tested without UObject lifetime, rendering, or engine scheduling.


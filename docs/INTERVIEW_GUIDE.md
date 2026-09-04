# Interview guide

Explain the product by tracing one input, one late packet, one correction and one teardown. Use current evidence and distinguish personally completed exercises from the AI-produced delivery. The following fifteen questions cover the 0.2 integration contract.

## 1. Why not expose C++ classes directly to a UE module?

C++ class layout, name mangling, standard-library implementations, exception configuration and allocator ownership are not a stable cross-toolchain contract. Exposing `std::vector`, `unique_ptr` or a Core class would couple UE to those details. A small C ABI keeps fixed-width data and opaque handles public while C++23 and its allocations stay inside the SDK DLL. UE still uses the original implementation.

## 2. What does C ABI solve, and what does it leave unsolved?

It gives a C11-compatible calling/data boundary with explicit versions, sizes, errors and ownership. It avoids exposing STL/class layout and exceptions. It does not make arbitrary versions compatible, make pointers safe, provide thread safety, remove CPU/OS/runtime prerequisites, authenticate a DLL or deploy it automatically. The manifest, staging, thread contract and tests address those separate obligations.

## 3. What are a DLL, import library, static library and CRT?

A DLL contains executable code loaded at runtime. Its import `.lib` tells the Windows linker how to reference exports; it is not the same as a static archive of Core implementation. A static `.lib` contributes object code to the consumer binary. The CRT provides C/C++ runtime facilities, including allocation and I/O. This SDK ships `rollback_lab_c.dll` plus its import library, and separately the original `rollback_lab_core.lib`. UE uses Release `/MD`, and SDK allocations are destroyed inside that DLL. Actual debug-CRT engine targets are rejected.

## 4. Why require both `struct_size` and ABI version?

Version identifies the semantic/layout contract; size tells the callee what storage the caller actually supplied. Checking both before access prevents an older/smaller record from being read as a newer one. In ABI v1 every versioned record requires the exact size and zero reserved input bytes. Appending a field does not silently establish compatibility; incompatible changes require a new ABI version and tests. The UDP extension instead adds separate functions and new records, leaves all existing layouts intact and advertises `RL_CAP_UDP=8`; a new consumer checks that capability rather than assuming every ABI-v1 DLL has UDP.

## 5. Who owns handles, and in what order are they released?

The SDK allocates and destroys opaque session and live-driver handles. The live driver borrows two distinct unused A/B sessions; direct mutation or destruction while borrowed returns `RL_BORROWED`. The UDP driver borrows one unused session under the same guard. Destroy the live/UDP driver, then its owned session handle(s), then the DLL lease. All operations stay on the creating thread. UE native RAII, the game-instance subsystem, EndPlay and module pre-exit/shutdown enforce this order. Stale or forged handle pointers remain caller errors outside the safety contract.

## 6. Why must UE Delta Seconds stay out of canonical state?

Rendered frame duration depends on hardware, load and scheduling. Putting it into movement, state or hashes would make identical ordered inputs yield different results. UE accumulates wall seconds outside Core; each canonical transition receives only its integer frame and inputs. Presentation can use floating point, while authoritative state uses integer subunits and checked arithmetic. UDP additionally receives caller-provided monotonic elapsed milliseconds for transport deadlines; those are not simulation time or canonical input.

## 7. What is the accumulator/catch-up trade-off?

Accumulate elapsed wall time, execute one logical tick for each complete 1/60 second, and retain the fractional remainder. The bridge caps a callback at eight steps and discards excess whole-step wall debt observationally. That bounds game-thread work but can slow simulation relative to real time under overload. It never skips canonical frame numbers. Pause accumulates no debt; single step performs exactly one logical tick while preserving pause. The scheduling cap is unrelated to the 120-frame rollback limit and 256-frame history capacity.

## 8. Why is Actor Transform only presentation?

Core owns positions, velocity, HP, projectiles and collision outcomes. UE copies snapshots and projects them into reusable geometry. Feeding an Actor transform or engine physics result back into Core would introduce a second authority dependent on rendering/physics scheduling. The correction ghost and flash are transient views of actual pre/post SDK correction data and do not alter gameplay state.

## 9. How do two peers prove they do not share state?

They have distinct move-only Core sessions and distinct opaque handles, each with its own world, input history, snapshots and rollback decisions. Tests advance one while observing the other unchanged, reject shared/swapped/used session pairs, and compare independent histories. The production driver communicates only encoded versioned packets. UE displays the two canonical worlds in one scene; that display arrangement does not imply shared canonical ownership. Handle inequality alone is insufficient, so behavior tests and code review check the path as well. The UDP supervisor additionally checks distinct Shipping PIDs, SDK-bound ports and one session per client; the relay owns no world.

## 10. How does CLI/C API/UE hash parity support a single algorithm?

Run identical scripted inputs, seeds, transport settings and frame count through the CLI, a true C11 consumer and the packaged UE executable. Compare confirmed hashes, full reports and identity, and replay bytes/reconstruction; validate exact source/DLL/artifact identity. All three delegate to the same `LiveScenario` and Core. This evidence plus dependency review constrains accidental duplication or divergence. Matching hashes alone cannot prove no collision or shared bug; a regression makes both peers equally wrong and still rejects them against canonical replay. Final packaged claims must come from the generated verifier JSON, not an Editor screenshot. This full report/trace equality applies to the seeded path. Real UDP uses OS scheduling, so verify the aggregate handshake profile, confirmed hash and canonical replay; counters/report identities can differ, and correction is required across the pair rather than on each endpoint.

## 11. What distinguishes prediction difference from confirmed desync?

A peer may be ahead using last-known remote input while another has different speculative information. Those unconfirmed differences are expected. A late actual mismatch marks the earliest dirty input, restores its pre-input snapshot and resimulates `[dirty,current)`. Desync means unequal canonical hashes for the same confirmed boundary, where both actual input streams are known and incorporated. The controlled damage-bias mode reports earliest confirmed divergence, 91 for its default seed, without treating normal correction as a desync.

## 12. What can a packaged executable expose that Editor tests miss?

A cooked build can omit the map/material, fail to stage a DLL/manifest, select a different target/runtime configuration or fail at bootstrap before the game starts. Editor search paths, loaded content and existing modules may hide those problems. Real BuildCookRun, ordinary EXE launch, packaged smoke, captured images, process exit checks and three-way parity execute the deployed path. BuildPlugin and NullRHI Automation remain separate useful gates.

## 13. Why is this not UE Replication, Iris or production networking?

The Runtime plugin calls RollbackLab C ABI and its versioned packet-driven Core. UE presents the results; it does not replace the protocol with Replication, Iris or Network Prediction Plugin. The dual-view Arena runs seeded packets in one process; PACT-85 also runs two real Shipping UDP clients with the existing relay. Its normal runs and six negatives pass. Each client owns one session and calls the shared `PeerDriver` through C ABI. The `engine-udp-v1` digest validates ABI together with protocol/simulation/seeds/target, without inventing a new remote-ABI field in Protocol v1. The product is a bounded two-peer teaching laboratory, without the authority, security, operations and broad gameplay requirements of production networking.

## 14. Why does localhost UDP not establish WAN readiness?

Both the CLI demo and the UE UDP supervisor really start a relay and two independent peer targets, send datagrams, check handshake/profile identity, converge, verify replay and reap owned children. Loopback still omits geographic RTT, path changes, NAT/firewalls, MTU variation, congestion, competing traffic, service deployment and adversarial conditions. The seeded emulator explores bounded policies; it is not calibrated proof of an Internet connection or production SLA.

## 15. What did AI do, and what can the user honestly claim?

The user set the career goal, product direction, deadline, boundaries and acceptance criteria. Codex GPT-5.6 Sol refined the architecture and produced the C ABI, SDK, UE plugin/demo, tests, debugging, packaging, visual inspection and documentation, building on the AI-assisted Core work. The user did not hand-write delivery code in this session and cannot claim independent hand-written authorship. The honest description is an AI-assisted project with user-defined product/acceptance constraints. Personal understanding or changes should be claimed only after actually completing them. Before an interview, study C ABI, fixed-step, presentation ownership and rollback flow and complete at least one [live change drill](LIVE_CHANGE_DRILLS.md).

## Core questions to retain

**Lockstep, interpolation and rollback:** deterministic lockstep waits for inputs and trades responsiveness for alignment. Snapshot interpolation renders delayed authority snapshots. Client prediction acts locally and reconciles later. Rollback predicts missing input, restores earlier state when wrong and resimulates. This lab demonstrates peer-style rollback, not a production authoritative server.

**Snapshot and confirmation boundaries:** snapshot F is the state before input F. Confirmed boundary N means actual input for `[0,N)` is known and incorporated; it does not mean current speculative state is confirmed. Off-by-one boundaries can duplicate damage or movement.

**Prediction policy:** repeating last-known input is deterministic and often useful for held directions. It is a policy, not a universal optimum; neutral/decayed or action-specific prediction needs its own tests.

**Packet loss versus input loss:** bounded later packets resend earlier input, so losing a datagram need not lose its input. Recovery fails when all covering packets are lost or arrive outside accepted history. The window is capped at 32 inputs and packets at 1,200 bytes; sequence observation covers 64 packet positions across uint32 wrap.

**Rollback cost:** a larger window tolerates older input but stores more history and increases worst-case replay work. Tagged slots prevent stale ring data from masquerading as the requested frame. This Core keeps 256 slots but accepts at most 120 frames of rollback.

**Hashes and security:** unequal hashes prove the serialized bytes differ; equal FNV-1a hashes indicate likely agreement but do not authenticate origin or exclude collisions. CRC-32 detects accidental corruption. Neither provides encryption, anti-cheat or security.

**Timing and visual effects:** wall-time benchmark data is observational and excluded from deterministic identity. Canonical effects are replaced by resimulation. A production audio/VFX system would need stable event identity and reconciliation; the lab's bounded correction presentation is narrower.

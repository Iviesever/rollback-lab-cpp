# Architecture

Rollback Lab separates deterministic state transitions from every wall-clock and operating-system concern. The production library is reused by tests and by one thin CLI executable; there is no test-only or CLI-only netcode implementation.

```text
core ──► simulation ──► netcode
  │            │            │
  └────► protocol ◄──────────┤
              │              │
              ▼              │
        seeded transport     │
              │              │
              └────► report / replay / trace
                              │
platform UDP/process ─► relay / peer / supervisor
                              │
                              ▼
                        self-contained viewer
```

## Module boundaries

| Module | Owns | Must not own |
| --- | --- | --- |
| `core` | typed errors, frame arithmetic, PCG32, FNV-1a | gameplay, sockets, time |
| `simulation` | integer canonical state and one-frame transition | prediction, network, wall clock |
| `netcode` | one peer's histories, snapshots, prediction, rollback, confirmation | another peer's state |
| `protocol` | checked little-endian bytes, packet v1, CRC, sequence window | sockets, session state |
| `transport` | seeded logical scheduling or OS datagrams/processes | gameplay decisions |
| `replay` | strict confirmed-input artifact and verification | prediction history |
| `report` | canonical JSON, trace, desync evidence, in-process scenario | socket implementation |
| `udp` | opaque relay, independent peer mode, supervisor | shared world state |
| `cli` | parsing, file I/O, timing observations | a second simulation/netcode path |

## Canonical frame boundary

`WorldState.frame == N` means the state immediately before frame `N`. A snapshot tagged `N` stores exactly that boundary. Inputs for `N` produce boundary `N + 1`. A correction at frame `F` restores snapshot `F` and resimulates `[F, current_frame)`. This convention is shared by hashes, checkpoints, replay, and diagnostics.

## Ownership model

`RollbackSession` is move-only. Each instance owns a heap-resident fixed-capacity `Storage` containing 256 tagged slots for local inputs, remote inputs, used predictions, snapshots, and hashes. The one construction allocation avoids the Windows thread-stack overflow that an inline snapshot array would cause; tick and resimulation paths do not allocate that storage.

The in-process harness can call public methods and compare copied `SessionReport` values. It cannot access ring storage or assign one session to another. The UDP supervisor sees only child PIDs, artifact paths, exit codes, and report/replay files.

## Process topology

```text
udp-demo supervisor
  ├─ relay process: UDP source-port routing of opaque bytes only
  ├─ peer A process: local A input + predicted/confirmed B input + private session
  └─ peer B process: local B input + predicted/confirmed A input + private session
```

Ports are dynamically reserved on loopback. The relay binds before writing its ready file. Peers validate hello packets for protocol, scenario, and identity, then exchange input packets with a 32-frame redundant window. Completion requires contiguous actual inputs, equal confirmed final hashes, verified replays, zero peer exits, a graceful relay stop, and reaped children.

## Resource and failure model

Fallible APIs return `Result<T>` with a stable `ErrorCode`, numeric detail, byte/frame offset, and context. Socket and process handles are move-only RAII objects. Queues, replays, traces, packet sizes, receive loops, handshake time, run time, and final drain are bounded. No thread is detached.

See [DETERMINISM_CONTRACT.md](DETERMINISM_CONTRACT.md), [ROLLBACK_ALGORITHM.md](ROLLBACK_ALGORITHM.md), and [PROTOCOL.md](PROTOCOL.md) for the contracts behind these boundaries.


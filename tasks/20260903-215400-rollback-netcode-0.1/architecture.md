# Architecture

## Decision

Build one static library, `rollback_lab`, and one thin command-line executable, `rollback_lab_cli` (output name `rollback_lab`). The CLI dispatches subcommands, while production behavior remains in library modules used directly by tests. Peer, relay, and supervisor are modes of the same executable so there is one codec and one netcode implementation.

This was selected over separate peer/relay binaries (more packaging and orchestration duplication) and a header-only design (weaker dependency boundaries and slower incremental builds).

## Dependency direction

```text
core <- simulation <- netcode
  ^          ^           ^
  |          |           |
protocol <- replay/report |
  ^                      |
  +---- transport --------+
             ^
             |
          cli/udp

viewer consumes report/trace files only
tests consume production public APIs
```

`core` defines identifiers, frame arithmetic, checked integers, typed results, hashing, and version constants. `simulation` owns canonical state and the only gameplay transition. `netcode` owns one peer's prediction, histories, snapshots, rollback, confirmation, and metrics. `protocol` owns byte codec and sequence filtering without socket APIs. `transport` provides deterministic emulation plus a platform UDP datagram adapter. `replay` and `report` serialize strict artifacts from public state. `cli` composes these modules but contains no second implementation.

## Canonical frame boundary

`WorldState.frame == N` means the state immediately before simulating frame `N`. A snapshot keyed by `N` stores exactly that boundary. To simulate frame `N`, the engine consumes both players' inputs for `N`, applies gameplay in stable-ID order, and returns state boundary `N+1`. A mismatch first discovered for frame `F` restores snapshot `F`, replaces predictions with all known confirmed inputs, and resimulates `[F, current_frame)` once. This definition prevents off-by-one restore bugs and duplicate event application.

Gameplay events are derived outputs tagged with `(frame, stable_event_id)`. Resimulation replaces the derived event range instead of appending it. Persistent score/HP live only in canonical state.

## Confirmation and convergence

Each packet carries a bounded redundant window of the sender's inputs and its highest contiguous received-remote frame. A peer's confirmed frame is the greatest frame for which it knows both actual inputs have been incorporated. Hashes are sent only for confirmed boundaries. Packet loss is therefore not input loss when a later redundant window covers the missing frame. Inputs older than the 120-frame rollback limit fail closed.

The in-process harness owns two peer objects but can only call public input/packet/report interfaces. It may compare final reports; it cannot access or overwrite peer internals. The UDP supervisor owns only child processes and output paths. The relay forwards opaque datagrams and cannot decode canonical state.

## Deterministic transport

The emulator turns each outbound packet into zero or more immutable scheduled deliveries using PCG32. It records every random decision, applies a deterministic stable ordering key `(delivery_tick, reorder_rank, enqueue_ordinal, duplicate_ordinal)`, and enforces packet count, byte queue, and maximum-age bounds. Wall time is absent; logical network ticks drive delivery.

UDP makes no byte-for-byte scheduling claim. The relay may use wall-clock waits, sockets, and process scheduling, while simulation and replay remain deterministic.

## Protocol

Packets have a fixed header followed by bounded records and trailing CRC-32/ISO-HDLC. Each integer is appended/read explicitly in little-endian order. Decode uses a cursor that checks remaining bytes before every field and returns status plus byte offset and context. Unknown magic/version/type and non-canonical lengths fail closed. A sequence window classifies packets as new, duplicate, old-but-acceptable, or stale; out-of-order new packets can contribute previously missing inputs.

## Replay, report, and trace

Replay is a versioned binary file containing scenario identity, seeds, confirmed input pairs, final frame, checkpoints, expected final hash, and CRC. Canonical JSON is emitted by explicit writers in a documented fixed key order; timing fields are observations excluded from identity. Trace is bounded JSON: frame samples, canonical display state, packet events, rollback ranges, hashes, and optional desync metadata. Long runs use a configured sample interval plus all event frames.

The viewer generator escapes trace JSON into one HTML file with embedded CSS and JavaScript. It does not invent state: all scene and markers come from trace records. A checked-in small sample viewer is generated from the sample trace using the production CLI.

## Real UDP process topology

`udp-demo` reserves random loopback ports, writes a run manifest, starts `relay`, then starts `peer --id A` and `peer --id B` with unique report/replay paths. A handshake binds scenario ID, protocol version, simulation version, and peer ID before inputs are accepted. The supervisor monitors bounded completion, terminates timed-out children through the platform process wrapper, waits for exit, and validates reports and replays. Normal completion is zero; protocol mismatch, desync, timeout, bind conflict, malformed report, replay mismatch, or abnormal child exit is non-zero.

## Error and resource model

Public fallible APIs return `Result<T>`/`Status` with a stable `ErrorCode` and contextual numeric offset/frame. Socket, platform startup, file, thread, and process handles are move-only RAII types. Queues and arrays have explicit maxima. Threads receive stop tokens/atomic stop state and are always joined; subprocesses are waited or forcibly terminated within a documented watchdog.

## Test strategy

Unit tests validate simulation, frame math, rings, rollback metrics, codec byte boundaries, and replay. Integration tests run two opaque peers over deterministic transport and real UDP subprocesses. Property tests sweep 10,000 bounded scenario/transport seeds and repeat identity subsets. Protocol fuzz feeds deterministic random bytes plus mutation corpora. Golden canonical bytes/hashes run under every compiler. Browser QA opens the real sample viewer, checks console output, manipulates controls/scrubber, compares rollback markers to trace, and captures desktop/mobile screenshots.


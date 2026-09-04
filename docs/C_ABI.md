# Stable C ABI v1

[rollback_lab_c.h](../include/rollback_lab/c_api/rollback_lab_c.h) is a genuine C11 interface with 26 `rl_` exports. The shared SDK owns every C++ object, allocation and exception. Callers receive opaque `rl_session*`, `rl_live*` and `rl_udp_peer*` handles, explicit status values and copied plain-data records. No STL, Unreal type, C++ reference, virtual table or allocator ownership crosses this boundary.

The DLL was chosen to keep C++ class/STL layout and allocation inside one binary. It still requires the correct CPU architecture, runtime prerequisites, version semantics and deployed DLL. The original static C++ Core is also installed, but UE uses the C ABI DLL. See [SDK packaging](SDK_PACKAGING.md).

## Version and layout

API version is 1; the SDK package is 0.2.0 Candidate. Simulation, protocol and replay remain version 1. `rl_get_version` reports all of these, capability bits and the compiled source Git SHA. Every versioned input/output structure starts with `uint32_t api_version` and `uint32_t struct_size`. Zero-initialize the structure, then set both fields before calling:

```c
rl_version_info version = {0};
version.api_version = RL_API_VERSION;
version.struct_size = (uint32_t)sizeof(version);
rl_status status = rl_get_version(&version);
if (status != RL_OK) {
    /* Handle the typed failure before using the output. */
}
```

Version 1 requires the exact size. It does not promise that appending fields is automatically compatible; incompatible extensions require a new ABI version. Reserved input bytes must be zero. The supported layout is x64, little endian, with natural alignment up to eight bytes. Do not surround the header with a packing pragma. Status and peer values are `uint32_t` constants, so no C enum representation is assumed.

Positions and velocities are signed integer copies in the Core's 1/1,024-world-unit scale. The shared C/C++ layout checks cover offsets, alignment and complete sizes, including session config 24, input 24, player/projectile record 32 each, world snapshot 2,144, metrics 48, version info 88, live config 72, live correction 4,312, UDP config 72 and UDP step result 32 bytes. The UDP extension adds seven functions, two new records and `RL_CAP_UDP` (`8`) without altering any existing ABI-v1 export or record layout. A consumer that needs UDP must check that capability and resolve the new surface; an older ABI-v1 DLL may not provide it.

## API surface

| Operations | Contract |
| --- | --- |
| `rl_get_version` | Query SDK/ABI/canonical versions, capabilities and source identity |
| `rl_session_create`, `rl_session_destroy` | SDK-owned allocation and destruction on the creating thread |
| `rl_session_advance`, `rl_session_ingest_remote`, `rl_session_flush_corrections` | Advance local input, ingest actual remote input, then perform coalesced correction |
| `rl_session_get_snapshot`, `rl_session_get_confirmed_frame`, `rl_session_get_metrics` | Copy read-only presentation and observation data |
| `rl_session_get_hash`, `rl_session_hash_at`, `rl_session_serialize_state` | Current hash, retained confirmed-boundary hash, canonical bytes |
| `rl_live_create`, `rl_live_destroy`, `rl_live_step` | Borrow two independent sessions and drive the existing packet scenario one logical tick at a time |
| `rl_live_get_correction` | Copy a real correction's pre/post worlds, earliest frame and depth |
| `rl_live_copy_report`, `rl_live_copy_trace`, `rl_live_copy_replay` | Copy bounded artifacts into caller storage |
| `rl_udp_peer_create`, `rl_udp_peer_destroy`, `rl_udp_peer_step` | Borrow one session, own its loopback socket, poll bounded datagrams and advance scripted local input |
| `rl_udp_peer_get_correction` | Copy the actual local UDP correction into the existing correction record |
| `rl_udp_peer_copy_report`, `rl_udp_peer_copy_replay`, `rl_udp_peer_copy_failure` | Successful report/replay and all-phase status/failure JSON through sized copies |

## Ownership and failures

Create and destroy each handle through the SDK exactly once. All handle operations, including queries and destruction, are affine to the creating thread; concurrent access is unsupported. Wrong-thread calls return `RL_WRONG_THREAD` before reading mutable state. Stale/forged handles and invalid storage pointers are outside the contract: an opaque pointer is not a memory-safety sandbox.

Every exported function contains unexpected C++ exceptions and maps them to `RL_INTERNAL_FAILURE`. After an internal failure, destroy the live driver and sessions and restart. Typed statuses distinguish invalid/null argument, ABI version, structure size, peer, frame, stale confirmed history, rollback-window overflow, insufficient buffer, capacity, timeout, desync, replay mismatch and I/O. UDP adds `RL_NETWORK_VERSION` (17), `RL_HANDSHAKE_PROFILE` (18) and `RL_PACKET` (19).

The session facade rejects future remote inputs before they can overwrite retained history. Callers queue future inputs until their frame is current. Current and retained past input follow the existing Core semantics, including the 120-frame fail-closed rollback limit. Invalid argument/version/size tests check rejection before mutation and preservation of caller output where promised.

## Live driver

Create two distinct unused frame-zero sessions, local A and local B, then call `rl_live_create`. The live driver borrows both. While borrowed, direct session mutation or destruction returns `RL_BORROWED`; snapshot, hash and metrics queries remain available. Destroy the driver first, then the two sessions, all on the creating thread. Borrowing becomes visible only after construction succeeds.

The driver calls the incremental production `LiveScenario` that also backs `run_seeded_scenario`. It does not implement another simulator, predictor, packet codec, transport scheduler, snapshot algorithm or hash. One `rl_live_step` advances one logical transport tick. Scripted mode uses the existing seeded input. Interactive mode replaces only A's current-frame buttons; B learns them through versioned packets, never a copied world.

The facade caps simulation at 36,000 frames, tail retransmission at 4,096 ticks followed by 32 drain ticks, latency/jitter at 10,000 ticks, queues at 65,536 packets/64 MiB, bandwidth at 64 MiB per tick and packet age at 100,000 ticks. These embedding bounds do not change the standalone C++ API contract.

`rl_live_get_correction` reports only the most recent step's actual correction. Its `performed` flag, before/after snapshots, earliest dirty frame and resimulation depth drive the UE ghost and flash. Predicted differences are expected. Desync observations require a shared confirmed boundary. A controlled desync can finish and export an explicitly unsuccessful report; it must not be called convergence.

Successful completion also requires the peers' final hash to match canonical replay reconstruction. A regression configures both peers with the same faulty simulation variant: they agree with each other, but the API returns `RL_REPLAY_MISMATCH` against the canonical replay. Two matching peer hashes alone cannot manufacture success.

## Incremental UDP driver

`rl_udp_peer_create` borrows one unused frame-zero session on its creating thread. It owns transport state and a bound loopback socket, not another world. Queries remain available; direct session mutation/destruction is blocked until the UDP driver is destroyed. The existing CLI `run_peer` now drives the same production `PeerDriver`; UE does not implement another UDP algorithm.

Each `rl_udp_peer_step` polls at most 64 immediately ready datagrams and advances at most one scripted local frame. There is no worker thread or sleep inside the step. The caller supplies monotonic elapsed milliseconds since creation for transport deadlines and calls at fixed-step cadence. These milliseconds never enter canonical simulation state. Reversed elapsed time is rejected without advancing. Successful terminal calls are idempotent; failures retain their typed error and failed phase. A real isolated `bad_alloc` regression verifies that a caught step exception also leaves consistent error/phase/context state; its allocation injector exists only in the test executable.

The UDP facade permits 1..240 frames, 1..60,000 ms handshake/run deadlines, and a nonzero relay port. `listen_port=0` asks the SDK socket to bind an ephemeral port. The all-phase `copy_failure` JSON reports the actual bound port, source/peer/profile identity, handshake/phase and any failure/desync diagnostic. Reports and replay require successful completion. The short frame bound permits full Replay v1 reconstruction from the existing retained input history.

The named `engine-udp-v1` configuration digest is domain-separated from the legacy CLI profile. It binds ABI, protocol and simulation versions, both seeds and target frame using the existing Protocol-v1 Hello configuration field. ABI agreement is checked as part of that aggregate profile; there is no independently readable remote-ABI wire field. Advertised-version zero selects the compiled version; nonzero values deliberately advertise a negative-test version, never override the expected version. Protocol v1 and Replay v1 remain unchanged, as does the legacy CLI handshake profile.

Real UDP schedule/counters and report identities can differ across peers and repeated runs. Check profile/source identity, equal confirmed hashes and equal peer replay bytes verified by the canonical CLI. Require prediction/correction in aggregate across the two clients: one may legitimately have zero local rollbacks. This differs from the byte-stable seeded three-way report/trace comparison.

## Buffers and verification

Two-stage copies accept null/zero capacity to return the required count and `RL_BUFFER_TOO_SMALL`. Insufficient storage receives no partial bytes. JSON sizes include the terminal NUL; binary replay sizes do not. For the seeded live facade, trace remains available after a failed transport step, and report/replay require completed logical execution, including a controlled desync demonstration. For UDP, report/replay require successful completion; controlled UDP desync is exported through `rl_udp_peer_copy_failure` with its confirmed diagnostic, not as a completed replay.

The installed `rollback_lab_c_demo <scenario-seed> <transport-seed> <frames> <existing-out-dir>` is compiled as C11 and exports report, trace and Replay v1 through the DLL. The CLI takes the same full-width seeds through `--scenario-seed` and `--transport-seed`. Existing default sample identity remains unchanged.

Seven session tests include 128 delayed-input C++/C parity scenarios. Four live tests include 128 complete packet-driven scenarios with full report/replay comparison, borrowing negatives, genuine correction/desync and canonical-replay rejection. Six UDP cases add real C11 calls, loopback relay traffic, profile/version rejection, monotonic clock, terminal behavior, confirmed desync and teardown. The isolated caught-exception regression is a separate CTest executable, not a seventh case in that runner. A separate real C11 smoke and two installed `find_package` consumers cover the language/package boundary. See [testing](TESTING.md) for current evidence rather than inferring correctness from the header alone.

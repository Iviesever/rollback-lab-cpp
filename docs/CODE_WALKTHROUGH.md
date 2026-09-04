# Code Walkthrough

Start with these files in order:

1. `include/rollback_lab/simulation/state.hpp` — the entire integer canonical state and hard capacity/range constants.
2. `src/simulation/simulation.cpp` — the pure one-frame movement, attack, projectile, collision, damage, score, and serialization transition.
3. `include/rollback_lab/netcode/session.hpp` and `src/netcode/session.cpp` — prediction, tagged rings, snapshot boundary, late-input detection, rollback, confirmation, and metrics.
4. `include/rollback_lab/protocol/packet.hpp` and `src/protocol/codec.cpp` — the socket-independent wire contract and fail-closed decoder.
5. `src/transport/seeded_transport.cpp` — PCG32 logical scheduling and bounded hostile-network policies.
6. `src/report/scenario_runner.cpp` — two independent sessions connected only by encoded packet bytes.
7. `src/replay/replay.cpp` — strict confirmed-input persistence and reconstruction.
8. `src/report/desync.cpp` and `src/report/canonical_json.cpp` — confirmed-only diagnosis and stable artifacts.
9. `src/transport/udp_socket.cpp`, `src/transport/process.cpp`, and `src/udp/*.cpp` — actual OS datagrams, child lifecycle, opaque relay, peers, and supervisor.
10. `src/report/viewer.cpp` — production trace embedded into a no-CDN HTML inspector.

Tests call these public APIs directly. `tests/unit/rollback_session_test.cpp` is the clearest executable explanation of boundary semantics. `tests/protocol/protocol_transport_test.cpp` documents hostile input behavior. `tests/udp/udp_demo_test.cpp` proves separate processes and bounded negative paths.

The CLI in `src/cli/commands.cpp` intentionally remains orchestration: it parses, calls production APIs, writes files, and measures outer timing. If gameplay, codec, or rollback logic appears there, the dependency boundary has regressed.


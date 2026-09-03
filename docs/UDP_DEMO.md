# Real Localhost UDP Demo

## Run

From a built configuration:

```powershell
build\msvc-debug\rollback_lab.exe udp-demo --frames 120 --out artifacts\udp-demo
```

The supervisor dynamically selects three distinct loopback ports, starts an opaque relay, waits for its ready file, then starts Peer A and Peer B. The peers are independent OS processes using the same production codec and rollback library.

## Startup and completion

Hello packets establish protocol version, simulation version, scenario/config digest, target frame, and peer identity. Receipt is acknowledged with one final hello to avoid an asymmetric two-message race. Inputs then flow in bounded 32-input windows while up to 32 recent confirmed hashes provide earliest-overlap desync evidence.

After the target frame, each peer retransmits its final window and confirmed hash. Once it has the complete remote input log and the remote final hash, it remains in a bounded 16-iteration final drain so the other peer can reach the same condition. This is a protocol state, not an arbitrary long sleep.

Each peer independently writes:

- `peer-a-report.json` / `peer-b-report.json`
- `peer-a-input.rlr` / `peer-b-input.rlr`

The supervisor requires zero peer exits, asks the relay to stop through a separate six-byte control message, waits for a zero relay exit, decodes both replays, compares confirmed input logs, reruns replay verification, and checks both reports for success, target confirmation, and equal hashes.

For a controlled failure path:

```powershell
build\msvc-release\rollback_lab.exe udp-demo --frames 240 --inject-desync --out artifacts\udp-desync
```

Peer B runs the deliberate damage-bias fault while advertising the same scenario contract, simulating a real logic bug. The detecting peer writes `peer-a-desync.json` or `peer-b-desync.json` from its own confirmed input/hash/snapshot history before returning nonzero. The supervisor preserves typed `desync` even if the other peer later reaches its watchdog.

## Platform implementation

Windows uses Winsock and `CreateProcessW`; POSIX builds use BSD sockets and `posix_spawn`/`waitpid`. Windows UDP reset notifications are disabled with the documented vendor ioctl and residual `WSAECONNRESET` is normalized to an empty receive because UDP is connectionless. Other socket errors remain typed failures.

## Negative behavior

Automated tests cover:

- occupied relay port;
- missing Peer B / handshake timeout;
- protocol version 99 mismatch;
- simulation version mismatch;
- controlled canonical desync with peer-local diagnostic;
- abnormal child result and supervisor watchdog;
- child termination/wait/handle cleanup.

The evidence suite also runs 20 consecutive successful demos and asserts that no `rollback_lab` child remains.

## Limits

The relay currently forwards immediately and does not model Internet routing, NAT, congestion, encryption, authentication, or matchmaking. Adverse latency/loss/reorder/duplicate behavior is covered by the deterministic in-process transport. Localhost UDP validates the real socket, process, packet, shutdown, and artifact path—not production WAN fitness.

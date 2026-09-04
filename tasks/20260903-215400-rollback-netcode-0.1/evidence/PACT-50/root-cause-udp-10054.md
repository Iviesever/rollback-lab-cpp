# Root Cause Packet — UDP Peer `WSAECONNRESET`

## Minimal reproduction

```powershell
rollback_lab.exe udp-demo --frames 90 --watchdog-ms 4000 --out artifacts/udp-debug
```

Observed supervisor result: `child_failure`, peer A exit 60. Both peer diagnostic files reported `io_error`, detail 10054, context `recvfrom`.

## Facts

- Relay created `relay.ready` and did not emit a relay error diagnostic.
- Both peers emitted the same Winsock 10054 diagnostic before report/replay generation.
- Windows UDP may surface an ICMP port-unreachable from early handshake forwarding as `WSAECONNRESET` on a later receive.
- The first proposed socket change used `SIO_UDP_CONNRESET=FALSE`; the second also normalized a residual 10054 receive into an empty datagram.
- Both subsequent manual runs produced identical old behavior.
- A non-suppressed build showed `udp_socket.cpp(118): error C2065: SIO_UDP_CONNRESET undeclared` and exit 2.
- The source timestamp was later than `rollback_lab.exe`; therefore neither proposed behavior had entered the executed binary.

## Excluded hypotheses

- Canonical simulation or rollback failure: no report was reached and the error is at `recvfrom`.
- Relay process crash before startup: ready file exists and no relay error file was emitted.
- Protocol decode mismatch: the failure context is socket receive, before decode.
- The 10054 normalization itself failed at runtime: the updated code never compiled into the executable.

## Root cause and ownership fix

The root product issue is Windows UDP reset semantics during startup. The investigation issue was a hidden native-build failure because PowerShell `ErrorAction=Stop` does not by itself make every native nonzero exit throw.

Ownership fix:

1. Define the documented UDP reset control value locally as `_WSAIOW(IOC_VENDOR, 12)` instead of depending on an SDK macro declaration.
2. Keep both defenses: disable reset reporting at socket creation and treat any residual `WSAECONNRESET` as no datagram.
3. Check `$LASTEXITCODE` explicitly whenever build output is suppressed.
4. Rebuild and verify executable timestamp before rerunning the unchanged reproduction.


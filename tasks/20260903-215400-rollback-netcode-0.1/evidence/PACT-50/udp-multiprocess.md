# PACT-50 Real UDP Multiprocess Evidence

## Contract

- Objective: run an opaque localhost relay and two independently owned rollback peers as three child processes.
- Gap: the seeded scenario used only an in-process logical transport.
- Guardrail: relay never includes protocol/world headers, ports are dynamically reserved, every child has a watchdog/wait/terminate path, and peer state is never copied.
- Done when: normal convergence, reports/replays, port conflict, missing peer, protocol mismatch, repeated stability, and zero residual children all pass.

## RED

```text
tests/udp/udp_demo_test.cpp: fatal error C1083:
cannot open include file 'rollback_lab/transport/udp_socket.hpp'
```

## GREEN and negative-path regression

```text
Compiler: MSVC 19.51.36248.0
Configuration: msvc-debug, warning-as-error
Feature commit: 26721d0bdfd248f9068f2ecc9a9d0f2286c0dca5
CTest: 2/2 passed
Internal cases: 41 tests, 0 failures
Normal UDP stress: 20/20 passed
Residual rollback_lab child processes after stress: 0
Negative cases: occupied relay port, missing Peer B, protocol v99 mismatch
```

Fresh 120-frame run:

```text
Relay PID: 32432
Peer A PID: 26864
Peer B PID: 20220
Confirmed frame: 120
Final hash A/B: 0x84F6E54086A52EF3 / 0x84F6E54086A52EF3
Peer A sent/delivered: 138/137
Replay verification: true
Relay/Peer A/Peer B exit: 0/0/0
Residual children: 0
```

Artifact SHA256:

```text
peer-a-report.json 954496F237C4EE83C42445C778667A73406111CEDF85565BC9BF54A7FE5F6B43
peer-b-report.json 5001CE7A575BB04C2E0C9EC718C4555DCB82D3FFC6F609B8FE692286B56243B1
peer-a-input.rlr  37B205DBE874ECE7E12BFA58AF2074A9C4AE807AC6A57ECF89CAD97133E14865
peer-b-input.rlr  37B205DBE874ECE7E12BFA58AF2074A9C4AE807AC6A57ECF89CAD97133E14865
```

Both replay byte streams are identical and independently verify to the reported final hash. The two JSON reports differ only in peer-local observations/metrics and both assert success, final confirmation, and both hashes.

## Investigation notes

`root-cause-udp-10054.md` records the Windows UDP reset investigation and the hidden native-build failure. Once the actual socket binary was rebuilt, 10054 disappeared. A second deterministic root cause was asymmetric two-message startup/final handshakes: one peer could exit its state before the other observed the corresponding packet. Startup now acknowledges the received hello; final completion uses a bounded 16-iteration drain while continuing to retransmit and receive. No watchdog extension or unbounded sleep was used.


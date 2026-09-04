# PACT-40 Replay, Report, Trace, Desync, and CLI Evidence

## Contract

- Objective: make confirmed inputs independently replayable and turn real runs into deterministic reports/traces plus confirmed-only desync evidence.
- Gap: packet-driven sessions existed only in tests and had no durable artifacts or CLI.
- Guardrail: timing is observational and excluded from identity; replay is strict and bounded; unconfirmed prediction differences never trigger canonical desync.
- Done when: replay round trip/rejection/verification, report order/identity, trace bounds, exact desync injection, packet-driven convergence, and CLI artifact loop pass.

## RED

```text
replay_report_test.cpp: cannot open 'rollback_lab/replay/replay.hpp'
cli_test.cpp: cannot open 'rollback_lab/cli/commands.hpp'
```

## GREEN and regression

```text
Compiler: MSVC 19.51.36248.0
Configuration: msvc-debug, warning-as-error
Feature commit: 4e4b1c39f0ea89f0d50bb69ed0b2af3f6190bf0a
CTest: 2/2 passed
Internal cases: 37 tests, 0 failures
Protocol fuzz smoke retained: 100,000 inputs
```

Fresh 240-frame CLI run:

```text
scenario seed: 12648430
transport seed: 5351397
sent/delivered/dropped/duplicated/reordered: 608/594/44/30/64
rollback count/resimulated/max depth: 189/1044/10
confirmed frame: 240
hash A/B: 0x4B35DC3FD8F6009C / 0x4B35DC3FD8F6009C
replay verification: true
desync result: none
identity: 0x9AE68C993226DD9A
```

Artifact SHA256 values from the fresh ignored work directory:

```text
report.json  6129CA3B510B845B9800E7D843EB5AB91A2028BAE23C5B18CFB81FDD420111D3
input.rlr    8C86C75B4EF84C1B02EFA9B31FA300D246B55D50604848D53DFDB76E80D13051
trace.json   048DA4B79A55B4182A18AD7750D3E503AE7B6DBA043A18261804A31325235575
```

Standalone replay rebuilt frame 240 to decimal hash `5419479893391311004`; compare matched the report identity; verify repeated the same seeded identity. Benchmark smoke ran 5,000 simulation ticks plus a 2,000-frame rollback stress and emitted schema-valid integer observations without an absolute performance gate.

The controlled `damage_bias` simulation variant produced a hash difference at boundary frame 1. With the remote observation marked unconfirmed the tracker emitted nothing; once both observations were confirmed it recorded frame 1, both hashes, recent inputs, bounded state summary, versions, and seed.


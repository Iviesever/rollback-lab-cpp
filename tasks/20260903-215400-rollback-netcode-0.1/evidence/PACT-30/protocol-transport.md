# PACT-30 Protocol and Seeded Transport Evidence

## Contract

- Objective: strict socket-independent packet bytes and deterministic bounded hostile-network scheduling.
- Gap: sessions had no packet representation or transport path.
- Guardrail: no raw struct casting, no `random_device`, <=1,200-byte packets, <=32 redundant inputs, typed decode offsets, bounded queues/age/bandwidth.
- Done when: codec/CRC/sequence edges, every truncation, corruption/random bytes/fuzz, and latency/jitter/loss/reorder/duplicate/burst/overflow/age/bandwidth tests pass repeatably.

## RED

Both production consumers failed at the intended boundary:

```text
protocol_transport_test.cpp: cannot open 'rollback_lab/protocol/codec.hpp'
protocol_fuzz_smoke.cpp: cannot open 'rollback_lab/protocol/codec.hpp'
```

## GREEN and regression

```text
Compiler: MSVC 19.51.36248.0
Configuration: msvc-debug, /W4 /WX /permissive- /EHsc
Feature commit: 3629bd12c3d33e9bac6e89c65a178e6f1d607d5f
CTest: 2/2 passed
Internal runner: 30 tests, 0 failures
Standalone fuzz smoke: 100000 bounded inputs, 0 crashes
```

Protocol evidence includes CRC-32/ISO-HDLC check vector `123456789 -> 0xCBF43926`, complete packet round trip, `RLBK` little-endian prefix, every byte truncation, magic/version/type/count/length/CRC failures, >32 input rejection, 64-sequence duplicate/out-of-order/stale/wrap behavior, 10,000 random inputs, and canonical re-encode on any successful decode.

Transport evidence includes repeat-equal deliveries and metrics for the same seed; base latency and jitter; 0%, 1%, 5%, and 20% seeded loss; reordering; duplication; burst loss; two-packet queue fail-closed; maximum-age drop; two-byte/tick bandwidth deferral; and complete drain without unbounded growth.

CRC is documented and tested only as accidental-corruption detection. FNV-1a remains a deterministic state identity, not a security primitive.


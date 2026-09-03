# Replay Format v1

Replay files contain confirmed inputs, not snapshots or predicted state. Verification reconstructs the canonical simulation from `make_initial_world()` and rejects any checkpoint or final-hash mismatch.

## Header

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `RLRP` (`0x50524C52` LE) |
| 4 | 2 | Replay version = 1 |
| 6 | 2 | Simulation version = 1 |
| 8 | 2 | Protocol version = 1 |
| 10 | 1 | Simulation variant |
| 11 | 1 | Reserved, must be zero |
| 12 | 8 | Scenario seed |
| 20 | 8 | Transport seed |
| 28 | 4 | Final boundary |
| 32 | 4 | Confirmed input-pair count |
| 36 | 4 | Checkpoint count |
| 40 | 8 | Expected final hash |

The fixed header is 48 bytes. Each input pair is 14 bytes: frame `u32`, A buttons `u8`, A sequence `u32`, B buttons `u8`, B sequence `u32`. Each checkpoint is boundary `u32` plus hash `u64`. A trailing CRC-32/ISO-HDLC covers every earlier byte.

## Bounds and validation

- Maximum input pairs: 1,000,000.
- Maximum checkpoints: 100,000.
- Input count must equal final boundary for the version-1 zero-based format.
- Input frames must be contiguous and player identities are implicit A/B.
- Checkpoints must be strictly increasing and no later than the final boundary.
- The computed total length must exactly equal the file length.
- Unsupported versions, flags, length, CRC, frame order, checkpoint hash, or final hash return typed errors.

## CLI

```powershell
build\msvc-debug\rollback_lab.exe replay samples\input.rlr
```

A successful command prints the final boundary and reconstructed hash and returns zero. Corruption, unsupported version, or mismatch returns nonzero.


# Binary Protocol v1

Packets are encoded field by field in little-endian order. C++ object layout is never placed on the wire. The maximum datagram is 1,200 bytes and the redundant input window is at most 32 records.

## Packet layout

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic bytes `RLBK` (`0x4B424C52` as LE integer) |
| 4 | 2 | Protocol version (`1`) |
| 6 | 1 | Packet type: hello=1, input=2, state-hash=3, goodbye=4 |
| 7 | 1 | Sender peer: A=0, B=1 |
| 8 | 4 | Sequence number |
| 12 | 4 | Ack / highest contiguous remote-input boundary observation |
| 16 | 8 | Scenario identity |
| 24 | 4 | Sender confirmed boundary |
| 28 | 1 | Input record count, 0–32 |
| 29 | 1 | Confirmed hash record count, 0–32 |
| 30 | 2 | Payload length |
| 32 | variable | Input records, confirmed hash records, then hello payload when type=hello |
| end−4 | 4 | CRC-32/ISO-HDLC over all preceding bytes |

Each input record is 10 bytes: frame `u32`, player `u8`, buttons `u8`, sequence `u32`. Each confirmed hash record is boundary `u32` plus hash `u64`. Hashes are strictly increasing and bounded to 32, letting receivers find the earliest overlap mismatch rather than seeing only the latest boundary.

A hello packet has no input/hash records and carries a strict 16-byte payload: simulation version `u32`, scenario/config digest `u64`, and target frame `u32`. Non-hello packets may not carry that payload.

## Decoder behavior

The decoder validates minimum/maximum size and CRC before constructing a packet. It then validates magic, supported version/type, peer ID, both counts, strictly ordered hashes, hello/type consistency, exact payload/total length, sender ownership of every input, known button bits, and complete reads. Failure returns a typed status with byte offset/context. Unknown versions and packet types fail closed.

Tests cover every packet type, every truncation boundary, bad magic/version/type/count/length/CRC, oversized input/hash windows, 10,000 random byte strings, and a 100,000-input fuzz smoke. The fuzz corpus contains 33,334 random packets, 33,333 structured packet mutations, 33,333 structured replay mutations, and 50,000 CRC rewrites that exercise fields beyond the integrity gate.

## Sequence receive window

A 64-bit receive mask tracks the newest sequence. Newer packets advance the window, unseen packets within 63 positions are accepted as out of order, seen positions are duplicates, and older positions are stale. Arithmetic handles `uint32` wrap.

Duplicate or stale packets are ignored at the peer boundary. An out-of-order packet may still supply a missing input. This distinction is why packet loss is not necessarily input loss: later packets resend up to 32 recent inputs.

## Integrity and security

CRC-32/ISO-HDLC detects accidental corruption only. It provides no authentication, secrecy, replay protection against an attacker, or anti-cheat guarantee. The project deliberately has no cryptographic or public-network threat model.

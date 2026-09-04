# Known Limitations

- This is an educational laboratory, not production-ready netcode.
- Only two peers, one arena, one projectile attack, fixed capacities, and a last-known-input prediction policy are implemented.
- The in-process transport uses integer percentages and logical ticks; it is a deterministic model, not a network simulator calibrated to a carrier or ISP.
- The real relay forwards immediately on localhost. It has no WAN service, NAT traversal, STUN/TURN, matchmaking, lobbies, persistence, accounts, encryption, authentication, congestion control, or anti-cheat.
- Port discovery reserves a random bind-0 port and releases it before child bind, leaving a small TOCTOU race; a conflict fails safely rather than silently selecting a different session.
- CRC-32 detects accidental corruption only. FNV-1a is not cryptographic.
- Sequence handling is bounded to 64 packet positions; input redundancy is bounded to 32 frames; rollback is bounded to 120 frames; history is 256 frames.
- Replay v1 assumes the default initial world and contiguous frames beginning at zero.
- The version-1 report writer is explicit rather than a general JSON parser; `compare` reads the generated identity field and rejects missing/different identities.
- Canonical hashing currently builds a byte vector, so a small hot-path allocation may occur. v0.1 does not claim an allocation-free tick.
- Benchmark timing is a local observation and has no cross-machine SLA.
- The viewer embeds bounded JSON without compression. The checked sample is small, but very long traces are sampled rather than storing every state forever.
- Browser QA covers the current in-app Chromium surface at desktop and narrow viewport; it is not an exhaustive browser/device matrix.
- A portable/local Clang or GCC and supported sanitizers are verified in the final matrix; unsupported platform combinations are reported rather than called passing.
- No Unity, Unreal, renderer, full physics engine, ECS, cloud backend, database, release tag, or formal GitHub Release is part of v0.1.

# PACT-85 Core/SDK checkpoint

Recovery baseline: clean P0 commit `1c2ed8eaa792e093a7c81b2088e527d3593dbe7c`.
PACT85 entry met the time/quota gate; see PACT-85-plan.md and the immutable P0
receipt. This checkpoint does not claim the two packaged UE clients are complete.

The existing production UDP peer now has a caller-driven PeerDriver. Legacy CLI
run_peer drives the same implementation; no second simulation or transport is
introduced. The SDK borrows one unused Session, polls at most64 datagrams and
advances at most one scripted frame per call. It adds seven C exports and one
capability bit without changing existing record layouts or exports.

The engine profile is explicitly domain separated and binds ABI plus the other
scenario parameters in the existing config digest. Protocol and Replay remain v1;
the legacy CLI digest is unchanged. ABI is not a separately readable wire field.
Normal/advertised version fields and precise limits are documented in the header.

Actual RED: all six new tests failed against the scaffold. Actual GREEN:

- MSVC Debug: full7/7 CTest,60.63s.
- MSVC Release: full7/7,23.26s.
- ASan+UBSan: full7/7,45.31s.
- UDP cases repeated five consecutive times, all passed.
- A paced60Hz DLL run with an actual Relay confirmed240 frames, observed106
  Corrections, produced equal Replay bytes and hash0x522270F668B4A27C, and reaped
  the Relay with exit0. That probe used scenario seed1; this is not the default
  P0 sample seed/hash.

The new cases cover C11 calls, version/size/null/thread/borrow/buffer boundaries,
real loopback traffic, identity/profile mismatches, missing peer, watchdog,
confirmed desync, canonical replay disagreement, invalid future input and
terminal-state behavior. Existing61 Core/CLI/UDP cases still pass. New test
launcher names are platform conditional; the final test-only portability fix
was followed by another successful focused run.

Initial sanitizer execution failed with0xc0000139 because the ignored evidence
command omitted the matching Clang22 runtime directory from PATH. The corrected
documented runtime environment passed the full suite; no timeout was widened.
Both the failure and corrected logs remain in artifacts/pact85-core.

Source hashes and commands: artifacts/pact85-core/handoff.json. Paced evidence:
artifacts/pact85-core/paced-dll/verification.json. No owned rollback_lab process
remained after verification. UE adaptation is still an ignored patch draft until
this tested SDK can be rebuilt from a clean commit, preserving staging checks.

Real UDP scheduling can concentrate corrections in either peer. The two-client
supervisor must require aggregate prediction/correction and an actual correction
capture, not incorrectly require a correction on each endpoint individually.

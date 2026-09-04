# PACT-85 bounded UDP-UE plan

Entry checkpoint: clean `1c2ed8eaa792e093a7c81b2088e527d3593dbe7c` passed the
complete P0 functional acceptance, including same-source SDK/Plugin/Shipping
artifacts, five compiler configurations, full repeated10000-seed sweep,
Automation, real controls, smoke, failures and three-way parity. The immutable
receipt is `artifacts/ue5-0.2/p0-clean-1c2e-checkpoint.json`.
At entry on September4 around14:18 UTC+8, quota remaining was35%, above22% and
before the September5 04:00 cutoff. No reset was consumed.

## Contract decision

Keep Protocol v1 and the legacy CLI handshake unchanged. The new SDK UDP driver
uses a named, domain-separated engine handshake profile whose configuration
digest includes ABI version, protocol/simulation versions, scenario/transport
seeds and target frame. Peers compare the complete profile before input exchange.
This validates ABI agreement as part of the aggregate configuration contract;
it does not add an independently readable remote ABI field, and a mismatch is
reported as a profile/configuration mismatch. Do not repurpose ack/confirmed
fields or silently append bytes to the strict16-byte v1 Hello.

The narrow driver is limited to1..240 scripted frames so existing confirmed
input history can build a complete Replay v1. One SDK Session belongs to each UE
client; remote inputs enter only through the existing UDP decoder. The existing
relay remains a third process and forwards versioned datagrams without a world.
No UE Replication, second simulator, transport implementation in UE, worker
thread, detached process or long-running input-log feature is added.

## Implementation boundaries

1. Core/SDK agent: extract the existing blocking UDP peer into a bounded
   incremental driver, preserving CLI behavior through the same driver; add
   opaque borrowed-session C ABI operations and meaningful RED/negative tests.
2. Parent: adapt the UE Runtime and native scene to one peer in network mode,
   retaining the existing two-peer P0 modes. Own the two-client+relay supervisor,
   packaged evidence, input/frame/hash/replay checks and all UE tool pipelines.
3. Both: preserve the clean P0 checkpoint as the recovery point. The SDK may
   receive a tested checkpoint commit after its focused and relevant full Core
   regressions, because UE staging intentionally requires a clean SDK manifest.
   Until that commit/build is complete, parent UE edits stay as an ignored patch
   draft, not a second checkout. PACT85 is not complete until real three-process
   packaged success plus negatives pass. If the extension cannot close within
   the quota/time envelope, remove its unfinished production changes and keep
   this investigation as deferred scope.

## Acceptance tests before implementation

- Actual C11 calls: null/version/size/thread/borrow/buffer lifetime boundaries.
- Pump two independent SDK drivers over real loopback datagrams and existing
  relay logic; observe prediction, actual correction and convergence/replay.
- Missing peer, protocol/simulation/ABI-profile mismatch, watchdog, controlled
  confirmed desync, invalid target-frame input and terminal-state non-mutation.
- Preserve original real CLI UDP tests and repeat completion/teardown cases.
- Packaged relay+UE A+UE B: dynamic ports, source/profile/peer identities,
  one Session per client, equal confirmed hashes and reconstructed replays,
  actual rollback, failure traces and no remaining owned process.
- Repeat relevant full Core and UE regression, then regenerate all final
  artifacts from one clean exact source after any completed extension.

Quota controls remain unchanged: stop extending near18%; no new feature work
at15%; only fixes/verification/packaging/docs afterward.

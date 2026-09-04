# PACT-85 real UDP UE checkpoint

The optional phase began only after the complete clean P0 checkpoint at1c2ed8e,
with35% remaining quota and before the specified time cutoff. The tested SDK
checkpoint is f12e9acd6307a36732aa048688f05c5a14d4e611. Artifacts below are
explicitly working-source evidence; final delivery rebuilds one clean commit.

## UE RED/GREEN and ownership

The real UE RED run152812 passed the existing28 tests and failed all4 new UDP
tests at the explicit scaffold. Full implementation passed32/32 in run152940.
The new mode uses one borrowed SDK Session and one UDP driver in each process;
the inactive peer slot has no handle and is not rendered. Existing P0 pair modes,
DLL leases, pre-exit/StopAll, fixed-step bounds and cleanup remain shared.

Normal mode uses local scripted input inside the existing Core PeerDriver.
Only decoded datagrams supply remote input. The UE module owns no simulator,
codec or transport policy. The existing relay forwards datagrams and has no World.
Each UE publishes its actual dynamically bound port before its first step.
The supervisor holds only a relay-port reservation until both peers are ready,
then starts the existing relay with those exact live peer ports. Peer sockets
are never released/reclaimed; a relay-port handoff collision fails closed.

## Actual positive runs

- run155457: UE PIDs49260/50520, Relay24804, ports53863/53864/53862;
  confirmed240/240, hash0x4B35DC3FD8F6009C,153 aggregate rollbacks,
  equal replay bytes, both canonical CLI replay checks passed, all targets0.
- Image inspection found cropped single worlds. Stock automatic orthographic
  plane adjustment recenters the camera onto the view-target Actor at the origin.
  UDP mode now retains its offset using explicit near0/far2000 planes; P0 camera
  settings are unchanged. Source evidence: UCameraComponent assigns the owner's
  location as CameraToViewTarget and SceneView repositions ViewOrigin accordingly.
- run160929: both single worlds centered correctly; confirmed240/hashunchanged,
 141 aggregate rollbacks. A happened to need no correction while B had141;
  the verifier correctly requires aggregate prediction/correction and an actual
  correction image only where a correction occurred. All five produced PNGs
  were opened and checked, and all three target processes exited0.

The UDP replay digest in the first run is
653da9dc0e64a4bc912bf355af01a9913a1f690b2e14b27e6ec8b4084a45c9ca.
It differs from the seeded P0 replay because its transport seed metadata differs;
the two UDP clients' replay bytes agree. Actual UDP report counters/identity can
vary with scheduling and are not incorrectly asserted equal between peers.

## Actual negative runs

Each run returned nonzero, had its expected reason checked, and reaped all owned
processes. Raw directories below are under artifacts/ue5-0.2/runs/20260904-.

| Case | Run suffix | Observation |
| --- | --- | --- |
| Missing Peer | 161541-198-udp-ue-missingpeer | No B process; A handshake timeout |
| Protocol mismatch | 161600-042-udp-ue-protocolmismatch | Version rejection before convergence |
| Simulation mismatch | 161619-316-udp-ue-simulationmismatch | Simulation contract rejection |
| ABI profile mismatch | 161638-258-udp-ue-abimismatch | Aggregate engine profile mismatch |
| Controlled desync | 162158-556-udp-ue-desync | A confirmed/earliest divergence91; desync image inspected; B later timed out after A exited |
| Watchdog | 163434-420-udp-ue-watchdog | Actual5s outer timeout, failure summary retained, all children exited |

## Confirmed fixes

Read-only review found no Blocker/High but identified a narrow exception diagnostic
inconsistency: the C API retained an internal failure while native phase/context
still appeared healthy. A real one-shot bad_alloc reproduced it. PeerDriver::step
now publishes an allocation-free FAILED/internal-failure transition; retry does
not alter frame, hash, tick or JSON. Test injection exists only in its separate
test executable. The ignored overlay passed Release/ASan; integrated Release
passed all8 CTest entries after applying the fix.

The first actual Watchdog run161656 killed all6 job processes, but a transient
sharing violation on a redirected log aborted the only summary write. The
supervisor now writes a provisional failure summary first, then waits only on
actual sharing/lock violations32/33 within the remaining existing10-second
cleanup allowance. Unavailable files have a null hash and explicit status.
Normal success still requires a complete hash inventory; Watchdog is explicitly
diagnostic-partial. No timeout or arbitrary sleep was increased. Three real
multi-helper watchdog tests and deterministic locked-file tests passed, then the
actual UE Watchdog above passed. Helper suite:38/38.

## Regression and remaining delivery gates

Shipping BuildCookRun and three-target BuildPlugin passed with the extension.
The P0 paired Smoke and full CLI/C11/UE report/trace/replay verifier still passed
using the extended SDK. Core counts are61 original Core/CLI/UDP,7 C-session,
4 C-live and6 C-UDP behavior cases, plus C11 consumers and the isolated exception
test. The final matrix has8 CTest entries; UE has32 Automation tests.

Final clean SDK/Plugin/Demo, all five Core configurations, repeated10000 seeds,
both packaged modes/negative sets, refreshed independent full audit, documentation
calibration, push and Draft PR remain separate delivery gates. PACT85 functional
closure does not imply WAN readiness, authentication, NAT traversal or a
production networking system. ABI is bound in the named configuration digest,
not exposed as an independent wire field.

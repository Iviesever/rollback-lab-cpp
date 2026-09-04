# Live change drills

Complete at least one exercise personally before presenting the project. First explain the current contract and predict the result. For behavior changes, write a focused failing test, make the smallest fix, run the relevant regression, and record commands/results. Do not present these suggested exercises as work already completed by the user.

The following fourteen exercises directly involve UE or C ABI. Use a practice branch and disposable generated artifact copies. Intentional contract changes must be labeled; do not silently update golden identity or weaken final acceptance.

1. **C11 structure initialization.** Write a small C consumer that queries version and creates/destroys a session. Deliberately omit `api_version`, then use the wrong `struct_size`. Show typed failure and unchanged output before fixing initialization. Build it through the installed `find_package` package, not an include path into the source tree.
2. **ABI evolution.** Design a new copied observation field. Explain why ABI v1's exact-size rule forbids silently appending it, propose version/capability behavior, and add old/new-size rejection tests in an isolated exercise. Identify which package/loader checks must change before implementing a versioned extension.
3. **Handle and borrow ownership.** Add a focused consumer case that tries to destroy a session while a live driver borrows it. Assert `RL_BORROWED`, destroy the driver first, then release both sessions. Show zero live resources after repeated runs and explain why freeing the opaque pointer directly is invalid.
4. **Thread affinity.** Query a live session from a worker thread, assert `RL_WRONG_THREAD`, then query/destroy it on the creating thread. Explain why this is not a promise of general thread safety and why adding a mutex alone would not change the public ownership contract.
5. **Two-stage buffers.** Export report JSON and replay from C11 using a sizing call, one byte too little, then exact capacity. Prove the insufficient buffer is unchanged. Explain NUL-inclusive JSON sizes versus binary replay byte counts and impose a consumer allocation limit.
6. **Loader integrity failure.** Alter the DLL or manifest in a disposable copied SDK and reproduce the exact loader rejection. Restore it by staging the verified SDK. Show that no session is created on failure and distinguish checksum integrity from cryptographic authenticity.
7. **CRT and linkage inspection.** Inspect the DLL dependencies and import library, and compare them with the static Core archive. Explain Release `/MD`, the rejected debug-CRT UE target and why SDK creation/destruction must stay together. Do not bypass the runtime guard to make the exercise pass.
8. **Fixed-step scheduling.** Add an Automation scenario with zero-step render callbacks, a long delta, pause and one manual step. Assert no more than eight steps per callback, a bounded remainder, no accumulated paused debt and no skipped canonical frame. Compare canonical hash after the same ordered logical steps under different render-delta sequences.
9. **Short-tap input retention.** Reproduce a movement or attack tap that begins and ends before a simulation step. Use actual PlayerController/PIE input, verify the pending bit survives zero-step callbacks and is consumed at the next step, and verify reset clears it. Do not replace this with a model-only test.
10. **Presentation-only change.** Change an arena color, camera scale or ghost duration. Run the fixed scripted scenario and prove the report identity, replay bytes and confirmed hashes are unchanged. Point to the exact code that projects integer state into transforms and show that no reverse write exists.
11. **Correction event display.** Add a bounded HUD indicator for actual correction depth, including the no-correction case. Drive it from SDK correction revision/result, not a timer or random flash. Use a trace to verify an observed event and keep speculative mismatch distinct from confirmed desync.
12. **Restart and DLL lease.** Extend a real PIE lifecycle test or simultaneous-runtime case. Start two runtimes, stop one, continue the other, then exit. Verify driver/session/library counts return to baseline and explain why a borrowed OS module handle cannot be unconditionally freed as an owned lease.
13. **Network preset and parity.** Define one additional bounded preset using existing transport configuration. Verify identical CLI/C/UE scripted settings produce the same report and replay, with at least one real rollback and eventual confirmation. Explain how changing the preset affects identity while changing presentation does not.
14. **Packaged failure diagnosis.** On a disposable package copy, remove a staged DLL, corrupt a screenshot, or alter a nested report field. Show that the appropriate launch/verifier check fails with useful evidence and clean child teardown. Restore the exact package and rerun ordinary startup plus smoke. Explain why an Editor pass could not establish this deployment result.

## Core and networking practice

These optional extensions retain the original laboratory exercises:

- Change integer movement speed or attack cooldown, add the exact behavior test, and update golden identity only as an intentional simulation-contract change.
- Predict neutral after three repeated frames and show the expected change in rollback count/depth without weakening convergence.
- Add a central integer AABB obstacle with stable resolution order and bounded collision work.
- Start a session near uint32 frame wrap and cross zero without signed-overflow ordering assumptions.
- Add a versioned packet field with every truncation boundary covered; explain the protocol/replay version consequences separately.
- Compare redundancy windows of 1 and 32 under a bounded 20% loss scenario and explain bandwidth versus recoverability.
- Exercise `drop_oldest` and expose the exact queue counter, or mutate replay sections and assert typed errors and offsets.
- Inject movement divergence, then prove its earliest confirmed boundary and compare it with an ordinary prediction correction.
- Add a viewer lane using only production trace JSON; no invented events or unbounded embedded data.
- Tighten a CLI UDP handshake deadline in a deterministic negative test, retain bounded shutdown and verify all child processes exit.

For an interview, narrate the changed contract, the failing test, why it failed, the smallest fix, the regression scope and any intended identity change. Finish by explaining teardown and the limits of the evidence. Do not claim to understand the project solely because the unmodified suite passes.

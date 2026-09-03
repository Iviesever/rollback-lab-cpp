# Live Change Drills

Complete at least one drill personally before presenting the project. Run focused and full regression after every change.

1. **Movement tuning** — change player speed from 4 to 5 world units/tick and update the golden trace intentionally.
2. **Prediction policy** — predict neutral after three repeated frames; add a case proving the exact rollback change.
3. **Arena boundary** — add a central rectangular obstacle using integer AABB collision and stable resolution order.
4. **Cooldown rule** — make attack cooldown 12 ticks and prove projectile capacity remains bounded.
5. **Frame wrap** — add a session test starting near `0xFFFFFFFF` and crossing zero.
6. **Protocol field** — add a one-byte simulation flags field with a version bump and truncation coverage.
7. **Input redundancy** — make the window configurable from 1–32 and compare 20% packet-loss outcomes.
8. **Queue policy** — exercise `drop_oldest` and expose its exact counter in report JSON.
9. **Replay corruption** — mutate each replay section and assert the typed error plus offset.
10. **Desync investigation** — inject movement rather than damage divergence and prove the earliest boundary.
11. **Viewer lane** — add a duplicate-packet lane without reading any data outside trace JSON.
12. **UDP timeout** — shorten handshake timeout and replace any flaky duration assumption with a state condition.
13. **Snapshot compression sketch** — measure canonical snapshot bytes and propose a bounded delta format without implementing premature complexity.
14. **Engine adapter design** — write a thin Unreal-facing interface and explain why it cannot expose UObject pointers to the core.

For interview practice, narrate: the changed contract, the RED test, why it failed, the smallest GREEN, the regression cone, and any updated deterministic identity.


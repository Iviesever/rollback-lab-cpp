# AI assistance

This is an AI-assisted engineering project. Its disclosure is part of the product's acceptance criteria.

The **user** specified the career goal, product direction, deadline and freeze policy, scope boundaries, safety permissions, non-goals and detailed acceptance criteria. The user did **not** hand-write the delivered code in this work session.

**Codex GPT-5.6 Sol** refined the architecture and produced the implementation and associated engineering work: deterministic Core and protocol integration, the C ABI and SDK, UE Runtime plugin, native Arena demo, tests, debugging, packaging scripts and artifacts, visual inspection, viewer, documentation and evidence. The 0.2 work builds on the already AI-assisted 0.1 Core, CLI, replay/report/desync and real localhost UDP/process path.

The project must not be described as independently hand-written by the user. AI involvement must not be hidden in a résumé, portfolio, interview or repository history. The user can honestly say they defined the product constraints and acceptance criteria for an AI-assisted project. Claims of personal implementation, debugging or understanding must identify work the user actually performed; suggested exercises are not completed experience.

Before presenting the project in an interview, the user must personally understand:

- C ABI version/size rules, opaque handles, DLL/import/static library roles, CRT and allocator ownership;
- fixed-step accumulation, bounded catch-up, pause/step/reset and why wall time stays outside canonical state;
- presentation ownership, real correction events and why Actors never become canonical authority;
- input prediction, pre-input snapshots, earliest-dirty restore/resimulation, confirmed frames, packets and replay/desync verification.

The user must also complete at least one [live change drill](LIVE_CHANGE_DRILLS.md) personally, explain its failing test and fix, and run the relevant regression. The [interview guide](INTERVIEW_GUIDE.md) is a study aid, not a script that establishes understanding.

AI authorship does not authenticate verification claims. Exact commands, source identity, checked tests, raw logs, artifacts, manifests, hashes, visual checks, independent review and the Draft PR evidence are the basis for review. A model's summary is not a substitute for a successful final packaged verifier result or a known limitation.

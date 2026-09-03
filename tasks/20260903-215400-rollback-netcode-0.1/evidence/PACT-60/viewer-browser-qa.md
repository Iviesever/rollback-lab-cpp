# PACT-60 Viewer and Browser QA Evidence

## Contract

- Objective: generate a no-CDN, no-Node-runtime interactive timeline from production trace data and publish reproducible portfolio artifacts/documentation.
- Gap: trace JSON existed but had no visual inspector or checked samples.
- Guardrail: no hard-coded demo state, no external resource fetch, no animation on repeated keyboard stepping, bounded trace/HTML size, responsive and reduced-motion behavior.
- Done when: generation tests, CLI artifact loop, real browser interaction, console, responsive layout, markers, samples, screenshots, and docs all pass.

## RED and GREEN

```text
RED 1: viewer_generation_test.cpp could not include rollback_lab/report/viewer.hpp
RED 2: CLI contract failed because viewer.html did not exist
RED 3: CLI contract failed because desync-demo was an unknown command
GREEN feature commit: 42842defffe78ff980bdf43b57b330a9dca94fd5
MSVC workflow: configure + build + 2/2 CTest passed
Internal behavior cases: 43/43
Sample replay: frame 240, hash 0x4B35DC3FD8F6009C
```

## Browser QA

Source: `viewer/sample-viewer.html`, generated from `viewer/sample-trace.json` by the production C++ generator.

Desktop viewport 1440×900:

```text
dashboard columns: 999.438px / 371.219px
dashboard width: 1384.667px
canvas: 998.104×561.427px
scrubber frame: 120
confirmed through: 118
mode: Predicted
step forward/back: 120 -> 121 -> 120
play for 180ms: 120 -> 133
rollback markers: 189 (matches trace)
packet markers: 1340 (matches trace event array)
console errors: 0
```

Narrow viewport 390×844:

```text
dashboard columns: 358.667px (single column)
document client/scroll width: 375/375 (no horizontal overflow)
scrubber frame: 200
controls visible: true
timeline visible: true
rollback markers: 189
packet markers: 1340
console errors: 0
```

Screenshots:

- `viewer/screenshot.png` — SHA256 `2A936DB3FF72B055DFF46F8B7FEA710DE7472C4B42061FF670FC68349E5683F8`
- `viewer/screenshot-mobile.png` — SHA256 `E12E5FE759B0CD5F3424948D4B886B1424BB1ED1CC9E619B80DAD12D2082BDF4`

## Artifact checksums

```text
samples/report.json             A1F7968E2D75B83DEFDA4767EA995B0A689028891360A24BA222B51B22BA0592
samples/input.rlr               8C86C75B4EF84C1B02EFA9B31FA300D246B55D50604848D53DFDB76E80D13051
samples/trace.json              048DA4B79A55B4182A18AD7750D3E503AE7B6DBA043A18261804A31325235575
samples/viewer.html             99F4AFBDF7CBE7512A289A901E7ECE7BFC7265E1DE302EF9995790CE131A0A90
samples/desync-diagnostic.json  4B39CC78B5B8ACFA3AE4F79E6F90DE25DD2CD7E8EC311692D398076D6323E47B
```

The sample report records generation SHA `42842defffe78ff980bdf43b57b330a9dca94fd5`. Placeholder scan, viewer external-dependency scan, and all 15 Markdown files' relative-link validation passed.

## Design influence

The design-engineering guidance kept repeated controls immediate, limited button feedback to transform/color properties at 140ms, gated hover to fine pointers, included reduced-motion behavior, and used no decorative blocking animation. Information hierarchy prioritizes frame/confirmation/rollback identity over ornament.


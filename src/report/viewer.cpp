#include <rollback_lab/report/viewer.hpp>

#include <rollback_lab/report/canonical_json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace rollback_lab {

auto generate_viewer_html(const Trace& trace) -> Result<std::string> {
    if (trace.frames.empty()) {
        return Result<std::string>::failure(
            Error{ErrorCode::invalid_argument, 0U, 0U, "viewer_empty_trace"});
    }
    const auto json = canonical_json(trace);
    if (!json.ok()) {
        return Result<std::string>::failure(json.error());
    }

    static constexpr auto prefix = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Rollback Timeline — Rollback Lab C++</title>
<style>
:root {
  color-scheme: dark;
  --bg: #080a0f;
  --surface: #10141c;
  --surface-raised: #171c26;
  --line: #293140;
  --line-bright: #3d485b;
  --text: #f4f6fa;
  --muted: #8e99aa;
  --cyan: #58d8ff;
  --violet: #9f8cff;
  --amber: #ffbe5c;
  --red: #ff6874;
  --green: #62dfa0;
  --ease-out: cubic-bezier(0.23, 1, 0.32, 1);
  font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont,
               "Segoe UI", sans-serif;
}
* { box-sizing: border-box; }
body {
  margin: 0;
  min-width: 320px;
  min-height: 100vh;
  color: var(--text);
  background:
    radial-gradient(circle at 16% -10%, rgba(88,216,255,.11), transparent 34rem),
    radial-gradient(circle at 90% 0%, rgba(159,140,255,.10), transparent 30rem),
    var(--bg);
}
button, input { font: inherit; }
.shell { width: min(1480px, calc(100% - 40px)); margin: 0 auto; padding: 34px 0 48px; }
.masthead {
  display: flex; align-items: flex-end; justify-content: space-between; gap: 24px;
  margin-bottom: 22px;
}
.eyebrow {
  margin: 0 0 8px; color: var(--cyan); font: 700 11px/1.2 ui-monospace, monospace;
  letter-spacing: .17em; text-transform: uppercase;
}
h1 { margin: 0; font-size: clamp(27px, 4vw, 50px); line-height: .98; letter-spacing: -.045em; }
.subtitle { max-width: 670px; margin: 13px 0 0; color: var(--muted); line-height: 1.55; }
.live-status {
  display: inline-flex; align-items: center; gap: 9px; flex: 0 0 auto;
  padding: 9px 12px; border: 1px solid var(--line); border-radius: 999px;
  color: var(--muted); background: rgba(16,20,28,.82); font: 700 11px/1 ui-monospace, monospace;
  letter-spacing: .08em; text-transform: uppercase;
}
.status-dot { width: 7px; height: 7px; border-radius: 50%; background: var(--green); box-shadow: 0 0 14px var(--green); }
.dashboard { display: grid; grid-template-columns: minmax(0, 1.75fr) minmax(270px, .65fr); gap: 14px; }
.panel {
  border: 1px solid var(--line); border-radius: 16px; background: rgba(16,20,28,.88);
  box-shadow: 0 22px 70px rgba(0,0,0,.28); overflow: hidden;
}
.panel-head {
  display: flex; align-items: center; justify-content: space-between; gap: 14px;
  padding: 13px 16px; border-bottom: 1px solid var(--line);
  background: rgba(23,28,38,.72);
}
.panel-title { margin: 0; font-size: 12px; letter-spacing: .1em; text-transform: uppercase; }
.mono { font-family: "SFMono-Regular", Consolas, "Liberation Mono", monospace; }
.frame-chip { color: var(--muted); font-size: 12px; }
.arena-wrap { position: relative; aspect-ratio: 16 / 9; min-height: 310px; background: #0a0d13; }
#arena { display: block; width: 100%; height: 100%; }
.arena-legend {
  position: absolute; left: 14px; bottom: 14px; display: flex; flex-wrap: wrap; gap: 8px;
  pointer-events: none;
}
.legend-item {
  display: inline-flex; align-items: center; gap: 7px; padding: 6px 9px;
  border: 1px solid rgba(255,255,255,.10); border-radius: 999px;
  background: rgba(7,9,13,.78); backdrop-filter: blur(8px); color: #c8cfda; font-size: 11px;
}
.swatch { width: 7px; height: 7px; border-radius: 50%; }
.swatch.a { background: var(--cyan); } .swatch.b { background: var(--violet); }
.swatch.p { background: var(--amber); }
.sidebar { display: grid; align-content: start; gap: 14px; }
.metric-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); }
.metric { min-height: 84px; padding: 15px; border-right: 1px solid var(--line); border-bottom: 1px solid var(--line); }
.metric:nth-child(2n) { border-right: 0; }
.metric:nth-last-child(-n+2) { border-bottom: 0; }
.metric-label { color: var(--muted); font-size: 10px; letter-spacing: .09em; text-transform: uppercase; }
.metric-value { margin-top: 9px; font: 650 21px/1.1 ui-monospace, monospace; letter-spacing: -.035em; }
.metric-value.cyan { color: var(--cyan); } .metric-value.amber { color: var(--amber); }
.players { display: grid; grid-template-columns: 1fr 1fr; }
.player { padding: 16px; }
.player + .player { border-left: 1px solid var(--line); }
.player-name { display: flex; align-items: center; gap: 8px; margin-bottom: 15px; font-size: 13px; font-weight: 700; }
.stat-row { display: flex; justify-content: space-between; gap: 12px; margin-top: 8px; color: var(--muted); font-size: 12px; }
.stat-row strong { color: var(--text); font-family: ui-monospace, monospace; font-weight: 600; }
.hash-box { padding: 14px 16px; color: var(--muted); font-size: 11px; overflow-wrap: anywhere; }
.hash-box strong { display: block; margin-top: 6px; color: var(--text); font-size: 12px; }
.timeline-panel { grid-column: 1 / -1; margin-top: 14px; }
.transport-summary { display: flex; flex-wrap: wrap; gap: 13px; color: var(--muted); font-size: 11px; }
.transport-summary strong { color: var(--text); }
.controls { display: flex; align-items: center; gap: 8px; }
.control {
  min-width: 38px; height: 34px; padding: 0 12px; border: 1px solid var(--line-bright);
  border-radius: 9px; color: var(--text); background: #1b2230; cursor: pointer;
  transition: transform 140ms var(--ease-out), border-color 140ms ease, background-color 140ms ease;
}
.control:active { transform: scale(.97); }
.control:focus-visible, #timeline:focus-visible { outline: 2px solid var(--cyan); outline-offset: 2px; }
.control.primary { min-width: 78px; color: #071017; border-color: var(--cyan); background: var(--cyan); font-weight: 800; }
.timeline-body { padding: 18px 18px 20px; }
.scrubber-row { display: grid; grid-template-columns: 70px minmax(0,1fr) 70px; align-items: center; gap: 12px; }
.tick-label { color: var(--muted); font: 11px/1 ui-monospace, monospace; }
.tick-label:last-child { text-align: right; }
#timeline { width: 100%; accent-color: var(--cyan); cursor: ew-resize; }
.lanes { display: grid; gap: 8px; margin-top: 14px; }
.lane-row { display: grid; grid-template-columns: 84px minmax(0,1fr); align-items: center; gap: 12px; }
.lane-label { color: var(--muted); font: 10px/1 ui-monospace, monospace; letter-spacing: .08em; text-transform: uppercase; }
.lane { position: relative; height: 24px; border: 1px solid var(--line); border-radius: 6px; background: #0b0e14; overflow: hidden; }
.lane::after { content: ""; position: absolute; inset: 50% 0 auto; border-top: 1px solid rgba(255,255,255,.055); }
.marker { position: absolute; top: 5px; width: 2px; height: 12px; border-radius: 2px; z-index: 1; }
.marker.delivered { background: var(--green); } .marker.sent { background: #667085; }
.marker.dropped { width: 4px; background: var(--red); }
.marker.reordered, .marker.duplicated { background: var(--amber); }
.rollback-span { position: absolute; top: 5px; height: 12px; min-width: 4px; border-radius: 3px; background: rgba(255,104,116,.52); border: 1px solid var(--red); z-index: 1; }
.playhead { position: absolute; inset: 0 auto 0 0; width: 1px; background: var(--cyan); box-shadow: 0 0 9px var(--cyan); z-index: 3; pointer-events: none; }
.event-readout { display: grid; grid-template-columns: repeat(4, minmax(0,1fr)); gap: 8px; margin-top: 16px; }
.event-pill { padding: 10px 11px; border: 1px solid var(--line); border-radius: 9px; background: #0d1118; }
.event-pill span { display: block; color: var(--muted); font-size: 9px; letter-spacing: .08em; text-transform: uppercase; }
.event-pill strong { display: block; margin-top: 5px; font: 12px/1.2 ui-monospace, monospace; }
.footer { display: flex; justify-content: space-between; gap: 20px; margin-top: 16px; color: #727e90; font-size: 11px; }
@media (hover: hover) and (pointer: fine) {
  .control:hover { border-color: #66748a; background: #212a3a; }
  .control.primary:hover { border-color: #8ce7ff; background: #8ce7ff; }
}
@media (max-width: 900px) {
  .shell { width: min(100% - 24px, 760px); padding-top: 24px; }
  .masthead { align-items: flex-start; flex-direction: column; }
  .dashboard { grid-template-columns: 1fr; }
  .timeline-panel { grid-column: 1; }
  .sidebar { grid-template-columns: 1fr 1fr; }
  .sidebar .panel:first-child { grid-column: 1 / -1; }
  .event-readout { grid-template-columns: repeat(2, minmax(0,1fr)); }
}
@media (max-width: 560px) {
  .shell { width: calc(100% - 16px); padding-top: 18px; }
  .live-status { display: none; }
  .arena-wrap { min-height: 220px; }
  .sidebar { grid-template-columns: 1fr; }
  .sidebar .panel:first-child { grid-column: 1; }
  .panel-head { align-items: flex-start; flex-direction: column; }
  .controls { width: 100%; }
  .control { flex: 1; }
  .scrubber-row { grid-template-columns: 48px minmax(0,1fr) 48px; gap: 6px; }
  .lane-row { grid-template-columns: 64px minmax(0,1fr); gap: 8px; }
  .footer { flex-direction: column; }
}
@media (prefers-reduced-motion: reduce) {
  .control { transition: color 100ms ease, background-color 100ms ease; }
  .control:active { transform: none; }
}
</style>
</head>
<body>
<main class="shell">
  <header class="masthead">
    <div>
      <p class="eyebrow">Rollback Lab / Trace Inspector</p>
      <h1>Rollback Timeline</h1>
      <p class="subtitle">Inspect predicted play, packet disruption, rollback correction, and confirmed-state convergence from one production trace.</p>
    </div>
    <div class="live-status"><span class="status-dot"></span><span id="run-status">Trace loaded</span></div>
  </header>

  <section class="dashboard">
    <article class="panel">
      <div class="panel-head">
        <h2 class="panel-title">Canonical Arena</h2>
        <div class="frame-chip mono">Logical frame <strong id="logical-frame">0</strong></div>
      </div>
      <div class="arena-wrap">
        <canvas id="arena" width="1024" height="576" aria-label="Two-player rollback arena"></canvas>
        <div class="arena-legend" aria-hidden="true">
          <span class="legend-item"><span class="swatch a"></span>Peer A</span>
          <span class="legend-item"><span class="swatch b"></span>Peer B</span>
          <span class="legend-item"><span class="swatch p"></span>Projectile</span>
        </div>
      </div>
    </article>

    <aside class="sidebar">
      <section class="panel">
        <div class="panel-head"><h2 class="panel-title">Netcode state</h2></div>
        <div class="metric-grid">
          <div class="metric"><div class="metric-label">Frame mode</div><div id="frame-mode" class="metric-value cyan">Predicted</div></div>
          <div class="metric"><div class="metric-label">Confirmed through</div><div id="confirmed-frame" class="metric-value">0</div></div>
          <div class="metric"><div class="metric-label">Rollbacks</div><div id="rollback-count" class="metric-value amber">0</div></div>
          <div class="metric"><div class="metric-label">Resimulated</div><div id="resimulated-frames" class="metric-value">0</div></div>
        </div>
      </section>
      <section class="panel">
        <div class="panel-head"><h2 class="panel-title">Players</h2></div>
        <div class="players">
          <div class="player"><div class="player-name"><span class="swatch a"></span>Player A</div><div class="stat-row"><span>HP</span><strong id="a-hp">100</strong></div><div class="stat-row"><span>Score</span><strong id="a-score">0</strong></div><div class="stat-row"><span>Position</span><strong id="a-position">0, 0</strong></div></div>
          <div class="player"><div class="player-name"><span class="swatch b"></span>Player B</div><div class="stat-row"><span>HP</span><strong id="b-hp">100</strong></div><div class="stat-row"><span>Score</span><strong id="b-score">0</strong></div><div class="stat-row"><span>Position</span><strong id="b-position">0, 0</strong></div></div>
        </div>
      </section>
      <section class="panel">
        <div class="panel-head"><h2 class="panel-title">State identity</h2></div>
        <div class="hash-box">Canonical FNV-1a 64-bit<strong id="state-hash" class="mono">—</strong></div>
      </section>
    </aside>

    <article class="panel timeline-panel">
      <div class="panel-head">
        <div>
          <h2 class="panel-title">Network and rollback timeline</h2>
          <div class="transport-summary"><span>Packets <strong id="packet-total">0</strong></span><span>Dropped <strong id="packet-dropped">0</strong></span><span>Reordered <strong id="packet-reordered">0</strong></span><span>Max depth <strong id="maximum-depth">0</strong></span></div>
        </div>
        <div class="controls">
          <button id="step-back" class="control" type="button" aria-label="Previous frame">−1</button>
          <button id="play-toggle" class="control primary" type="button">Play</button>
          <button id="step-forward" class="control" type="button" aria-label="Next frame">+1</button>
        </div>
      </div>
      <div class="timeline-body">
        <div class="scrubber-row"><span class="tick-label" id="first-frame">0</span><input id="timeline" type="range" min="0" max="0" value="0" step="1" aria-label="Timeline frame"><span class="tick-label" id="last-frame">0</span></div>
        <div class="lanes">
          <div class="lane-row"><span class="lane-label">Packets</span><div id="packet-lane" class="lane"><div class="playhead"></div></div></div>
          <div class="lane-row"><span class="lane-label">Rollback</span><div id="rollback-lane" class="lane"><div class="playhead"></div></div></div>
        </div>
        <div class="event-readout">
          <div class="event-pill"><span>At frame</span><strong id="event-frame">—</strong></div>
          <div class="event-pill"><span>Rollback origin</span><strong id="rollback-origin">—</strong></div>
          <div class="event-pill"><span>Rollback depth</span><strong id="rollback-depth">0</strong></div>
          <div class="event-pill"><span>Desync</span><strong id="desync-state">None</strong></div>
        </div>
      </div>
    </article>
  </section>
  <footer class="footer"><span>Trace v<span id="trace-version">1</span> · Scenario <span id="scenario-seed" class="mono">0</span></span><span>Timing is observational; canonical identity comes from integer state and confirmed inputs.</span></footer>
</main>
<script>
const TRACE = )HTML";

    static constexpr auto suffix = R"HTML(;
(() => {
  'use strict';
  const frames = TRACE.frames;
  const timeline = document.getElementById('timeline');
  const arena = document.getElementById('arena');
  const context = arena.getContext('2d');
  let index = 0;
  let timer = null;

  const byId = id => document.getElementById(id);
  const setText = (id, value) => { byId(id).textContent = String(value); };
  const percent = value => {
    const last = Math.max(1, frames[frames.length - 1].frame);
    return Math.max(0, Math.min(100, value / last * 100));
  };

  function drawGrid() {
    const gradient = context.createLinearGradient(0, 0, arena.width, arena.height);
    gradient.addColorStop(0, '#101722');
    gradient.addColorStop(1, '#090d14');
    context.fillStyle = gradient;
    context.fillRect(0, 0, arena.width, arena.height);
    context.strokeStyle = 'rgba(255,255,255,.045)';
    context.lineWidth = 1;
    for (let x = 64; x < arena.width; x += 64) {
      context.beginPath(); context.moveTo(x, 0); context.lineTo(x, arena.height); context.stroke();
    }
    for (let y = 64; y < arena.height; y += 64) {
      context.beginPath(); context.moveTo(0, y); context.lineTo(arena.width, y); context.stroke();
    }
    context.strokeStyle = 'rgba(88,216,255,.17)';
    context.strokeRect(12, 12, arena.width - 24, arena.height - 24);
    context.setLineDash([8, 10]);
    context.beginPath(); context.moveTo(arena.width / 2, 24); context.lineTo(arena.width / 2, arena.height - 24); context.stroke();
    context.setLineDash([]);
  }

  function drawPlayer(player, color, glow) {
    const x = player.x / 1024;
    const y = player.y / 1024;
    context.save();
    context.shadowColor = glow;
    context.shadowBlur = 22;
    context.fillStyle = color;
    context.beginPath(); context.arc(x, y, 13, 0, Math.PI * 2); context.fill();
    context.shadowBlur = 0;
    context.strokeStyle = 'rgba(255,255,255,.75)'; context.lineWidth = 2;
    context.beginPath(); context.moveTo(x, y); context.lineTo(x + player.facing_x * 20, y + player.facing_y * 20); context.stroke();
    context.restore();
  }

  function draw(frame) {
    drawGrid();
    frame.projectiles.forEach(projectile => {
      context.save();
      context.fillStyle = '#ffbe5c'; context.shadowColor = '#ffbe5c'; context.shadowBlur = 14;
      context.beginPath(); context.arc(projectile.x / 1024, projectile.y / 1024, 5, 0, Math.PI * 2); context.fill();
      context.restore();
    });
    drawPlayer(frame.players[0], '#58d8ff', '#58d8ff');
    drawPlayer(frame.players[1], '#9f8cff', '#9f8cff');
  }

  function addMarker(lane, className, at, title) {
    const marker = document.createElement('span');
    marker.className = `marker ${className}`;
    marker.style.left = `${percent(at)}%`;
    marker.title = title;
    lane.appendChild(marker);
  }

  function buildLanes() {
    const packetLane = byId('packet-lane');
    const rollbackLane = byId('rollback-lane');
    TRACE.packets.forEach(packet => addMarker(packetLane, packet.kind, packet.tick, `${packet.kind} · tick ${packet.tick} · ${packet.from}→${packet.to}`));
    TRACE.rollbacks.forEach(rollback => {
      const span = document.createElement('span');
      span.className = 'rollback-span';
      span.style.left = `${percent(rollback.rollback_from)}%`;
      span.style.width = `${Math.max(.35, percent(rollback.observed_at) - percent(rollback.rollback_from))}%`;
      span.title = `rollback ${rollback.rollback_from} → ${rollback.observed_at} (${rollback.depth})`;
      rollbackLane.appendChild(span);
    });
  }

  function updatePlayheads(frameNumber) {
    document.querySelectorAll('.playhead').forEach(playhead => {
      playhead.style.left = `${percent(frameNumber)}%`;
    });
  }

  function update(nextIndex) {
    index = Math.max(0, Math.min(frames.length - 1, nextIndex));
    const frame = frames[index];
    timeline.value = String(index);
    setText('logical-frame', frame.frame);
    setText('frame-mode', frame.predicted ? 'Predicted' : 'Confirmed');
    byId('frame-mode').className = `metric-value ${frame.predicted ? 'amber' : 'cyan'}`;
    setText('confirmed-frame', frame.confirmed_frame);
    setText('state-hash', frame.hash);
    const playerA = frame.players[0];
    const playerB = frame.players[1];
    setText('a-hp', playerA.hp); setText('a-score', playerA.score);
    setText('a-position', `${Math.round(playerA.x / 1024)}, ${Math.round(playerA.y / 1024)}`);
    setText('b-hp', playerB.hp); setText('b-score', playerB.score);
    setText('b-position', `${Math.round(playerB.x / 1024)}, ${Math.round(playerB.y / 1024)}`);
    const rollback = [...TRACE.rollbacks].reverse().find(event => event.observed_at <= frame.frame);
    setText('event-frame', rollback ? rollback.observed_at : '—');
    setText('rollback-origin', rollback ? rollback.rollback_from : '—');
    setText('rollback-depth', rollback ? rollback.depth : 0);
    const desyncAtFrame = TRACE.desync && TRACE.desync.frame <= frame.frame;
    setText('desync-state', desyncAtFrame ? `Frame ${TRACE.desync.frame}` : 'None');
    byId('run-status').textContent = desyncAtFrame ? 'Desync detected' : (frame.predicted ? 'Prediction active' : 'Confirmed state');
    document.querySelector('.status-dot').style.background = desyncAtFrame ? 'var(--red)' : (frame.predicted ? 'var(--amber)' : 'var(--green)');
    updatePlayheads(frame.frame);
    draw(frame);
  }

  function pause() {
    if (timer !== null) { window.clearInterval(timer); timer = null; }
    byId('play-toggle').textContent = 'Play';
  }

  function play() {
    if (timer !== null) return;
    if (index >= frames.length - 1) update(0);
    byId('play-toggle').textContent = 'Pause';
    timer = window.setInterval(() => {
      if (index >= frames.length - 1) { pause(); return; }
      update(index + 1);
    }, 1000 / 30);
  }

  timeline.max = String(frames.length - 1);
  setText('first-frame', frames[0].frame);
  setText('last-frame', frames[frames.length - 1].frame);
  setText('trace-version', TRACE.trace_version);
  setText('scenario-seed', TRACE.scenario_seed);
  setText('rollback-count', TRACE.rollbacks.length);
  setText('resimulated-frames', TRACE.rollbacks.reduce((sum, event) => sum + event.depth, 0));
  setText('maximum-depth', TRACE.rollbacks.reduce((maximum, event) => Math.max(maximum, event.depth), 0));
  setText('packet-total', TRACE.packets.filter(packet => packet.kind === 'sent').length);
  setText('packet-dropped', TRACE.packets.filter(packet => packet.kind === 'dropped').length);
  setText('packet-reordered', TRACE.packets.filter(packet => packet.kind === 'reordered').length);
  buildLanes();
  update(0);

  timeline.addEventListener('input', event => { pause(); update(Number(event.target.value)); });
  byId('step-back').addEventListener('click', () => { pause(); update(index - 1); });
  byId('step-forward').addEventListener('click', () => { pause(); update(index + 1); });
  byId('play-toggle').addEventListener('click', () => { timer === null ? play() : pause(); });
  document.addEventListener('keydown', event => {
    if (event.key === 'ArrowLeft') { pause(); update(index - 1); }
    if (event.key === 'ArrowRight') { pause(); update(index + 1); }
    if (event.key === ' ') { event.preventDefault(); timer === null ? play() : pause(); }
  });
  document.addEventListener('visibilitychange', () => { if (document.hidden) pause(); });
  window.addEventListener('resize', () => draw(frames[index]));

  window.rollbackViewer = Object.freeze({
    trace: TRACE,
    getFrame: () => frames[index].frame,
    setFrame: frameNumber => {
      pause();
      const next = frames.findIndex(frame => frame.frame >= frameNumber);
      update(next < 0 ? frames.length - 1 : next);
      return frames[index].frame;
    },
    play,
    pause
  });
})();
</script>
</body>
</html>
)HTML";

    std::string html;
    html.reserve(std::char_traits<char>::length(prefix) + json.value().size() +
                 std::char_traits<char>::length(suffix));
    html += prefix;
    html += json.value();
    html += suffix;
    return Result<std::string>::success(std::move(html));
}

auto write_viewer(const Trace& trace, const std::filesystem::path& path)
    -> Result<void> {
    const auto html = generate_viewer_html(trace);
    if (!html.ok()) {
        return Result<void>::failure(html.error());
    }
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream) {
        return Result<void>::failure(
            Error{ErrorCode::io_error, 0U, 0U, "viewer_open"});
    }
    stream << html.value();
    if (!stream) {
        return Result<void>::failure(
            Error{ErrorCode::io_error, html.value().size(), 0U,
                  "viewer_write"});
    }
    return Result<void>::success();
}

}  // namespace rollback_lab


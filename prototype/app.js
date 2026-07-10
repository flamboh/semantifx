// THROWAWAY UI: three structural variants sharing one inspectable in-memory state.
const variants = [
  { key: 'A', name: 'Guided studio' },
  { key: 'B', name: 'Conversation' },
  { key: 'C', name: 'Workbench' },
];

const state = {
  capture: 'empty',
  captureSeconds: 0,
  prompt: 'Make it warmer, but keep the vocal clear.',
  analysis: 'idle',
  progress: 0,
  compare: 'suggestion',
  revision: 0,
  history: [],
  parameters: [4, 0.5, 2.5, 0, 0, -0.5],
};

let timer;
const app = document.querySelector('#app');
const wave = () => `<div class="wave ${state.capture === 'recording' ? 'live' : ''}">${Array.from({ length: 45 }, (_, i) => `<i style="--h:${8 + ((i * 17 + i * i) % 38)}"></i>`).join('')}</div>`;
const graph = () => {
  const points = state.parameters.map((gain, i) => `${i * 20},${50 - gain * 4}`).join(' ');
  return `<div class="eq-graph" aria-label="Suggested EQ curve"><svg viewBox="0 0 100 100" preserveAspectRatio="none"><line class="eq-zero" x1="0" y1="50" x2="100" y2="50"/><polyline class="eq-line" points="${points}"/>${state.parameters.map((gain, i) => `<circle class="eq-node" cx="${i * 20}" cy="${50 - gain * 4}" r="2.2"/>`).join('')}</svg></div>`;
};
const bands = ['Low shelf', 'Low mid', 'Presence', 'High mid', 'Air', 'High shelf'];
const controls = (compact = false) => `<div class="parameter-list">${state.parameters.map((gain, i) => `<label class="parameter-row"><span>${bands[i]}</span><input data-gain="${i}" type="range" min="-12" max="12" step="0.1" value="${gain}" aria-label="${bands[i]} gain"><output>${gain > 0 ? '+' : ''}${gain.toFixed(1)} dB</output></label>`).join('')}</div>${compact ? '' : '<p class="muted" style="font-size:11px;margin-top:12px">All suggestions remain ordinary, automatable EQ controls.</p>'}`;
const compare = () => `<div class="compare" aria-label="Compare input and suggestion"><button data-compare="original" class="${state.compare === 'original' ? 'active' : ''}">Original</button><button data-compare="suggestion" class="${state.compare === 'suggestion' ? 'active' : ''}">Suggestion</button></div>`;
const progress = () => state.analysis === 'running' ? `<div><div class="progress"><span style="--progress:${state.progress}%"></span></div><p class="muted" style="font:11px DM Mono;margin-top:9px">Listening for semantic direction · ${state.progress}%</p></div>` : '';
const result = (small = false) => state.analysis === 'done' ? `<div class="result"><div style="display:flex;justify-content:space-between;align-items:center;gap:10px"><div><span class="eyebrow">Suggestion ${state.revision + 1}</span><h3 style="margin:5px 0 0">Warmth with space for the voice</h3></div>${compare()}</div>${graph()}${small ? '' : controls()}<div class="actions" style="margin-top:14px"><button class="button" data-action="undo">Undo</button><button class="button" data-action="refine">Refine this</button><button class="button quiet" data-action="reject">Reject</button></div></div>` : `<div class="result-empty"><div><div style="font-size:34px;margin-bottom:10px">⌁</div><p>${state.analysis === 'running' ? 'Building an editable suggestion…' : 'Your suggested curve will appear here.'}</p>${progress()}</div></div>`;
const stateDrawer = () => `<details class="state-drawer"><summary>Inspect complete prototype state</summary><pre>${escapeHtml(JSON.stringify(state, null, 2))}</pre></details>`;
const header = () => `<header class="topbar"><div class="brand"><div class="brand-mark">SFX</div><div><strong>SemantiFX</strong><br><span>semantic equalizer</span></div></div><div class="status-pill">Local engine ready</div></header>`;

function guided() {
  return `<div class="shell"><div class="guided-shell">${header()}<section class="guided"><div class="intro"><span class="eyebrow">A — Guided studio</span><h1>Say where the sound should go.</h1><p class="muted">Capture one meaningful moment. Describe the direction in your own vocabulary. Keep, edit, or discard every setting.</p></div><div class="steps"><article class="panel step"><div class="step-number"><b>1</b><span>CAPTURE A MOMENT</span></div><button class="capture-orb ${state.capture === 'recording' ? 'recording' : ''}" data-action="capture"><span>${state.capture === 'recording' ? 'Listening…' : state.capture === 'ready' ? 'Captured 5.0s' : 'Capture 5 seconds'}</span></button>${wave()}<p class="muted" style="text-align:center;font-size:12px">Use a passage that contains what you want to change.</p></article><article class="panel step"><div class="step-number"><b>2</b><span>DESCRIBE THE DIRECTION</span></div><h2>What should this feel like?</h2><textarea class="prompt" aria-label="Desired sound">${escapeHtml(state.prompt)}</textarea><div class="actions"><button class="button primary" data-action="analyze" ${state.capture !== 'ready' || state.analysis === 'running' ? 'disabled' : ''}>${state.analysis === 'running' ? 'Analyzing…' : 'Make a suggestion'}</button><button class="button quiet" data-prompt="More intimate and close">Try “more intimate”</button></div>${progress()}</article><article class="panel step"><div class="step-number"><b>3</b><span>LISTEN, THEN DECIDE</span></div>${result(true)}</article></div>${stateDrawer()}</section></div></div>`;
}

function conversation() {
  const messages = state.history.map(item => `<div class="message ${item.role}">${escapeHtml(item.text)}</div>`).join('');
  return `<div class="shell"><div class="conversation-shell">${header()}<section class="conversation"><span class="eyebrow">B — Conversation</span><h1>Shape the sound through dialogue.</h1><div class="panel chat"><div class="message">Play me the moment you want to work on, then describe what you hear in your own words.</div><div class="sound-card"><button class="button capture-mini ${state.capture === 'recording' ? 'recording' : ''}" data-action="capture">${state.capture === 'ready' ? '✓ Captured' : state.capture === 'recording' ? '■ Listening' : '● Capture'}</button>${wave()}<span class="muted" style="font:11px DM Mono">${state.capture === 'ready' ? '5.0 SEC' : 'NO CLIP'}</span></div>${messages}${state.analysis === 'done' ? `<div class="message">Here’s one interpretation. I kept the upper mids open while adding weight below.</div><div class="inline-result">${result(true)}</div>` : ''}${state.analysis === 'running' ? `<div class="message">I’m listening across the captured phrase…${progress()}</div>` : ''}<div class="chat-compose"><button class="button capture-mini" data-action="capture" title="Replace captured excerpt">⌁</button><input class="prompt" value="${escapeHtml(state.prompt)}" aria-label="Message SemantiFX"><button class="button primary" data-action="analyze" ${state.capture !== 'ready' || state.analysis === 'running' ? 'disabled' : ''}>Send ↗</button></div></div>${stateDrawer()}</section></div></div>`;
}

function workbench() {
  return `<div class="workbench-shell"><header class="workbench-top"><div class="brand"><div class="brand-mark">SFX</div><strong>SemantiFX</strong></div><div class="actions">${compare()}<button class="button" data-action="undo">↶ Undo</button><button class="button" data-action="capture">${state.capture === 'recording' ? '■ Capturing' : state.capture === 'ready' ? '✓ 5.0s source' : '● Capture source'}</button></div></header><main class="workbench"><aside class="rail"><span class="eyebrow">C — Workbench</span><h3>Suggestion history</h3><div class="history-list"><button class="history-item active">Current curve<small>${escapeHtml(state.prompt)}</small></button><button class="history-item">Neutral<small>Starting state</small></button></div><h3>Source excerpt</h3>${wave()}<p class="muted" style="font:11px DM Mono">${state.capture === 'ready' ? 'CAPTURE 01 · 5.0 SEC' : 'AWAITING CAPTURE'}</p><h3>Engine</h3><p class="muted" style="font-size:12px">MS-CLAP · Local<br>Quick pass · 100 iterations</p></aside><section class="work-main"><div style="display:flex;justify-content:space-between;align-items:end"><div><span class="eyebrow">Live response</span><h2 style="margin:5px 0 13px">Six-band parametric EQ</h2></div><span class="muted" style="font:11px DM Mono">−20 dB &nbsp; 20 Hz — 21 kHz &nbsp; +20 dB</span></div>${graph()}<div class="command-bar"><input class="prompt" value="${escapeHtml(state.prompt)}" aria-label="Semantic EQ command"><button class="button primary" data-action="analyze" ${state.capture !== 'ready' || state.analysis === 'running' ? 'disabled' : ''}>${state.analysis === 'running' ? `${state.progress}%` : 'Analyze ↵'}</button></div>${progress()}<div class="actions"><button class="button" data-action="refine">New variation</button><button class="button quiet" data-action="reject">Reset curve</button></div>${stateDrawer()}</section><aside class="rail right-rail"><h3>Gain controls</h3>${controls(true)}<h3>Output</h3><div style="display:grid;grid-template-columns:1fr 1fr;gap:10px"><div><span class="muted" style="font:10px DM Mono">IN</span><div class="meter"></div></div><div><span class="muted" style="font:10px DM Mono">OUT</span><div class="meter" style="transform:scaleY(.66)"></div></div></div></aside></main></div>`;
}

function currentVariant() {
  const key = new URL(location.href).searchParams.get('variant')?.toUpperCase();
  return variants.find(v => v.key === key) ?? variants[0];
}

function render() {
  const variant = currentVariant();
  app.innerHTML = variant.key === 'A' ? guided() : variant.key === 'B' ? conversation() : workbench();
  document.querySelector('#variant-label').textContent = `${variant.key} — ${variant.name}`;
  bind();
}

function bind() {
  document.querySelectorAll('.prompt').forEach(input => input.addEventListener('input', event => { state.prompt = event.target.value; }));
  document.querySelectorAll('[data-gain]').forEach(input => input.addEventListener('input', event => { state.parameters[Number(event.target.dataset.gain)] = Number(event.target.value); render(); }));
  document.querySelectorAll('[data-compare]').forEach(button => button.addEventListener('click', () => { state.compare = button.dataset.compare; render(); }));
  document.querySelectorAll('[data-prompt]').forEach(button => button.addEventListener('click', () => { state.prompt = button.dataset.prompt; render(); }));
  document.querySelectorAll('[data-action]').forEach(button => button.addEventListener('click', () => action(button.dataset.action)));
}

function action(name) {
  if (name === 'capture') {
    clearInterval(timer); state.capture = 'recording'; state.analysis = 'idle'; state.progress = 0; render();
    setTimeout(() => { state.capture = 'ready'; state.captureSeconds = 5; render(); }, 1300);
  }
  if (name === 'analyze' && state.capture === 'ready') {
    clearInterval(timer); state.analysis = 'running'; state.progress = 8;
    state.history.push({ role: 'user', text: state.prompt }); render();
    timer = setInterval(() => { state.progress = Math.min(96, state.progress + 8 + Math.floor(Math.random() * 9)); if (state.progress >= 96) { clearInterval(timer); setTimeout(() => { state.progress = 100; state.analysis = 'done'; render(); }, 350); } render(); }, 180);
  }
  if (name === 'refine') { state.revision += 1; state.prompt = 'A little warmer, with less change above 8 kHz.'; state.analysis = 'idle'; render(); }
  if (name === 'undo' || name === 'reject') { state.parameters = [0, 0, 0, 0, 0, 0]; state.analysis = name === 'reject' ? 'idle' : state.analysis; state.compare = 'original'; render(); }
}

function switchVariant(offset) {
  const current = currentVariant();
  const index = variants.findIndex(v => v.key === current.key);
  const next = variants[(index + offset + variants.length) % variants.length];
  const url = new URL(location.href); url.searchParams.set('variant', next.key); history.replaceState(null, '', url); render();
  window.scrollTo({ top: 0, behavior: 'instant' });
}

document.querySelector('#previous-variant').addEventListener('click', () => switchVariant(-1));
document.querySelector('#next-variant').addEventListener('click', () => switchVariant(1));
window.addEventListener('keydown', event => {
  if (!['ArrowLeft', 'ArrowRight'].includes(event.key)) return;
  if (event.target.matches('input, textarea, [contenteditable]')) return;
  switchVariant(event.key === 'ArrowLeft' ? -1 : 1);
});

function escapeHtml(value) { return String(value).replace(/[&<>'"]/g, char => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', "'": '&#39;', '"': '&quot;' })[char]); }
render();

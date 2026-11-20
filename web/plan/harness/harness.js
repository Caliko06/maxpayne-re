'use strict';
// One validation surface; paths in plan.json are relative to /plan/.
const $ = s => document.querySelector(s);
function node(tag, attrs = {}, ...children) {
  const n = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (key === 'text') n.textContent = value;
    else n.setAttribute(key, value);
  }
  n.append(...children.flat());
  return n;
}
const url = path => new URL(path, location.origin + '/plan/').href;
const clamp = (n, a, b) => Math.min(b, Math.max(a, n));
const stage = $('#stage'), images = [$('#before'), $('#after')];
const view = { mode: 'cortina', cut: 50, zoom: 1, x: 0, y: 0 };
const errors = new Set();
const reduced = matchMedia('(prefers-reduced-motion: reduce)');
let blinkTimer, liveFrame, frozenURL;
function viewport() { return { w: stage.clientWidth / (view.mode === 'lado' ? 2 : 1), h: stage.clientHeight }; }
function layout() {
  const { w, h } = viewport();
  for (const im of images) {
    if (!im.naturalWidth) continue;
    const fit = Math.min(w / im.naturalWidth, h / im.naturalHeight);
    const scale = fit * view.zoom;
    im.style.transform = `translate(${(w - im.naturalWidth * scale) / 2 + view.x}px,${(h - im.naturalHeight * scale) / 2 + view.y}px) scale(${scale})`;
    im.style.imageRendering = scale * devicePixelRatio > 1.01 ? 'pixelated' : 'auto';
  }
  $('.after').style.clipPath = view.mode === 'cortina' ? `inset(0 0 0 ${view.cut}%)` : '';
  $('#divider').style.left = view.cut + '%';
  $('#divider').setAttribute('aria-valuenow', Math.round(view.cut));
}
function fit() { view.zoom = 1; view.x = view.y = 0; layout(); }
function imageStatus() {
  const status = $('#frame-status');
  status.textContent = errors.size ? 'No se pudo cargar: ' + [...errors].join(', ') : 'Cargando imágenes…';
  status.hidden = !errors.size && images.every(im => im.complete && im.naturalWidth);
}
images.forEach(im => {
  im.onload = () => { errors.delete(im.id === 'before' ? 'Baseline' : 'Ahora'); layout(); imageStatus(); };
  im.onerror = () => { errors.add(im.id === 'before' ? 'Baseline' : 'Ahora'); imageStatus(); };
});
function setAfter(src) { errors.delete('Ahora'); images[1].src = url(src); imageStatus(); }
function toggleBlink() { stage.dataset.show = stage.dataset.show === 'after' ? 'before' : 'after'; }
function startBlink() {
  clearInterval(blinkTimer);
  if (view.mode === 'parpadeo' && !reduced.matches && !document.hidden) blinkTimer = setInterval(toggleBlink, 900);
}
function mode(value) {
  view.mode = value; stage.dataset.mode = value; stage.dataset.show = 'after';
  document.querySelectorAll('button[data-mode]').forEach(b => b.setAttribute('aria-pressed', b.dataset.mode === value));
  stage.title = value === 'parpadeo' ? 'Clic o espacio para alternar Baseline y Ahora' : 'Rueda o pellizco: zoom · Doble clic: ajustar';
  startBlink(); fit();
}
reduced.addEventListener('change', startBlink);
document.addEventListener('visibilitychange', startBlink);
document.querySelectorAll('button[data-mode]').forEach(b => b.onclick = () => mode(b.dataset.mode));
document.querySelectorAll('[data-zoom]').forEach(b => b.onclick = () => {
  fit();
  if (b.dataset.zoom !== 'fit' && images[0].naturalWidth) {
    const { w, h } = viewport();
    view.zoom = Number(b.dataset.zoom) / devicePixelRatio / Math.min(w / images[0].naturalWidth, h / images[0].naturalHeight);
    layout();
  }
  $('.zoom').open = false;
});
function point(e) {
  const rect = stage.getBoundingClientRect(), { w } = viewport();
  let x = e.clientX - rect.left;
  if (view.mode === 'lado' && x >= w) x -= w;
  return { x, y: e.clientY - rect.top };
}
function zoomBy(factor, p) {
  const { w, h } = viewport(), next = clamp(view.zoom * factor, .25, 64), ratio = next / view.zoom;
  view.x = p.x - w / 2 - (p.x - w / 2 - view.x) * ratio;
  view.y = p.y - h / 2 - (p.y - h / 2 - view.y) * ratio;
  view.zoom = next; layout();
}
stage.addEventListener('wheel', e => {
  e.preventDefault();
  zoomBy(Math.exp(-e.deltaY * (e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? stage.clientHeight : 1) * (e.ctrlKey ? .01 : .002)), point(e));
}, { passive: false });
stage.ondblclick = fit;
const pointers = new Map();
let gestureMoved = false;
const pair = () => {
  const [a, b] = [...pointers.values()];
  return { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2, distance: Math.max(1, Math.hypot(a.x - b.x, a.y - b.y)) };
};
stage.onpointerdown = e => {
  if (e.target.closest('#divider') || e.button > 0) return;
  if (!pointers.size) gestureMoved = false;
  pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
  stage.setPointerCapture(e.pointerId); stage.focus({ preventScroll: true });
};
stage.onpointermove = e => {
  const previous = pointers.get(e.pointerId); if (!previous) return;
  const oldPair = pointers.size === 2 ? pair() : null;
  const dx = e.clientX - previous.x, dy = e.clientY - previous.y;
  if (dx || dy) gestureMoved = true;
  pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
  if (oldPair) {
    const next = pair(); view.x += next.x - oldPair.x; view.y += next.y - oldPair.y;
    zoomBy(next.distance / oldPair.distance, point({ clientX: next.x, clientY: next.y }));
  } else { view.x += dx; view.y += dy; layout(); }
};
function release(e) {
  if (!pointers.has(e.pointerId)) return;
  pointers.delete(e.pointerId);
  if (!gestureMoved && !pointers.size && view.mode === 'parpadeo' && e.type === 'pointerup') { toggleBlink(); clearInterval(blinkTimer); }
}
stage.onpointerup = release; stage.onpointercancel = release; stage.onlostpointercapture = release;
const divider = $('#divider');
function moveCut(e) { view.cut = clamp((e.clientX - stage.getBoundingClientRect().left) / stage.clientWidth * 100, 0, 100); layout(); }
divider.onpointerdown = e => { e.stopPropagation(); divider.setPointerCapture(e.pointerId); divider.focus({ preventScroll: true }); moveCut(e); };
divider.onpointermove = e => { if (divider.hasPointerCapture(e.pointerId)) moveCut(e); };
stage.onkeydown = e => {
  if (view.mode === 'cortina' && ['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(e.key)) {
    e.preventDefault(); view.cut = e.key === 'Home' ? 0 : e.key === 'End' ? 100 : clamp(view.cut + (e.key === 'ArrowRight' ? 1 : -1) * (e.shiftKey ? 10 : 1), 0, 100); layout();
  } else if (e.key === ' ' && view.mode === 'parpadeo') { e.preventDefault(); clearInterval(blinkTimer); if (!e.repeat) toggleBlink(); }
  else if (e.key === '0') fit();
};
new ResizeObserver(layout).observe(stage);

function cropFigure(path, crop, label) {
  const canvas = node('canvas', { role: 'img', 'aria-label': label });
  const caption = node('figcaption', { text: label });
  const im = new Image();
  im.onload = () => {
    const x = clamp(crop.x, 0, 1), y = clamp(crop.y, 0, 1);
    const w = Math.max(1, Math.round(Math.min(crop.w, 1 - x) * im.naturalWidth));
    const h = Math.max(1, Math.round(Math.min(crop.h, 1 - y) * im.naturalHeight));
    canvas.width = w; canvas.height = h;
    canvas.getContext('2d').drawImage(im, x * im.naturalWidth, y * im.naturalHeight, w, h, 0, 0, w, h);
  };
  im.onerror = () => { canvas.hidden = true; caption.textContent = label + ': imagen no disponible'; };
  im.src = url(path);
  return node('figure', {}, canvas, caption);
}
function detailRow(detail, baseline, latest) {
  const note = node('textarea', { maxlength: 500, placeholder: 'Nota opcional', 'aria-label': 'Nota sobre ' + detail.name });
  note.value = detail.owner?.note || '';
  const status = node('span', { class: 'verdict-status', role: 'status', text: detail.owner?.time ? 'Guardado · ' + detail.owner.time : '' });
  let selected = typeof detail.owner?.ok === 'boolean' ? detail.owner.ok : null, saving = false;
  const buttons = [true, false].map(ok => {
    const b = node('button', { type: 'button', text: ok ? 'Va' : 'No va', 'aria-pressed': selected === ok });
    b.onclick = () => save(ok); return b;
  });
  async function save(ok) {
    if (saving) return;
    saving = true; buttons.forEach(b => { b.disabled = true; }); note.disabled = true; status.textContent = 'Guardando…';
    try {
      const response = await fetch('/plan/verdict', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ detail: detail.name, ok, note: note.value }) });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      selected = ok; buttons.forEach((b, i) => b.setAttribute('aria-pressed', selected === (i === 0)));
      status.textContent = 'Guardado · ' + (ok ? 'Va' : 'No va');
    } catch (e) { status.textContent = 'No se guardó (' + e.message + '). Pulsa Va o No va para reintentar.'; }
    finally { saving = false; buttons.forEach(b => { b.disabled = false; }); note.disabled = false; }
  }
  note.oninput = () => { status.textContent = 'Nota sin guardar. Pulsa Va o No va para guardarla.'; };
  const crop = detail.crop || { x: 0, y: 0, w: 1, h: 1 };
  return node('article', { class: 'detail-row' },
    node('div', { class: 'detail-copy' }, node('h3', { text: detail.name }), node('p', { text: detail.why || '' })),
    node('div', { class: 'crops' }, cropFigure(detail.before || baseline, crop, 'Antes'), cropFigure(detail.after || latest, crop, 'Después')),
    node('div', { class: 'verdict' }, node('div', { class: 'verdict-buttons' }, buttons), note, status));
}
async function loadPlan() {
  try {
    const response = await fetch('/plan/plan.json', { cache: 'no-store' });
    if (!response.ok) throw new Error('HTTP ' + response.status);
    const p = await response.json(), baseline = p.baseline;
    const rounds = [...(p.rounds || [])].sort((a, b) => a.n - b.n);
    const latest = rounds.at(-1)?.shot || baseline.shot;
    $('#goal').textContent = p.goal;
    $('#baseline').textContent = `Baseline ${baseline.time} · ${baseline.commit.slice(0, 7)}`;
    images[0].src = url(baseline.shot); setAfter(latest);
    $('#details').replaceChildren(...(p.details?.length ? p.details.map(d => detailRow(d, baseline.shot, latest)) : [node('p', { class: 'empty', text: 'Aún no hay detalles: el OWNER elige la lista.' })]));
    $('#checks').replaceChildren(...(p.steps || []).flatMap(s => [node('li', { class: 'muted', text: s.name }), ...(s.checks || []).map(c => {
      const evidence = c.ev ? node('a', { href: url(c.ev), class: 'evidence', 'aria-label': 'Evidencia: ' + c.do }, /\.(png|jpe?g|webp|gif)(\?|$)/i.test(c.ev) ? node('img', { src: url(c.ev), alt: 'Evidencia de ' + c.do, loading: 'lazy' }) : node('span', { text: 'Evidencia' })) : node('span', { class: 'muted', text: 'Sin evidencia' });
      return node('li', {}, node('div', { class: 'check-copy' }, node('strong', { text: c.do }), node('span', { text: (c.state || (c.done ? 'Hecho' : 'Abierto')) + ' · ' + (c.who || 'Sin asignar') })), evidence);
    })]), ...(p.restarted?.length ? [node('li', { class: 'muted', text: 'Reabierto por el OWNER' }), ...p.restarted.map(r => node('li', {}, node('div', { class: 'check-copy' }, node('strong', { text: r.what }), node('span', { text: r.when + ' · ' + r.src }))))] : []), ...(p.rejected?.length ? [node('li', { class: 'muted', text: 'Descartado por el OWNER: no se retoma sin su aprobación' }), ...p.rejected.map(r => node('li', {}, node('div', { class: 'check-copy' }, node('strong', { text: r.what }), node('span', { text: r.when + ' · en su lugar: ' + r.instead }))))] : []));
    $('#rounds').replaceChildren(...rounds.map(r => {
      const b = node('button', { type: 'button', 'aria-label': 'Comparar ronda ' + r.n }, node('img', { src: url(r.shot), alt: r.note || 'Ronda ' + r.n, loading: 'lazy' }));
      b.onclick = () => { setAfter(r.shot); stage.focus({ preventScroll: true }); stage.scrollIntoView({ block: 'center' }); };
      return node('figure', {}, b, node('figcaption', { text: `${r.time || ''} · ${r.note || 'Ronda ' + r.n}` }));
    }));
    $('#refs').replaceChildren(...(p.refs || []).map(r => node('figure', {}, node('a', { href: url(r.src) }, node('img', { src: url(r.src), alt: r.name, loading: 'lazy' })), node('figcaption', { text: r.name }))));
  } catch (e) {
    $('#goal').textContent = 'No se pudo cargar el plan';
    $('#baseline').textContent = e.message + ' · Recarga la página para reintentar.';
    $('#details').replaceChildren(node('p', { class: 'empty', text: 'Detalles no disponibles.' }));
    $('#frame-status').textContent = 'Comparación no disponible.';
  }
}
mode('cortina');
loadPlan();

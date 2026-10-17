'use strict';
// ---------------------------------------------------------------- helpers
function h(tag, attrs, ...kids) {
  const e = document.createElement(tag);
  if (attrs) for (const k in attrs) {
    const v = attrs[k];
    if (v == null || v === false) continue;
    if (k === 'class') e.className = v;
    else if (k === 'style' && typeof v === 'object') Object.assign(e.style, v);
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (k === 'text') e.textContent = v;
    else if (k === 'html') e.innerHTML = v;
    else if (v === true) e.setAttribute(k, '');
    else e.setAttribute(k, v);
  }
  for (const c of kids.flat()) if (c != null && c !== false) e.append(c.nodeType ? c : document.createTextNode(String(c)));
  return e;
}
function esc(s) {
  return String(s).replace(/[&<>"]/g, c => c === '&' ? '&amp;' : c === '<' ? '&lt;' : c === '>' ? '&gt;' : '&quot;');
}
const fmtSize = n => {
  if (n < 1024) return n + ' B';
  const u = ['KB', 'MB', 'GB', 'TB'];
  let i = -1;
  do { n /= 1024; i++; } while (n >= 1024 && i < 3);
  return n.toFixed(n < 10 ? 2 : 1) + ' ' + u[i];
};
const pad2 = n => String(n).padStart(2, '0');
function fmtTime(t) {
  if (!t) return '';
  const d = new Date(t * 1000);
  return d.getFullYear() + '-' + pad2(d.getMonth() + 1) + '-' + pad2(d.getDate()) + ' ' + pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ':' + pad2(d.getSeconds());
}
const baseName = p => (p || '').replace(/[\\/]+$/, '').split(/[\\/]/).pop() || p || '';
function joinPath(root, rel) {
  const sep = root.includes('\\') && !root.includes('/') ? '\\' : '/';
  return root.replace(/[\\/]+$/, '') + sep + (sep === '\\' ? rel.replace(/\//g, '\\') : rel);
}
function debounce(fn, ms) {
  let t = 0;
  return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); };
}

// ---------------------------------------------------------------- API
async function api(name, body) {
  let r;
  try {
    r = await fetch('/api/' + name, {
      method: 'POST', headers: { 'X-MBC-Token': window.MBC_TOKEN, 'Content-Type': 'application/json' },
      body: JSON.stringify(body || {}),
    });
  } catch (e) { return { ok: false, error: 'server not reachable: ' + e.message }; }
  if (!r.ok) return { ok: false, error: 'HTTP ' + r.status + ' ' + (await r.text()) };
  try { return await r.json(); } catch (e) { return { ok: false, error: 'bad response' }; }
}

// ---------------------------------------------------------------- persistent settings
const Settings = {
  data: (() => { try { return JSON.parse(localStorage.getItem('mbc.settings') || localStorage.getItem('bcc.settings') || '{}'); } catch (e) { return {}; } })(),
  get(k, d) { return k in this.data ? this.data[k] : d; },
  set(k, v) { this.data[k] = v; try { localStorage.setItem('mbc.settings', JSON.stringify(this.data)); } catch (e) { } },
};

// ---------------------------------------------------------------- toast / modal
function toast(msg, kind, ms) {
  const t = h('div', { class: 'toast ' + (kind || ''), text: msg });
  document.getElementById('toasts').append(t);
  setTimeout(() => t.remove(), ms || (kind === 'err' ? 8000 : 3500));
}

function modal(title, bodyEl, buttons, opts) {
  return new Promise(resolve => {
    const bg = h('div', { class: 'modal-bg' });
    const done = v => { bg.remove(); document.removeEventListener('keydown', onKey, true); resolve(v); };
    if (opts && opts.onReady) opts.onReady(done);
    const foot = h('div', { class: 'foot' }, (buttons || [{ label: 'OK', value: true, primary: true }]).map(b =>
      h('button', { class: (b.primary ? 'primary ' : '') + (b.danger ? 'danger' : ''), onclick: () => done(b.value) }, b.label)));
    const box = h('div', { class: 'modal ' + ((opts && opts.cls) || '') }, h('h3', null, title), h('div', { class: 'body' }, bodyEl), foot);
    bg.append(box);
    const onKey = e => {
      if (e.key === 'Escape') { e.stopPropagation(); done(opts && opts.escValue !== undefined ? opts.escValue : null); }
      else if (e.key === 'Enter' && !(e.target && e.target.tagName === 'TEXTAREA') && opts && opts.enter) {
        e.preventDefault(); e.stopPropagation(); done(opts.enter);
      }
    };
    document.addEventListener('keydown', onKey, true);
    bg.addEventListener('mousedown', e => { if (e.target === bg && opts && opts.dismiss !== false) done(null); });
    document.body.append(bg);
    const f = box.querySelector('input,textarea,select');
    if (f) { f.focus(); if (f.select && f.tagName === 'INPUT') f.select(); }
    else { const pb = foot.querySelector('.primary'); if (pb) pb.focus(); }
  });
}
async function confirmBox(title, msg, okLabel, danger) {
  const r = await modal(title, typeof msg === 'string' ? h('div', { style: { whiteSpace: 'pre-wrap' } }, msg) : msg,
    [{ label: 'Cancel', value: false }, { label: okLabel || 'OK', value: true, primary: !danger, danger }], { escValue: false });
  return r === true;
}
async function promptBox(title, label, value) {
  const inp = h('input', { type: 'text', value: value || '', style: { width: '360px' } });
  const r = await modal(title, h('div', null, h('div', { style: { marginBottom: '6px' } }, label), inp),
    [{ label: 'Cancel', value: false }, { label: 'OK', value: true, primary: true }], { escValue: false, enter: true });
  return r === true ? inp.value : null;
}

function ctxMenu(x, y, items) {
  document.querySelectorAll('.ctxmenu').forEach(e => e.remove());
  const m = h('div', { class: 'ctxmenu' });
  for (const it of items) {
    if (it === '-') { m.append(h('hr')); continue; }
    m.append(h('div', { class: it.disabled ? 'dis' : '', onclick: () => { m.remove(); if (!it.disabled) it.action(); } }, it.label));
  }
  document.body.append(m);
  const r = m.getBoundingClientRect();
  m.style.left = Math.max(0, Math.min(x, innerWidth - r.width - 4)) + 'px';
  m.style.top = Math.max(0, Math.min(y, innerHeight - r.height - 4)) + 'px';
  const close = e => { if (!m.contains(e.target)) { m.remove(); document.removeEventListener('mousedown', close, true); } };
  setTimeout(() => document.addEventListener('mousedown', close, true), 0);
}

// ---------------------------------------------------------------- virtual list
class VList {
  constructor(rowH, render) {
    this.rowH = rowH; this.render = render; this.count = 0;
    this.el = h('div', { class: 'vl', tabindex: '0' });
    this.inner = h('div', { class: 'vl-inner' });
    this.el.append(this.inner);
    this.rows = new Map();
    this.el.addEventListener('scroll', () => { this.update(); if (this.onscroll) this.onscroll(); });
    if (window.ResizeObserver) new ResizeObserver(() => this.update()).observe(this.el);
  }
  setCount(n) { this.count = n; this.inner.style.height = n * this.rowH + 'px'; this.invalidate(); }
  setMinWidth(px) { this.inner.style.minWidth = px + 'px'; }
  invalidate() { this.rows.forEach(e => e.remove()); this.rows.clear(); this.update(); }
  refresh(i) {
    const e = this.rows.get(i);
    if (!e) return;
    e.remove(); this.rows.delete(i);
    this.update();
  }
  firstVisible() { return Math.floor(this.el.scrollTop / this.rowH); }
  visibleCount() { return Math.max(1, Math.floor(this.el.clientHeight / this.rowH)); }
  update() {
    const hgt = this.el.clientHeight || 400;
    const first = Math.max(0, Math.floor(this.el.scrollTop / this.rowH) - 6);
    const last = Math.min(this.count - 1, Math.ceil((this.el.scrollTop + hgt) / this.rowH) + 6);
    for (const [i, e] of this.rows) if (i < first || i > last) { e.remove(); this.rows.delete(i); }
    for (let i = first; i <= last; i++) {
      if (this.rows.has(i)) continue;
      const e = this.render(i);
      e.classList.add('vl-row');
      e.style.top = i * this.rowH + 'px';
      e.style.height = this.rowH + 'px';
      this.inner.append(e);
      this.rows.set(i, e);
    }
  }
  reveal(i, center) {
    const top = i * this.rowH, hgt = this.el.clientHeight;
    if (center) this.el.scrollTop = Math.max(0, top - (hgt - this.rowH) / 2);
    else if (top < this.el.scrollTop) this.el.scrollTop = top;
    else if (top + this.rowH > this.el.scrollTop + hgt) this.el.scrollTop = top + this.rowH - hgt;
    this.update();
  }
}

// ---------------------------------------------------------------- path picker
async function pickPath(start, mode) {  // mode: 'dir' | 'file'
  let cur = null, sel = null;
  const pathIn = h('input', { type: 'text', style: { flex: 1, fontFamily: 'var(--mono)' } });
  const list = h('div', { class: 'list' });
  const info = h('div', { class: 'muted', style: { marginTop: '6px', minHeight: '1.4em' } });
  let roots = [], sep = '/';
  const wrap = h('div', { class: 'picker' },
    h('div', { style: { display: 'flex', gap: '6px', marginBottom: '6px' } },
      h('button', { onclick: () => load(cur ? cur.parent : '') }, '⬆ Up'),
      h('button', { onclick: () => load('') }, 'Drives'),
      pathIn, h('button', { onclick: () => load(pathIn.value) }, 'Go')),
    list, info);
  async function load(p) {
    const r = await api('browse', { path: p });
    if (!r.ok) { info.textContent = r.error; info.style.color = 'var(--err)'; return; }
    info.textContent = ''; info.style.color = '';
    cur = r; sel = null; roots = r.roots; sep = r.sep;
    pathIn.value = r.path;
    list.innerHTML = '';
    const mk = (nm, dir, size, act, click) => {
      const row = h('div', { class: 'it', onclick: () => { list.querySelectorAll('.sel').forEach(x => x.classList.remove('sel')); row.classList.add('sel'); click && click(); }, ondblclick: act },
        h('span', null, dir ? '📁' : '📄'), h('span', { class: 'nm' }, nm), h('span', { class: 'sz' }, size));
      list.append(row);
    };
    if (!r.path) {
      (roots.length ? roots : ['/']).forEach(d => mk(d, true, '', () => load(d)));
      if (r.home) mk('Home: ' + r.home, true, '', () => load(r.home));
    } else {
      for (const e of r.entries) {
        if (e.dir) mk(e.name, true, '', () => load(r.path.replace(/[\\/]+$/, '') + sep + e.name), () => { sel = r.path.replace(/[\\/]+$/, '') + sep + e.name; if (mode === 'dir') info.textContent = sel; });
        else if (mode === 'file') mk(e.name, false, fmtSize(e.size), () => finish(r.path.replace(/[\\/]+$/, '') + sep + e.name), () => { sel = r.path.replace(/[\\/]+$/, '') + sep + e.name; info.textContent = sel; });
      }
    }
  }
  let finish;
  let closeFn = null;
  finish = v => closeFn && closeFn(v);
  const p = modal(mode === 'dir' ? 'Select folder' : 'Select file', wrap,
    [{ label: 'Cancel', value: null }, { label: mode === 'dir' ? 'Select this folder' : 'Select', value: 'ok', primary: true }],
    { cls: 'picker-modal', onReady: d => { closeFn = d; } });
  load(start || '');
  const v = await p;
  if (v === 'ok') {
    if (mode === 'dir') return sel && info.textContent === sel ? sel : (cur && cur.path) || null;
    return sel;
  }
  return typeof v === 'string' && v !== 'ok' ? v : null;
}

// A labelled path input with Browse button.
function pathBar(label, mode, onEnter) {
  const inp = h('input', { type: 'text', spellcheck: 'false', placeholder: mode === 'dir' ? 'folder' : 'file', onkeydown: e => { if (e.key === 'Enter' && onEnter) onEnter(); } });
  const el = h('div', { class: 'pb' }, h('span', { class: 'lbl' }, label), inp,
    h('button', { title: 'Browse…', onclick: async () => { const p = await pickPath(inp.value, mode); if (p) { inp.value = p; if (onEnter) onEnter(); } } }, '…'));
  return { el, inp, get: () => inp.value.trim(), set: v => { inp.value = v || ''; } };
}

function download(name, content, mime) {
  const a = h('a', { href: URL.createObjectURL(new Blob([content], { type: mime || 'text/plain' })), download: name });
  document.body.append(a); a.click(); setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 500);
}

// ---------------------------------------------------------------- text width measuring
// Exact pixel width of the widest string: estimate every string cheaply, then measure the best candidates in the DOM.
function maxTextWidth(strs, font, extra) {
  const n = strs.length;
  if (!n) return 0;
  const est = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    const t = strs[i];
    let c = 0;
    for (let k = 0; k < t.length; k++) {
      const u = t.charCodeAt(k);
      if (u === 9) c = (c + 4) & ~3;
      else if (u >= 0xDC00 && u <= 0xDFFF) continue;
      else if (u >= 0xD800 && u <= 0xDBFF) c += 2;
      else if ((u >= 0x1100 && u <= 0x115F) || (u >= 0x2E80 && u <= 0xA4CF) || (u >= 0xAC00 && u <= 0xD7A3) || (u >= 0xF900 && u <= 0xFAFF) || (u >= 0xFE30 && u <= 0xFE6F) || (u >= 0xFF00 && u <= 0xFF60) || (u >= 0xFFE0 && u <= 0xFFE6)) c += 2;
      else c++;
    }
    est[i] = c * 7 + (extra ? extra(i) : 0);
  }
  let idx;
  if (n <= 60) idx = Array.from({ length: n }, (_, i) => i);
  else {
    idx = Array.from({ length: n }, (_, i) => i).sort((a, b) => est[b] - est[a]).slice(0, 40);
  }
  const probe = h('span', { style: { font, position: 'absolute', visibility: 'hidden', whiteSpace: 'pre', tabSize: 4, left: '-99999px', top: '0' } });
  document.body.append(probe);
  let w = 0;
  for (const i of idx) {
    const t = strs[i];
    probe.textContent = t.length > 200000 ? t.slice(0, 200000) : t;
    w = Math.max(w, probe.getBoundingClientRect().width + (extra ? extra(i) : 0));
  }
  probe.remove();
  return Math.ceil(w);
}

// ---------------------------------------------------------------- resizable side-by-side panes
// n columns (2 or 3) that start with equal width, can be resized by dragging the dividers, and each have their
// own horizontal scrollbar.  Rows size their panes with the CSS variables --pw0.. and shift their scrollable
// content with the --sx0.. variables (see the .sx0/.sx1/.sx2 classes).
class PaneLayout {
  // o: { n, sc (scroller whose clientWidth is the row width), vars (element holding the CSS variables),
  //      box (position:relative element for the dividers; its clientWidth includes scrollbars),
  //      head (grid header element), lead (px before pane 0), gaps (px between panes), fixed ([px], non-scrolling part of a pane),
  //      fixedSide ('l'|'r') }
  constructor(o) {
    this.o = Object.assign({ lead: 0, gaps: [], fixed: [], fixedSide: 'l' }, o);
    const n = this.n = o.n;
    this.fr = Array(n).fill(1 / n);
    this.content = Array(n).fill(0);
    this.gaps = Array.from({ length: n - 1 }, (_, i) => this.o.gaps[i] || 0);
    this.fixed = Array.from({ length: n }, (_, i) => this.o.fixed[i] || 0);
    this.lead = this.o.lead;
    this.hs = Array.from({ length: n }, (_, i) => {
      const sp = h('div', { class: 'hsp' });
      const el = h('div', { class: 'hs', onscroll: () => this.applyScroll(i) }, sp);
      el.sp = sp;
      return el;
    });
    const kids = [];
    if (this.lead) kids.push(h('div'));
    this.hs.forEach((e, i) => { kids.push(e); if (i < n - 1 && this.gaps[i] > 0) kids.push(h('div')); });
    this.hsrow = h('div', { class: 'hsrow', style: { display: 'none' } }, kids);
    this.handles = Array.from({ length: n - 1 }, (_, i) => h('div', {
      class: 'vsplit', title: 'Drag to resize the panes (double-click: equal widths)',
      onmousedown: e => this.startDrag(i, e), ondblclick: () => this.reset(),
    }));
    this.o.box.append(...this.handles);
    this.o.sc.addEventListener('wheel', e => this.onWheel(e), { passive: false });
    if (window.ResizeObserver) new ResizeObserver(() => this.layout()).observe(this.o.sc);
    this.publish();
  }
  tot() { return this.lead + this.gaps.reduce((a, b) => a + b, 0); }
  publish() {
    const v = this.o.vars, t = this.tot();
    this.fr.forEach((f, i) => v.style.setProperty('--pw' + i, 'calc((100% - ' + t + 'px) * ' + f + ')'));
  }
  widths() {
    const W = this.o.sc.clientWidth, avail = Math.max(60 * this.n, W - this.tot());
    return this.fr.map(f => avail * f);
  }
  setContent(arr) { this.content = arr; this.layout(); }
  layout() {
    const sc = this.o.sc;
    if (!sc.clientWidth) return;
    const w = this.widths(), n = this.n;
    const extra = Math.max(0, this.o.box.clientWidth - sc.clientWidth);
    const cols = [];
    if (this.lead) cols.push(this.lead + 'px');
    w.forEach((x, i) => { cols.push(i === n - 1 && this.o.head ? '1fr' : x + 'px'); if (i < n - 1 && this.gaps[i] > 0) cols.push(this.gaps[i] + 'px'); });
    if (this.o.head) { this.o.head.style.gridTemplateColumns = cols.join(' '); this.o.head.style.paddingRight = extra + 'px'; }
    this.hsrow.style.gridTemplateColumns = cols.map((c, i) => c === '1fr' ? w[n - 1] + 'px' : c).join(' ');
    this.hsrow.style.paddingRight = extra + 'px';
    let x = this.lead, any = false;
    w.forEach((pw, i) => {
      const vis = Math.max(20, pw - this.fixed[i]);
      const max = Math.max(0, Math.ceil(this.content[i] - vis));
      const hs = this.hs[i];
      hs.sp.style.width = (vis + max) + 'px';
      hs.style.marginLeft = this.o.fixedSide === 'l' ? this.fixed[i] + 'px' : '0';
      hs.style.marginRight = this.o.fixedSide === 'r' ? this.fixed[i] + 'px' : '0';
      hs.style.visibility = max ? 'visible' : 'hidden';
      if (max) any = true;
      x += pw;
      if (i < n - 1) { this.handles[i].style.left = (x + this.gaps[i] / 2 - 3) + 'px'; x += this.gaps[i]; }
    });
    this.hsrow.style.display = any ? 'grid' : 'none';
    for (let i = 0; i < n; i++) this.applyScroll(i);
    if (this.onLayout) this.onLayout();
  }
  applyScroll(i) {
    const hs = this.hs[i], off = hs.style.visibility === 'hidden' || this.hsrow.style.display === 'none' ? 0 : hs.scrollLeft;
    this.o.vars.style.setProperty('--sx' + i, off + 'px');
  }
  reset() { this.fr = Array(this.n).fill(1 / this.n); this.publish(); this.layout(); }
  startDrag(i, e) {
    if (e.button !== 0) return;
    e.preventDefault();
    const rect = this.o.sc.getBoundingClientRect();
    document.body.classList.add('resizing');
    const move = ev => {
      const w = this.widths(), avail = w.reduce((a, b) => a + b, 0);
      let before = this.lead;
      for (let k = 0; k < i; k++) before += w[k] + this.gaps[k];
      const pair = w[i] + w[i + 1], min = Math.min(pair / 2, Math.max(60, avail * 0.08));
      const wi = Math.min(pair - min, Math.max(min, ev.clientX - rect.left - this.gaps[i] / 2 - before));
      this.fr[i] = wi / avail; this.fr[i + 1] = (pair - wi) / avail;
      this.publish(); this.layout();
    };
    const up = () => { document.removeEventListener('mousemove', move); document.removeEventListener('mouseup', up); document.body.classList.remove('resizing'); };
    document.addEventListener('mousemove', move);
    document.addEventListener('mouseup', up);
  }
  onWheel(e) {
    const pn = e.target.closest && e.target.closest('[data-pane]');
    if (!pn) return;
    let dx = e.deltaX;
    if (!dx && e.shiftKey) dx = e.deltaY;
    if (!dx) return;
    if (e.deltaMode === 1) dx *= 16;
    const hs = this.hs[+pn.dataset.pane];
    if (this.hsrow.style.display === 'none' || hs.style.visibility === 'hidden') return;
    e.preventDefault();
    hs.scrollLeft += dx;
  }
}

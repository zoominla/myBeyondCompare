'use strict';
// ---------------------------------------------------------------- text compare view
const DEFAULT_TEXT_OPTS = { ws: 'none', ignoreCase: false, ignoreBlankLines: false, ignoreEol: true, regexes: [], align: 'smart' };
function textOpts() { return Object.assign({}, DEFAULT_TEXT_OPTS, Settings.get('textOpts', {})); }
const ENC_LIST = [['auto', 'Auto'], ['utf-8', 'UTF-8'], ['utf-8-bom', 'UTF-8 BOM'], ['utf-16le', 'UTF-16 LE'], ['utf-16be', 'UTF-16 BE'], ['ansi', 'ANSI / raw']];
const EOL_CH = { LF: 'L', CRLF: 'C', CR: 'R' };
let _charW = 0;
function charWidth() {
  if (_charW) return _charW;
  const s = h('span', { style: { font: '13px var(--mono)', position: 'absolute', visibility: 'hidden', whiteSpace: 'pre' } }, 'x'.repeat(100));
  document.body.append(s);
  _charW = s.getBoundingClientRect().width / 100 || 7.8;
  s.remove();
  return _charW;
}
// One-sided blocks are tinted like added text; lines that look like comments get the comment tint instead.
const COMMENT_RE = /^(\/\/|\/\*|\*\/|\*|<!--|-->|--|#|;|%%|'|REM\s|rem\s)/;
function isCommentLine(t) { return !!t && COMMENT_RE.test(t.trim()); }
function lineHtml(text, spans) {
  if (!spans || !spans.length) return esc(text);
  let out = '', p = 0;
  for (let k = 0; k + 1 < spans.length; k += 2) {
    const b = spans[k], e = spans[k + 1];
    if (b < p || e <= b) continue;
    out += esc(text.slice(p, b)) + '<span class="ih">' + esc(text.slice(b, e)) + '</span>';
    p = e;
  }
  return out + esc(text.slice(p));
}

class TextView extends View {
  constructor(init) {
    super('text', 'Text Compare');
    this.noHistory = !!(init && init.noHistory);
    this.S = [this.newSide(), this.newSide()];
    this.opts = textOpts();
    this.rows = new Int32Array(0); this.inl = {}; this.secs = []; this.vr = new Int32Array(0);
    this.secOf = new Int32Array(0); this.r2v = new Int32Array(0);
    this.curSec = -1; this.selLine = null; this.onlyDiffs = false; this.undoStack = []; this.redoStack = [];
    this.forceText = !!(init && init.forceText);
    this.stats = null; this.seq = 0; this.matches = []; this.matchIdx = -1; this.loaded = false;
    this.build();
    if (init) {
      this.pb[0].set(init.left); this.pb[1].set(init.right);
      if (init.left || init.right) this.load();
    }
  }
  newSide() { return { path: '', encReq: 'auto', enc: 'utf-8', eol: 'LF', mixed: false, lines: [], eols: [], orig: '', dirty: false }; }
  keyOf(s) { return s.eols.join('') + '\u0001' + s.lines.join('\u0000'); }

  build() {
    this.pb = [pathBar('Left', 'file', () => this.load()), pathBar('Right', 'file', () => this.load())];
    this.btnCompare = h('button', { class: 'primary', onclick: () => this.load() }, 'Compare');
    const pbar = h('div', { class: 'pathbar' }, this.pb[0].el, h('button', { title: 'Swap sides', onclick: () => this.swap() }, '⇄'), this.pb[1].el, this.btnCompare);
    const b = (label, fn, title) => h('button', { onclick: fn, title: title || '' }, label);
    this.btnUndo = b('↶ Undo', () => this.undo(), 'Ctrl+Z');
    this.btnRedo = b('↷ Redo', () => this.redo(), 'Ctrl+Y');
    this.chkOnly = h('input', { type: 'checkbox', onchange: e => { this.onlyDiffs = e.target.checked; this.buildView(); } });
    this.tb = h('div', { class: 'toolbar' },
      b('▲ Prev diff', () => this.nav(-1), 'F7 / Alt+Up'), b('▼ Next diff', () => this.nav(1), 'F8 / Alt+Down'),
      b('First', () => this.nav(0, 'first')), b('Last', () => this.nav(0, 'last')),
      h('span', { class: 'sep' }),
      b('Copy ▶', () => this.copyCur(0, 1), 'Copy the current difference from left to right (Alt+Right)'),
      b('◀ Copy', () => this.copyCur(1, 0), 'Copy the current difference from right to left (Alt+Left)'),
      h('span', { class: 'sep' }), this.btnUndo, this.btnRedo,
      h('span', { class: 'sep' }),
      b('💾 Save left', () => this.save(0)), b('💾 Save right', () => this.save(1)),
      h('span', { class: 'sep' }),
      h('label', null, this.chkOnly, 'Diffs only'),
      this.btnOpts = b('Rules ▾', e => this.optsPopover(e)),
      b('🔍 Find', () => this.toggleFind(true), 'Ctrl+F'),
      b('Report…', () => this.reportDialog()), b('Hex', () => this.openHex()), b('💾 Session', () => saveSessionDialog('text', this.S[0].path, this.S[1].path)));
    this.hd = [0, 1].map(i => this.makeHead(i));
    this.split = 0.5;  // fraction of the width (minus the middle column) given to the left pane
    this.textW = [0, 0];
    this.txhead = h('div', { class: 'txhead' }, this.hd[0].el, h('div', { class: 'hmid' }), this.hd[1].el);
    const head = this.txhead;
    this.findBar = this.makeFind();
    this.list = new VList(20, vi => this.renderRow(vi));
    this.list.setMinWidth(0);
    this.list.el.classList.add('nox');
    this.list.inner.style.setProperty('--split', this.split);
    this.list.el.addEventListener('wheel', e => this.onWheel(e), { passive: false });
    this.list.onscroll = () => this.drawOverview();
    this.overview = h('div', { class: 'overview', onmousedown: e => this.overviewClick(e) }, this.canvas = h('canvas'));
    this.vsplit = h('div', { class: 'vsplit', title: 'Drag to resize the panes (double-click: 50/50)', onmousedown: e => this.startSplit(e), ondblclick: () => this.setSplit(0.5) });
    this.body = h('div', { class: 'txbody' }, this.list.el, this.overview, this.vsplit);
    this.hs = [0, 1].map(i => {
      const sp = h('div', { class: 'hsp' });
      const el = h('div', { class: 'hs', onscroll: () => this.onHScroll(i) }, sp);
      el.sp = sp;
      return el;
    });
    this.hsrow = h('div', { class: 'hsrow', style: { display: 'none' } }, this.hs[0], h('div'), this.hs[1]);
    this.status = h('div', { class: 'statusbar' });
    this.wrap = h('div', { style: { display: 'flex', flexDirection: 'column', flex: 1, minHeight: 0 } }, head, this.findBar, this.body, this.hsrow);
    this.empty = h('div', { class: 'empty' }, 'Choose two files and press Compare.');
    this.main = h('div', { style: { display: 'flex', flexDirection: 'column', flex: 1, minHeight: 0 } }, this.empty);
    this.el.append(pbar, this.tb, this.main, this.status);
    const inner = this.list.inner;
    inner.addEventListener('mousedown', e => this.onMouse(e));
    inner.addEventListener('dblclick', e => this.onDbl(e));
    if (window.ResizeObserver) {
      new ResizeObserver(() => this.drawOverview()).observe(this.overview);
      new ResizeObserver(() => this.layoutPanes()).observe(this.list.el);
    }
    this.updateButtons();
  }

  makeHead(i) {
    const ttl = h('span', { class: 'ttl' });
    const enc = h('select', { title: 'Encoding', onchange: async e => {
      if (this.S[i].dirty && !(await confirmBox('Change encoding', 'This reloads the file and discards unsaved edits on this side. Continue?', 'Reload', true))) { e.target.value = this.S[i].encReq; return; }
      this.S[i].encReq = e.target.value; this.S[i].dirty = false; this.run(true);
    } }, ENC_LIST.map(([v, t]) => h('option', { value: v }, t)));
    const eol = h('select', { title: 'Line endings (changing converts the whole file)', onchange: e => this.setEol(i, e.target.value) },
      [['LF', 'Unix (LF)'], ['CRLF', 'DOS (CRLF)'], ['CR', 'Mac (CR)']].map(([v, t]) => h('option', { value: v }, t)));
    const el = h('div', null, ttl, enc, eol,
      h('button', { title: 'Edit the whole file as text', onclick: () => this.editAll(i) }, '✎'),
      h('button', { title: 'Save this side', onclick: () => this.save(i) }, '💾'));
    return { el, ttl, enc, eol };
  }

  makeFind() {
    const inp = h('input', { type: 'text', placeholder: 'Find text…', style: { width: '240px' }, onkeydown: e => { if (e.key === 'Enter') { e.preventDefault(); this.findStep(e.shiftKey ? -1 : 1); } else if (e.key === 'Escape') this.toggleFind(false); }, oninput: () => this.findRun() });
    const cs = h('input', { type: 'checkbox', onchange: () => this.findRun() });
    this.findInp = inp; this.findCase = cs; this.findInfo = h('span', { class: 'muted' });
    const el = h('div', { class: 'findbar', style: { display: 'none' } }, inp, h('label', null, cs, 'Match case'),
      h('button', { onclick: () => this.findStep(-1) }, '↑'), h('button', { onclick: () => this.findStep(1) }, '↓'), this.findInfo,
      h('button', { onclick: () => this.toggleFind(false) }, '✕'));
    return el;
  }
  toggleFind(on) {
    this.findBar.style.display = on ? 'flex' : 'none';
    if (on) { this.findInp.focus(); this.findInp.select(); } else { this.matches = []; this.matchIdx = -1; this.list.invalidate(); }
  }
  findRun() {
    const q = this.findInp.value, cs = this.findCase.checked;
    this.matches = []; this.matchIdx = -1;
    if (q) {
      const nq = cs ? q : q.toLowerCase();
      for (let vi = 0; vi < this.vr.length; vi++) {
        const ri = this.vr[vi];
        if (ri < 0) continue;
        for (let s = 0; s < 2; s++) {
          const ix = this.rows[ri * 3 + s];
          if (ix < 0) continue;
          const t = this.S[s].lines[ix];
          if ((cs ? t : t.toLowerCase()).includes(nq)) this.matches.push([vi, s]);
        }
      }
    }
    this.findInfo.textContent = q ? this.matches.length + ' match(es)' : '';
    if (this.matches.length) this.findStep(1);
    else this.list.invalidate();
  }
  findStep(d) {
    if (!this.matches.length) return;
    this.matchIdx = (this.matchIdx + d + this.matches.length) % this.matches.length;
    const [vi] = this.matches[this.matchIdx];
    this.findInfo.textContent = (this.matchIdx + 1) + ' / ' + this.matches.length;
    this.list.reveal(vi, true);
    this.list.invalidate();
  }

  onActivate() { this.list.update(); this.layoutPanes(); this.drawOverview(); }
  async canClose() {
    if (!this.S.some(s => s.dirty)) return true;
    return confirmBox('Unsaved changes', 'Close this comparison and discard unsaved edits?', 'Discard', true);
  }

  // ------------------------------------------------------------ loading / diff
  async load() {
    const l = this.pb[0].get(), r = this.pb[1].get();
    if (!l || !r) { toast('Both files are required', 'err'); return; }
    if (this.S.some(s => s.dirty) && !(await confirmBox('Reload', 'Discard unsaved edits and reload from disk?', 'Reload', true))) return;
    this.S[0].path = l; this.S[1].path = r;
    this.S.forEach(s => { s.dirty = false; });
    this.undoStack = []; this.redoStack = [];
    await this.run(true);
  }
  swap() {
    this.S.reverse();
    const l = this.pb[0].get(); this.pb[0].set(this.pb[1].get()); this.pb[1].set(l);
    if (this.loaded) this.run(false);
  }
  reqSide(s) {
    const o = { path: s.path, enc: s.dirty || !s.path ? s.enc : s.encReq };
    if (s.dirty || !s.path || !this.loaded) { }
    if (s.dirty || !s.path) { o.lines = s.lines; o.eols = s.eols.join(''); o.eol = s.eol; }
    return o;
  }
  async run(reloadFromDisk, force) {
    const my = ++this.seq;
    this.btnCompare.disabled = true;
    const rq = { left: this.reqSide(this.S[0]), right: this.reqSide(this.S[1]), opts: this.opts, forceText: !!force || this.forceText };
    const r = await api('text', rq);
    this.btnCompare.disabled = false;
    if (my !== this.seq) return;
    if (!r.ok) { toast(r.error, 'err'); return; }
    if (r.binary) {
      toast('Binary file detected - opening hex compare');
      const l = this.S[0].path, rr = this.S[1].path;
      await Tabs.close(this);
      Tabs.add(new HexView({ left: l, right: rr }));
      return;
    }
    this.adopt(0, r.left); this.adopt(1, r.right);
    this.rows = Int32Array.from(r.rows); this.inl = r.inline; this.secs = r.sections; this.stats = r.stats; this.bytesEqual = r.bytesEqual;
    this.secOf = new Int32Array(this.rows.length / 3).fill(-1);
    this.secs.forEach((s, j) => { for (let k = s[0]; k < s[1]; k++) this.secOf[k] = j; });
    if (this.curSec >= this.secs.length) this.curSec = this.secs.length - 1;
    this.loaded = true;
    if (!this.wrap.isConnected) { this.main.innerHTML = ''; this.main.append(this.wrap); }
    this.updateHeads();
    this.setTitle(baseName(this.S[0].path) + ' ↔ ' + baseName(this.S[1].path));
    if (reloadFromDisk && !this.noHistory) recordHistory('text', this.S[0].path, this.S[1].path);
    this.buildView(true);
    if (reloadFromDisk && this.secs.length && this.curSec < 0) { this.curSec = 0; this.gotoSec(0); }
    this.updateStatus();
  }
  adopt(i, j) {
    const s = this.S[i];
    s.lines = j.lines; s.eols = j.eols.split(''); s.enc = j.enc; s.eol = j.eol; s.mixed = j.mixedEol;
    if (!s.dirty) { s.orig = this.keyOf(s); }
  }
  updateHeads() {
    this.S.forEach((s, i) => {
      const hd = this.hd[i];
      hd.ttl.textContent = (s.dirty ? '● ' : '') + (s.path || '(unsaved text)') + (s.mixed ? '  [mixed EOL]' : '');
      hd.ttl.title = s.path;
      hd.enc.value = s.encReq; hd.eol.value = s.eol;
    });
    this.setDirty(this.S.some(s => s.dirty));
  }
  schedule = debounce(() => this.run(false), 140);

  // ------------------------------------------------------------ view construction
  buildView(keepScroll) {
    const N = this.rows.length / 3;
    const showRows = new Uint8Array(N);
    if (this.onlyDiffs) {
      const ctx = 3;
      for (const [b, e] of this.secs) for (let k = Math.max(0, b - ctx); k < Math.min(N, e + ctx); k++) showRows[k] = 1;
    } else showRows.fill(1);
    const vr = [];
    let hidden = 0;
    this.r2v = new Int32Array(N).fill(-1);
    for (let k = 0; k < N; k++) {
      if (showRows[k]) {
        if (hidden) { vr.push(-hidden); hidden = 0; }
        this.r2v[k] = vr.length; vr.push(k);
      } else hidden++;
    }
    if (hidden) vr.push(-hidden);
    this.vr = Int32Array.from(vr);
    // width
    this.measureWidths();
    const top = this.list.el.scrollTop;
    this.list.setCount(this.vr.length);
    if (keepScroll) this.list.el.scrollTop = top;
    this.layoutPanes();
    this.drawOverview();
    this.updateStatus();
    this.updateButtons();
  }

  renderRow(vi) {
    const ri = this.vr[vi];
    const row = document.createElement('div');
    if (ri < 0) { row.className = 'txrow gap'; row.textContent = '⋯  ' + (-ri) + ' identical line(s) hidden  ⋯'; return row; }
    const l = this.rows[ri * 3], r = this.rows[ri * 3 + 1], kb = this.rows[ri * 3 + 2], k = kb & 7, minor = kb & 8;
    const sec = this.secOf[ri];
    const secCls = sec < 0 ? '' : (this.secs[sec][0] === ri ? ' sec-start' : '') + (this.secs[sec][1] - 1 === ri ? ' sec-end' : '');
    row.className = 'txrow k' + k + (minor ? ' minor' : '') + secCls + (sec >= 0 && sec === this.curSec ? ' cur' : '');
    const sp = this.inl[ri];
    const txt = (s, ix, spans) => ix < 0 ? '' : lineHtml(this.S[s].lines[ix], spans);
    const sel = s => this.selLine && this.selLine.s === s && this.selLine.i === (s ? r : l) ? ' selline' : '';
    const cmt = (s, ix) => k >= 3 && ix >= 0 && isCommentLine(this.S[s].lines[ix]) ? ' cmt' : '';
    const fnd = s => this.matchIdx >= 0 && this.matches[this.matchIdx][0] === vi && this.matches[this.matchIdx][1] === s ? ' found' : '';
    const first = sec >= 0 && this.secs[sec][0] === ri;
    const arrows = first ? '<span data-copy="0" title="Copy to right">▶</span><span data-copy="1" title="Copy to left">◀</span>' : '';
    row.dataset.vi = vi;
    row.innerHTML =
      '<div class="pn L"><div class="ln">' + (l >= 0 ? l + 1 : '') + '</div>' +
      '<div class="tx L' + (l >= 0 ? ' has' : '') + cmt(0, l) + sel(0) + fnd(0) + '" data-s="0" data-i="' + l + '"><span class="c">' + txt(0, l, sp && sp[0]) + '</span></div></div>' +
      '<div class="mid" data-sec="' + sec + '">' + arrows + '</div>' +
      '<div class="pn R"><div class="ln">' + (r >= 0 ? r + 1 : '') + '</div>' +
      '<div class="tx R' + (r >= 0 ? ' has' : '') + cmt(1, r) + sel(1) + fnd(1) + '" data-s="1" data-i="' + r + '"><span class="c">' + txt(1, r, sp && sp[1]) + '</span></div></div>';
    return row;
  }

  // ------------------------------------------------------------ pane layout: equal panes, draggable divider, independent horizontal scrollbars
  measureWidths() {
    const probe = this.probe || (this.probe = h('span', { style: { font: '13px var(--mono)', position: 'absolute', visibility: 'hidden', whiteSpace: 'pre', tabSize: 4, left: '-99999px', top: '0' } }));
    if (!probe.isConnected) document.body.append(probe);
    this.textW = this.S.map(S => {
      const lines = S.lines, cols = new Int32Array(lines.length);
      let max = 0;
      for (let i = 0; i < lines.length; i++) {
        const t = lines[i];
        let c = 0;
        for (let k = 0; k < t.length; k++) {
          const u = t.charCodeAt(k);
          if (u === 9) c = (c + 4) & ~3;
          else if (u >= 0xDC00 && u <= 0xDFFF) continue;
          else if (u >= 0xD800 && u <= 0xDBFF) c += 2;
          else if ((u >= 0x1100 && u <= 0x115F) || (u >= 0x2E80 && u <= 0xA4CF) || (u >= 0xAC00 && u <= 0xD7A3) || (u >= 0xF900 && u <= 0xFAFF) || (u >= 0xFE30 && u <= 0xFE6F) || (u >= 0xFF00 && u <= 0xFF60) || (u >= 0xFFE0 && u <= 0xFFE6)) c += 2;
          else c++;
        }
        cols[i] = c;
        if (c > max) max = c;
      }
      if (!max) return 0;
      let w = 0, n = 0;
      for (let i = 0; i < lines.length && n < 40; i++) {
        if (cols[i] * 4 < max * 3) continue;  // only the longest candidates are measured exactly
        probe.textContent = lines[i].length > 200000 ? lines[i].slice(0, 200000) : lines[i];
        w = Math.max(w, probe.getBoundingClientRect().width);
        n++;
      }
      return Math.ceil(w);
    });
    probe.textContent = '';
  }
  layoutPanes() {
    if (!this.loaded) return;
    const el = this.list.el, cw = el.clientWidth;
    if (!cw) return;
    const MID = 54, LN = 56, avail = Math.max(100, cw - MID);
    const lw = avail * this.split, rw = avail - lw;
    const extra = Math.max(0, this.body.clientWidth - cw);  // vertical scrollbar + overview strip
    this.txhead.style.gridTemplateColumns = lw + 'px ' + MID + 'px 1fr';
    this.txhead.style.paddingRight = extra + 'px';
    this.hsrow.style.gridTemplateColumns = lw + 'px ' + MID + 'px ' + rw + 'px';
    this.hsrow.style.paddingRight = extra + 'px';
    this.vsplit.style.left = (lw + MID / 2 - 3) + 'px';
    let any = false;
    [lw, rw].forEach((pw, i) => {
      const need = this.textW[i] + 12 - (pw - LN);  // pixels of text hidden at the right edge
      const max = Math.max(0, Math.ceil(need));
      this.hs[i].sp.style.width = (pw + max) + 'px';
      this.hs[i].style.visibility = max ? 'visible' : 'hidden';
      if (max) any = true;
    });
    this.hsrow.style.display = any ? 'grid' : 'none';
    for (let i = 0; i < 2; i++) this.onHScroll(i);
  }
  onHScroll(i) {
    const hs = this.hs[i];
    this.list.inner.style.setProperty('--sx' + i, (hs.style.visibility === 'hidden' || this.hsrow.style.display === 'none' ? 0 : hs.scrollLeft) + 'px');
  }
  setSplit(f) {
    this.split = Math.min(0.9, Math.max(0.1, f));
    this.list.inner.style.setProperty('--split', this.split);
    this.layoutPanes();
  }
  startSplit(e) {
    if (e.button !== 0) return;
    e.preventDefault();
    const rect = this.list.el.getBoundingClientRect();
    document.body.classList.add('resizing');
    const move = ev => {
      const avail = Math.max(100, this.list.el.clientWidth - 54);
      this.setSplit((ev.clientX - rect.left - 27) / avail);
    };
    const up = () => {
      document.removeEventListener('mousemove', move);
      document.removeEventListener('mouseup', up);
      document.body.classList.remove('resizing');
    };
    document.addEventListener('mousemove', move);
    document.addEventListener('mouseup', up);
  }
  onWheel(e) {
    const pn = e.target.closest && e.target.closest('.pn');
    if (!pn) return;
    let dx = e.deltaX;
    if (!dx && e.shiftKey) dx = e.deltaY;
    if (!dx) return;
    if (e.deltaMode === 1) dx *= 16;
    const i = pn.classList.contains('R') ? 1 : 0, hs = this.hs[i];
    if (this.hsrow.style.display === 'none' || hs.style.visibility === 'hidden') return;
    e.preventDefault();
    hs.scrollLeft += dx;
  }

  // ------------------------------------------------------------ mouse
  onMouse(e) {
    const cp = e.target.closest('[data-copy]');
    if (cp) {
      const sec = +cp.parentElement.dataset.sec;
      if (cp.dataset.copy === '0') this.copySection(sec, 0, 1); else this.copySection(sec, 1, 0);
      return;
    }
    if (e.target.closest('.mid')) { this.startSplit(e); return; }
    const tx = e.target.closest('.tx');
    if (!tx || e.target.tagName === 'INPUT') return;
    const row = tx.closest('.txrow'), vi = +row.dataset.vi, ri = this.vr[vi];
    const s = +tx.dataset.s, ix = +tx.dataset.i;
    this.selLine = ix >= 0 ? { s, i: ix } : null;
    const sec = this.secOf[ri];
    if (sec >= 0) this.curSec = sec;
    this.list.invalidate();
    this.updateStatus();
    this.updateButtons();
  }
  onDbl(e) {
    if (e.target.closest('.mid') && !e.target.closest('[data-copy]')) { this.setSplit(0.5); return; }
    const tx = e.target.closest('.tx');
    if (!tx || e.target.tagName === 'INPUT') return;
    const s = +tx.dataset.s, ix = +tx.dataset.i;
    if (ix < 0) {
      // double click on an empty (orphan) side: nothing to edit there
      return;
    }
    this.startEdit(tx, s, ix);
  }
  startEdit(tx, s, ix) {
    const old = this.S[s].lines[ix];
    const inp = h('input', { type: 'text', value: old, spellcheck: 'false' });
    tx.innerHTML = ''; tx.append(inp);
    inp.focus();
    let done = false;
    const finish = (commit, insertAfter) => {
      if (done) return; done = true;
      if (commit && inp.value !== old) this.setLine(s, ix, inp.value);
      if (insertAfter) { this.splice(s, ix + 1, 0, ['']); this.afterEditPending = { s, i: ix + 1 }; }
      if (!commit || inp.value === old) { if (!insertAfter) this.list.invalidate(); }
    };
    inp.addEventListener('keydown', ev => {
      if (ev.key === 'Enter') { ev.preventDefault(); finish(true, ev.ctrlKey); }
      else if (ev.key === 'Escape') { ev.preventDefault(); finish(false); }
      ev.stopPropagation();
    });
    inp.addEventListener('blur', () => finish(true));
  }

  // ------------------------------------------------------------ editing primitives
  pushUndo(s) {
    this.undoStack.push({ s, lines: this.S[s].lines, eols: this.S[s].eols });
    if (this.undoStack.length > 200) this.undoStack.shift();
    this.redoStack = [];
  }
  touched(s) {
    const S = this.S[s];
    S.dirty = this.keyOf(S) !== S.orig;
    this.updateHeads(); this.updateButtons();
    this.schedule();
  }
  setLine(s, ix, text) {
    this.pushUndo(s);
    const S = this.S[s];
    const lines = S.lines.slice(); lines[ix] = text;
    S.lines = lines;
    this.touched(s);
  }
  splice(s, start, del, newLines) {
    this.pushUndo(s);
    const S = this.S[s], def = EOL_CH[S.eol] || 'L', oldLen = S.lines.length;
    const lines = S.lines.slice(0, start).concat(newLines, S.lines.slice(start + del));
    const ne = S.eols.slice(0, start).concat(newLines.map(() => def), S.eols.slice(start + del));
    if (start + del === oldLen && oldLen > 0) {
      const lastOld = S.eols[oldLen - 1];
      if (start === oldLen && lastOld === 'n') ne[oldLen - 1] = def;
      if (ne.length) ne[ne.length - 1] = lastOld;
    }
    S.lines = lines; S.eols = ne;
    this.touched(s);
  }
  undo() {
    const u = this.undoStack.pop();
    if (!u) return;
    const S = this.S[u.s];
    this.redoStack.push({ s: u.s, lines: S.lines, eols: S.eols });
    S.lines = u.lines; S.eols = u.eols;
    this.touched(u.s);
  }
  redo() {
    const u = this.redoStack.pop();
    if (!u) return;
    const S = this.S[u.s];
    this.undoStack.push({ s: u.s, lines: S.lines, eols: S.eols });
    S.lines = u.lines; S.eols = u.eols;
    this.touched(u.s);
  }
  setEol(i, eol) {
    const S = this.S[i];
    if (S.eol === eol && !S.mixed) return;
    this.pushUndo(i);
    const ch = EOL_CH[eol];
    S.eols = S.eols.map(c => c === 'n' ? 'n' : ch);
    S.eol = eol; S.mixed = false;
    this.touched(i);
  }
  async editAll(i) {
    const S = this.S[i];
    const ta = h('textarea', { spellcheck: 'false', wrap: 'off' });
    ta.value = S.lines.join('\n') + (S.lines.length && S.eols[S.eols.length - 1] !== 'n' ? '\n' : '');
    const r = await modal('Edit ' + (i ? 'right' : 'left') + ' file as text', ta, [{ label: 'Cancel', value: false }, { label: 'Apply', value: true, primary: true }], { escValue: false, dismiss: false });
    if (r !== true) return;
    let t = ta.value.replace(/\r\n?/g, '\n');
    const finalNl = t.endsWith('\n');
    if (finalNl) t = t.slice(0, -1);
    const lines = t === '' && !finalNl ? [] : t.split('\n');
    if (lines.join('\n') === S.lines.join('\n') && finalNl === (S.eols[S.eols.length - 1] !== 'n')) return;
    this.pushUndo(i);
    const def = EOL_CH[S.eol] || 'L';
    S.lines = lines; S.eols = lines.map((_, k) => k === lines.length - 1 && !finalNl ? 'n' : def);
    this.touched(i);
  }

  copySection(j, from, to) {
    if (j < 0) return;
    const [b, e] = this.secs[j], R = this.rows;
    const src = [], dst = [];
    for (let k = b; k < e; k++) { if (R[k * 3 + from] >= 0) src.push(R[k * 3 + from]); if (R[k * 3 + to] >= 0) dst.push(R[k * 3 + to]); }
    const lines = src.map(i => this.S[from].lines[i]);
    let start, del = dst.length;
    if (dst.length) start = dst[0];
    else {
      start = this.S[to].lines.length;
      for (let k = e; k < R.length / 3; k++) if (R[k * 3 + to] >= 0) { start = R[k * 3 + to]; break; }
    }
    this.curSec = j;
    this.splice(to, start, del, lines);
  }
  copyCur(from, to) {
    if (this.curSec < 0) { toast('Select a difference first'); return; }
    this.copySection(this.curSec, from, to);
  }

  // ------------------------------------------------------------ saving
  async save(i) {
    const S = this.S[i];
    let path = S.path;
    if (!path) { path = await promptBox('Save as', 'File path', ''); if (!path) return; }
    if (!S.dirty && S.path) { toast('Nothing to save on this side'); return false; }
    const r = await api('save', { path, lines: S.lines, eols: S.eols.join(''), enc: S.enc });
    if (!r.ok) { toast(r.error, 'err'); return false; }
    S.path = path; S.dirty = false; S.orig = this.keyOf(S);
    this.pb[i].set(path);
    toast('Saved ' + baseName(path), 'ok');
    this.updateHeads(); this.updateButtons();
    this.run(false);
    return true;
  }
  async saveAll() { for (let i = 0; i < 2; i++) if (this.S[i].dirty) await this.save(i); }

  // ------------------------------------------------------------ navigation
  gotoSec(j) {
    if (j < 0 || j >= this.secs.length) return;
    this.curSec = j;
    const vi = this.r2v[this.secs[j][0]];
    if (vi >= 0) this.list.reveal(Math.max(0, vi - 2), false), this.list.reveal(vi, true);
    this.list.invalidate();
    this.updateStatus(); this.updateButtons();
  }
  nav(d, kind) {
    const n = this.secs.length;
    if (!n) { toast('No differences'); return; }
    if (kind === 'first') return this.gotoSec(0);
    if (kind === 'last') return this.gotoSec(n - 1);
    let j = this.curSec;
    if (j < 0) {
      const top = this.list.firstVisible();
      j = d > 0 ? n : -1;
      for (let k = 0; k < n; k++) { const vi = this.r2v[this.secs[k][0]]; if (vi >= top) { j = d > 0 ? k - 1 : k; break; } }
    }
    j += d;
    if (j < 0 || j >= n) { toast(j < 0 ? 'At the first difference' : 'At the last difference'); return; }
    this.gotoSec(j);
  }
  onKey(e) {
    const inInput = e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA' || e.target.tagName === 'SELECT');
    if (e.key === 'F8' || (e.altKey && e.key === 'ArrowDown')) { this.nav(1); return true; }
    if (e.key === 'F7' || (e.altKey && e.key === 'ArrowUp')) { this.nav(-1); return true; }
    if (e.altKey && e.key === 'ArrowRight') { this.copyCur(0, 1); return true; }
    if (e.altKey && e.key === 'ArrowLeft') { this.copyCur(1, 0); return true; }
    if (e.ctrlKey && e.key.toLowerCase() === 's') { this.saveAll(); return true; }
    if (e.ctrlKey && e.key.toLowerCase() === 'f') { this.toggleFind(true); return true; }
    if (e.key === 'F5') { this.run(true); return true; }
    if (!inInput && e.ctrlKey && e.key.toLowerCase() === 'z') { this.undo(); return true; }
    if (!inInput && e.ctrlKey && e.key.toLowerCase() === 'y') { this.redo(); return true; }
    if (!inInput && this.selLine && e.key === 'Delete') { this.splice(this.selLine.s, this.selLine.i, 1, []); this.selLine = null; return true; }
    if (!inInput && this.selLine && e.key === 'Insert') { this.splice(this.selLine.s, this.selLine.i + 1, 0, ['']); return true; }
    if (!inInput && this.selLine && e.key === 'F2') { const tx = this.list.inner.querySelector('.selline'); if (tx) this.startEdit(tx, this.selLine.s, this.selLine.i); return true; }
    return false;
  }

  // ------------------------------------------------------------ misc UI
  updateButtons() {
    this.btnUndo.disabled = !this.undoStack.length;
    this.btnRedo.disabled = !this.redoStack.length;
  }
  updateStatus() {
    const st = this.status, s = this.stats;
    st.innerHTML = '';
    if (!s) return;
    const seg = (t, v) => h('span', null, t + ' ', h('b', null, String(v)));
    if (s.identical && this.bytesEqual) st.append(h('b', { style: { color: 'var(--ok)' } }, 'Files are identical'));
    else if (s.identical) st.append(h('b', { style: { color: 'var(--ok)' } }, 'No differences (apart from line endings)'));
    else if (s.importantSame) st.append(h('b', { style: { color: 'var(--ok)' } }, 'No important differences'));
    st.append(seg('Differences', this.secs.length), seg('Important', s.importantSections));
    if (this.curSec >= 0) st.append(h('span', null, 'Current ', h('b', null, (this.curSec + 1) + ' / ' + this.secs.length)));
    st.append(seg('Same lines', s.same), seg('Changed', s.different), seg('Left only', s.leftOnly), seg('Right only', s.rightOnly));
    if (this.selLine) st.append(h('span', null, (this.selLine.s ? 'Right' : 'Left') + ' line ', h('b', null, String(this.selLine.i + 1))));
    st.append(h('span', { class: 'muted' }, 'Double-click a line to edit · Del removes the selected line · Ins inserts a line'));
  }

  drawOverview() {
    const c = this.canvas, box = this.overview.getBoundingClientRect();
    if (!box.height || !this.loaded) return;
    const dpr = window.devicePixelRatio || 1;
    c.width = Math.round(box.width * dpr); c.height = Math.round(box.height * dpr);
    const g = c.getContext('2d');
    g.scale(dpr, dpr);
    const H = box.height, W = box.width, total = Math.max(1, this.vr.length);
    const css = getComputedStyle(document.documentElement);
    const col = n => css.getPropertyValue(n).trim() || '#f00';
    this.secs.forEach((s, j) => {
      const vi = this.r2v[s[0]];
      if (vi < 0) return;
      let kind = 'orph';
      for (let k = s[0]; k < s[1]; k++) { const kk = this.rows[k * 3 + 2]; if ((kk & 7) === 2) { kind = 'diff'; break; } if (kk & 8 || (kk & 7) === 1) kind = 'unimp'; }
      g.fillStyle = kind === 'diff' ? col('--diff-fg') : kind === 'unimp' ? col('--unimp-fg') : col('--orph-fg');
      const y = vi / total * H, hh = Math.max(2, (s[1] - s[0]) / total * H);
      g.fillRect(1, y, W - 2, hh);
      if (j === this.curSec) { g.strokeStyle = col('--text'); g.strokeRect(0.5, y - 1, W - 1, hh + 2); }
    });
    const vh = this.list.el.clientHeight / this.list.rowH / total * H;
    g.strokeStyle = 'rgba(100,100,100,.8)';
    g.strokeRect(0.5, this.list.el.scrollTop / this.list.rowH / total * H + 0.5, W - 1, Math.max(4, vh));
  }
  overviewClick(e) {
    const box = this.overview.getBoundingClientRect();
    const vi = Math.floor((e.clientY - box.top) / box.height * this.vr.length);
    this.list.reveal(Math.max(0, Math.min(this.vr.length - 1, vi)), true);
    const ri = this.vr[Math.min(this.vr.length - 1, Math.max(0, vi))];
    if (ri >= 0 && this.secOf[ri] >= 0) { this.curSec = this.secOf[ri]; this.list.invalidate(); this.updateStatus(); }
  }

  optsPopover(ev) {
    const o = this.opts;
    const ws = h('select', null, [['none', 'Compare whitespace'], ['amount', 'Ignore amount of whitespace'], ['leading', 'Ignore leading whitespace'], ['trailing', 'Ignore trailing whitespace'], ['leadtrail', 'Ignore leading & trailing'], ['all', 'Ignore all whitespace']].map(([v, t]) => h('option', { value: v }, t)));
    ws.value = o.ws;
    const ic = h('input', { type: 'checkbox', checked: o.ignoreCase });
    const bl = h('input', { type: 'checkbox', checked: o.ignoreBlankLines });
    const ee = h('input', { type: 'checkbox', checked: o.ignoreEol });
    const al = h('select', null, [['smart', 'Smart'], ['patience', 'Patience'], ['complete', 'Complete (minimal)'], ['fast', 'Fast'], ['sort', 'Sorted lines'], ['none', 'Positional (no alignment)']].map(([v, t]) => h('option', { value: v }, t)));
    al.value = o.align;
    const rx = h('textarea', { rows: 3, style: { width: '100%', fontFamily: 'var(--mono)' }, placeholder: 'regular expressions - text matching any of them is ignored (one per line)' });
    rx.value = (o.regexes || []).join('\n');
    const p = this.popover(ev.currentTarget, h('div', { style: { display: 'grid', gap: '7px', width: '340px' } },
      h('h4', null, 'Text compare rules'), ws,
      h('label', null, ic, 'Ignore case'), h('label', null, bl, 'Ignore blank lines'), h('label', null, ee, 'Ignore line-ending differences'),
      h('div', null, 'Alignment'), al, h('div', null, 'Ignore text matching'), rx,
      h('button', { class: 'primary', onclick: () => {
        Object.assign(o, { ws: ws.value, ignoreCase: ic.checked, ignoreBlankLines: bl.checked, ignoreEol: ee.checked, align: al.value, regexes: rx.value.split('\n').map(x => x.trim()).filter(Boolean) });
        Settings.set('textOpts', o); p.remove(); if (this.loaded) this.run(false);
      } }, 'Apply')));
  }
  popover(btn, content) { return DirView.prototype.popover.call(this, btn, content); }

  async reportDialog() {
    if (this.S.some(s => s.dirty)) { toast('Save your edits first - reports are made from the files on disk', 'err'); return; }
    const style = h('select', null, ['html', 'sidebyside', 'unified', 'context', 'normal', 'summary'].map(s => h('option', { value: s }, s)));
    const r = await modal('Text report', h('div', { style: { display: 'grid', gap: '8px' } }, h('div', null, 'Format'), style),
      [{ label: 'Cancel', value: false }, { label: 'Save report', value: true, primary: true }], { escValue: false });
    if (r !== true) return;
    const res = await api('text/report', { left: this.S[0].path, right: this.S[1].path, opts: this.opts, style: style.value });
    if (!res.ok) { toast(res.error, 'err'); return; }
    download('text-report.' + res.ext, res.content, res.ext === 'html' ? 'text/html' : 'text/plain');
  }
  openHex() { if (this.S[0].path && this.S[1].path) Tabs.add(new HexView({ left: this.S[0].path, right: this.S[1].path })); }
}

'use strict';
// ---------------------------------------------------------------- hex compare view
class HexView extends View {
  constructor(init) {
    super('hex', 'Hex Compare');
    this.noHistory = !!(init && init.noHistory);
    this.pages = new Map(); this.pending = new Set(); this.ranges = []; this.curRange = -1;
    this.PAGE = 8192;
    this.build();
    if (init && init.left) this.pl.set(init.left);
    if (init && init.right) this.pr.set(init.right);
    if (init && init.left && init.right) this.load();
  }
  build() {
    this.pl = pathBar('Left', 'file', () => this.load());
    this.pr = pathBar('Right', 'file', () => this.load());
    this.btn = h('button', { class: 'primary', onclick: () => this.load() }, 'Compare');
    const pb = h('div', { class: 'pathbar' }, this.pl.el, h('button', { title: 'Swap sides', onclick: () => { const l = this.pl.get(); this.pl.set(this.pr.get()); this.pr.set(l); if (this.loaded) this.load(); } }, '⇄'), this.pr.el, this.btn);
    this.goto = h('input', { type: 'text', placeholder: 'offset (0x… or decimal)', style: { width: '170px' }, onkeydown: e => { if (e.key === 'Enter') this.gotoOffset(this.goto.value); } });
    const b = (l, f, t) => h('button', { onclick: f, title: t || '' }, l);
    this.tb = h('div', { class: 'toolbar' }, b('▲ Prev diff', () => this.nav(-1), 'F7'), b('▼ Next diff', () => this.nav(1), 'F8'),
      h('span', { class: 'sep' }), this.goto, b('Go', () => this.gotoOffset(this.goto.value)),
      h('span', { class: 'sep' }), b('⟳ Refresh', () => this.load(), 'F5'),
      b('Text', () => this.openText()), b('💾 Session', () => saveSessionDialog('hex', this.pl.get(), this.pr.get())));
    const digits = Array.from({ length: 16 }, (_, i) => i.toString(16).toUpperCase().padStart(2, '0')).join(' ');
    const pHead = (k, name) => h('div', { class: 'pn ' + (k ? 'R' : 'L'), 'data-pane': k }, h('span', { class: 'pc sx' + k }, h('span', { class: 'hx' }, digits), h('span', { class: 'asc' }, name)));
    const colHead = h('div', { class: 'hexrow hexhead' }, h('span', { class: 'off' }, 'Offset'), pHead(0, 'Left'), pHead(1, 'Right'));
    this.list = new VList(20, i => this.renderRow(i));
    this.list.el.classList.add('nox');
    this.box = h('div', { class: 'dirbox' }, this.list.el);
    this.wrap = h('div', { style: { display: 'flex', flexDirection: 'column', flex: 1, minHeight: 0 } }, colHead, this.box);
    this.pane = new PaneLayout({ n: 2, sc: this.list.el, vars: this.wrap, box: this.box, head: colHead, lead: 84 });
    this.wrap.append(this.pane.hsrow);
    this.pane.setContent([Math.ceil(48 * charWidth() + 176), Math.ceil(48 * charWidth() + 176)]);
    this.status = h('div', { class: 'statusbar' });
    this.empty = h('div', { class: 'empty' }, 'Choose two files and press Compare.');
    this.main = h('div', { style: { display: 'flex', flexDirection: 'column', flex: 1, minHeight: 0 } }, this.empty);
    this.el.append(pb, this.tb, this.main, this.status);
  }
  onActivate() { this.list.update(); this.pane.layout(); }
  onKey(e) {
    if (e.key === 'F8' || (e.altKey && e.key === 'ArrowDown')) { this.nav(1); return true; }
    if (e.key === 'F7' || (e.altKey && e.key === 'ArrowUp')) { this.nav(-1); return true; }
    if (e.key === 'F5') { this.load(); return true; }
    return false;
  }
  openText() { const l = this.pl.get(), r = this.pr.get(); if (l && r) Tabs.add(new TextView({ left: l, right: r, forceText: true, noHistory: true })); }

  async load() {
    const l = this.pl.get(), r = this.pr.get();
    if (!l || !r) { toast('Both files are required', 'err'); return; }
    this.btn.disabled = true;
    const res = await api('hex/open', { left: l, right: r });
    this.btn.disabled = false;
    if (!res.ok) { toast(res.error, 'err'); return; }
    this.info = res; this.loaded = true;
    this.ranges = res.ranges; this.curRange = -1;
    this.pages.clear(); this.pending.clear();
    if (!this.wrap.isConnected) { this.main.innerHTML = ''; this.main.append(this.wrap); }
    this.setTitle(baseName(l) + ' ↔ ' + baseName(r));
    if (!this.noHistory) recordHistory('hex', l, r);
    this.rowsN = Math.ceil(Math.max(res.sizeL, res.sizeR) / 16);
    this.list.setCount(this.rowsN);
    this.updateStatus();
  }

  updateStatus() {
    const i = this.info, st = this.status;
    st.innerHTML = '';
    if (!i) return;
    const seg = (t, v) => h('span', null, t + ' ', h('b', null, String(v)));
    st.append(i.equal ? h('b', { style: { color: 'var(--ok)' } }, 'Files are identical') : h('b', { style: { color: 'var(--err)' } }, 'Files differ'),
      seg('Left size', i.sizeL.toLocaleString('en-US')), seg('Right size', i.sizeR.toLocaleString('en-US')),
      seg('Different bytes', i.diffBytes.toLocaleString('en-US')), seg('Difference ranges', i.nRanges.toLocaleString('en-US')));
    if (i.truncated) st.append(h('span', { style: { color: 'var(--warn)' } }, 'navigation limited to the first ' + (this.ranges.length / 2) + ' ranges'));
    if (this.curRange >= 0) st.append(h('span', null, 'Current ', h('b', null, (this.curRange + 1) + ' / ' + (this.ranges.length / 2)), ' @ 0x' + this.ranges[this.curRange * 2].toString(16)));
  }

  getPage(p) {
    const e = this.pages.get(p);
    if (e) return e;
    if (!this.pending.has(p)) {
      this.pending.add(p);
      const gen = this.info;
      api('hex/window', { left: this.pl.get(), right: this.pr.get(), offset: p * this.PAGE, length: this.PAGE }).then(r => {
        if (gen !== this.info) return;
        this.pending.delete(p);
        if (!r.ok) { toast(r.error, 'err'); return; }
        this.pages.set(p, r);
        if (this.pages.size > 64) this.pages.delete(this.pages.keys().next().value);
        this.list.invalidate();
      });
    }
    return null;
  }

  renderRow(i) {
    const row = h('div', { class: 'hexrow' + (this.curRange >= 0 && Math.floor(this.ranges[this.curRange * 2] / 16) === i ? ' cur' : '') });
    const off = i * 16;
    const p = Math.floor(off / this.PAGE), pg = this.getPage(p);
    row.append(h('span', { class: 'off' }, off.toString(16).toUpperCase().padStart(8, '0')));
    if (!pg) { row.append(h('div', { class: 'pn L', 'data-pane': 0 }, h('span', { class: 'pc sx0' }, h('span', { class: 'hx muted' }, '…'))), h('div', { class: 'pn R', 'data-pane': 1 })); return row; }
    const o2 = (off - p * this.PAGE) * 2;
    const L = pg.left.substr(o2, 32), R = pg.right.substr(o2, 32);
    const cell = (mine, other, sideR) => {
      let hx = '', asc = '';
      const n = Math.max(mine.length, other.length) / 2;
      for (let k = 0; k < 16; k++) {
        const a = mine.substr(k * 2, 2), b = other.substr(k * 2, 2);
        const diff = a !== b;
        let cls = '';
        if (diff) cls = a === '' ? '' : (b === '' ? 'm' : 'd');
        if (a === '') { hx += '   '; asc += ' '; continue; }
        hx += (cls ? '<span class="' + cls + '">' + a + '</span>' : a) + ' ';
        const c = parseInt(a, 16);
        const ch = c >= 32 && c < 127 ? esc(String.fromCharCode(c)) : '·';
        asc += cls ? '<span class="' + cls + '">' + ch + '</span>' : ch;
      }
      return [h('span', { class: 'hx' + (sideR ? ' sideR' : ''), html: hx }), h('span', { class: 'asc', html: asc })];
    };
    row.append(h('div', { class: 'pn L', 'data-pane': 0 }, h('span', { class: 'pc sx0' }, ...cell(L, R, false))),
      h('div', { class: 'pn R', 'data-pane': 1 }, h('span', { class: 'pc sx1' }, ...cell(R, L, true))));
    return row;
  }

  gotoOffset(txt) {
    if (!this.loaded) return;
    const t = txt.trim().toLowerCase();
    const off = t.startsWith('0x') ? parseInt(t.slice(2), 16) : /^[0-9a-f]+$/.test(t) && /[a-f]/.test(t) ? parseInt(t, 16) : parseInt(t, 10);
    if (!isFinite(off) || off < 0) { toast('Invalid offset', 'err'); return; }
    this.list.reveal(Math.min(this.rowsN - 1, Math.floor(off / 16)), true);
  }
  nav(d) {
    const n = this.ranges.length / 2;
    if (!n) { toast('No differences'); return; }
    let j = this.curRange;
    if (j < 0) {
      const top = this.list.firstVisible() * 16;
      j = d > 0 ? -1 : n;
      for (let k = 0; k < n; k++) if (this.ranges[k * 2] >= top) { j = d > 0 ? k - 1 : k; break; }
    }
    j += d;
    if (j < 0 || j >= n) { toast(j < 0 ? 'At the first difference' : 'At the last difference'); return; }
    this.curRange = j;
    this.list.reveal(Math.floor(this.ranges[j * 2] / 16), true);
    this.list.invalidate();
    this.updateStatus();
  }
}

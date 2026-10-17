'use strict';
// ---------------------------------------------------------------- 3-way merge view
class MergeView extends View {
  constructor(init) {
    super('merge', 'Merge');
    this.noHistory = !!(init && init.noHistory);
    this.regions = []; this.choice = []; this.manual = {}; this.expanded = new Set(); this.cur = -1;
    this.info = null; this.loaded = false; this.dirty0 = false; this.topEls = []; this.outEls = [];
    this.build();
    if (init) {
      this.pl.set(init.left); this.pb.set(init.base || init.center); this.pr.set(init.right); this.po.set(init.output || '');
      if (init.left && (init.base || init.center) && init.right) this.load();
    }
  }
  build() {
    this.pl = pathBar('Left', 'file', () => this.load());
    this.pb = pathBar('Base', 'file', () => this.load());
    this.pr = pathBar('Right', 'file', () => this.load());
    this.po = pathBar('Output', 'file');
    this.btn = h('button', { class: 'primary', onclick: () => this.load() }, 'Merge');
    const pbar = h('div', { class: 'pathbar' }, this.pl.el, this.pb.el, this.pr.el);
    const pbar2 = h('div', { class: 'pathbar' }, this.po.el, this.btn);
    const b = (l, f, t) => h('button', { onclick: f, title: t || '' }, l);
    this.tb = h('div', { class: 'toolbar' },
      b('▲ Prev change', () => this.nav(-1, false), 'F7'), b('▼ Next change', () => this.nav(1, false), 'F8'),
      b('⚠ Next conflict', () => this.nav(1, true)),
      h('span', { class: 'sep' }),
      b('Take left', () => this.choose(this.cur, 'left'), 'Use the left version for the selected region'),
      b('Take base', () => this.choose(this.cur, 'base')),
      b('Take right', () => this.choose(this.cur, 'right')),
      b('Left+Right', () => this.choose(this.cur, 'lr')), b('Right+Left', () => this.choose(this.cur, 'rl')),
      b('Edit…', () => this.editRegion(this.cur)), b('Reset', () => this.choose(this.cur, 'auto')),
      h('span', { class: 'sep' }),
      b('Take all left', () => this.chooseAllConflicts('left')), b('Take all right', () => this.chooseAllConflicts('right')),
      h('span', { class: 'sep' }),
      b('💾 Save output', () => this.save(), 'Ctrl+S'), b('💾 Session', () => saveSessionDialog('merge', this.pl.get(), this.pr.get(), this.pb.get())));
    this.head = h('div', { class: 'mghead' }, h('div', { text: 'Left' }), h('div', { text: 'Base' }), h('div', { text: 'Right' }));
    this.top = h('div', { class: 'mgtop' });
    this.topbox = h('div', { class: 'mgtopbox' }, this.top);
    this.out = h('div', { class: 'mgout' });
    this.split = h('div', { class: 'mgsplit' });
    this.outHead = h('div', { class: 'mghead', style: { gridTemplateColumns: '1fr' } }, h('div', { text: 'Output' }));
    this.status = h('div', { class: 'statusbar' });
    this.wrap = h('div', { class: 'mgbody' }, this.head, this.topbox, this.split, this.outHead, this.out);
    this.pane = new PaneLayout({ n: 3, sc: this.top, vars: this.wrap, box: this.topbox, head: this.head });
    this.topbox.after(this.pane.hsrow);
    this.empty = h('div', { class: 'empty' }, 'Choose left, base (common ancestor) and right files, then press Merge.');
    this.main = h('div', { style: { display: 'flex', flexDirection: 'column', flex: 1, minHeight: 0 } }, this.empty);
    this.el.append(pbar, pbar2, this.tb, this.main, this.status);
    // splitter drag
    this.split.addEventListener('mousedown', e => {
      e.preventDefault();
      const startY = e.clientY, h0 = this.topbox.getBoundingClientRect().height;
      const mv = ev => { this.topbox.style.flex = 'none'; this.topbox.style.height = Math.max(60, h0 + ev.clientY - startY) + 'px'; };
      const up = () => { document.removeEventListener('mousemove', mv); document.removeEventListener('mouseup', up); };
      document.addEventListener('mousemove', mv); document.addEventListener('mouseup', up);
    });
  }
  async canClose() {
    if (!this.loaded || !this.touchedAny) return true;
    return confirmBox('Merge', 'Close without saving the merge output?', 'Close', true);
  }
  onKey(e) {
    if (e.key === 'F8' || (e.altKey && e.key === 'ArrowDown')) { this.nav(1, false); return true; }
    if (e.key === 'F7' || (e.altKey && e.key === 'ArrowUp')) { this.nav(-1, false); return true; }
    if (e.ctrlKey && e.key.toLowerCase() === 's') { this.save(); return true; }
    return false;
  }

  async load() {
    const l = this.pl.get(), b = this.pb.get(), r = this.pr.get();
    if (!l || !b || !r) { toast('Left, base and right files are required', 'err'); return; }
    if (this.touchedAny && !(await confirmBox('Merge', 'Discard your choices and merge again?', 'Merge again', true))) return;
    this.btn.disabled = true;
    const res = await api('merge', { left: { path: l }, base: { path: b }, right: { path: r }, opts: textOpts() });
    this.btn.disabled = false;
    if (!res.ok) { toast(res.error, 'err'); return; }
    this.info = res; this.regions = res.regions;
    this.choice = this.regions.map(() => 'auto'); this.manual = {}; this.expanded.clear(); this.cur = -1; this.touchedAny = false;
    this.loaded = true;
    if (!this.wrap.isConnected) { this.main.innerHTML = ''; this.main.append(this.wrap); }
    this.setTitle('Merge: ' + baseName(this.pl.get()));
    if (!this.noHistory) recordHistory('merge', this.pl.get(), this.pr.get(), this.pb.get());
    if (!this.po.get()) this.po.set('');
    this.renderAll();
    const first = this.regions.findIndex(g => g.k !== 'same');
    if (first >= 0) this.select(first, true);
  }

  // ---- region output
  outLines(i) {
    const g = this.regions[i], c = this.choice[i];
    switch (c) {
      case 'left': return g.l || g.o;
      case 'right': return g.r || g.o;
      case 'base': return g.b || g.o;
      case 'lr': return (g.l || g.o).concat(g.r || []);
      case 'rl': return (g.r || g.o).concat(g.l || []);
      case 'manual': return this.manual[i];
      default: return g.k === 'conflict' ? null : g.o;
    }
  }
  unresolved() { return this.regions.reduce((n, g, i) => n + (g.k === 'conflict' && this.choice[i] === 'auto' ? 1 : 0), 0); }

  renderAll() {
    this.top.innerHTML = ''; this.out.innerHTML = '';
    this.topEls = []; this.outEls = [];
    this.regions.forEach((g, i) => {
      const t = this.mkTop(i), o = this.mkOut(i);
      this.topEls.push(t); this.outEls.push(o);
      this.top.append(t); this.out.append(o);
    });
    this.updateStatus();
    this.measure();
  }
  measure() {
    const cols = [[], [], []];
    for (const g of this.regions) {
      if (g.k === 'same') { cols[0].push(...g.o); cols[1].push(...g.o); cols[2].push(...g.o); } else { cols[0].push(...(g.l || [])); cols[1].push(...(g.b || [])); cols[2].push(...(g.r || [])); }
    }
    this.pane.setContent(cols.map(c => c.length ? maxTextWidth(c, '13px ' + getComputedStyle(document.documentElement).getPropertyValue('--mono'), () => 0) + 14 : 0));
  }
  onActivate() { this.pane.layout(); }
  refresh(i) {
    const t = this.mkTop(i), o = this.mkOut(i);
    this.topEls[i].replaceWith(t); this.outEls[i].replaceWith(o);
    this.topEls[i] = t; this.outEls[i] = o;
    this.updateStatus();
    this.measure();
  }
  collapsedLines(lines, i) {
    // returns array of {text} or {gap:n}
    if (lines.length <= 10 || this.expanded.has(i)) return lines.map(t => ({ t }));
    return lines.slice(0, 3).map(t => ({ t })).concat([{ gap: lines.length - 6 }], lines.slice(-3).map(t => ({ t })));
  }
  mkTop(i) {
    const g = this.regions[i];
    const el = h('div', { class: 'mgreg k-' + g.k + (i === this.cur ? ' cur' : ''), onclick: () => this.select(i) });
    if (g.k === 'same') {
      const parts = this.collapsedLines(g.o, i);
      ['cl', 'cb', 'cr'].forEach((col, k) =>
        el.append(h('div', { class: col, 'data-pane': k }, h('pre', { class: 'sx' + k }, parts.map(p => p.gap ? '⋯ ' + p.gap + ' identical lines ⋯' : p.t).join('\n')))));
      return el;
    }
    el.append(h('div', { class: 'cl', 'data-pane': 0 }, h('pre', { class: 'sx0' }, (g.l || []).join('\n'))),
      h('div', { class: 'cb', 'data-pane': 1 }, h('pre', { class: 'sx1' }, (g.b || []).join('\n'))),
      h('div', { class: 'cr', 'data-pane': 2 }, h('pre', { class: 'sx2' }, (g.r || []).join('\n'))));
    return el;
  }
  mkOut(i) {
    const g = this.regions[i], c = this.choice[i];
    const el = h('div', { class: 'mgo k-' + g.k + (i === this.cur ? ' cur' : ''), onclick: () => this.select(i) });
    const lines = this.outLines(i);
    if (lines === null) {
      el.classList.add('unres');
      el.append(h('pre', { 'data-tr': '1' }, '⚠ conflict: ' + (g.l || []).length + ' left line(s) / ' + (g.r || []).length + ' right line(s) - choose a resolution'));
      return el;
    }
    if (c !== 'auto') el.classList.add('chosen');
    const parts = g.k === 'same' ? this.collapsedLines(lines, i) : lines.map(t => ({ t }));
    el.append(h('pre', null, parts.map(p => p.gap ? '⋯ ' + p.gap + ' identical lines ⋯' : p.t).join('\n') || (lines.length ? '' : '')));
    if (g.k !== 'same') {
      const tagText = { auto: g.k === 'left' ? 'auto: left changed' : g.k === 'right' ? 'auto: right changed' : 'auto: same change', left: 'left', right: 'right', base: 'base', lr: 'left + right', rl: 'right + left', manual: 'edited' }[c];
      el.append(h('span', { class: 'tag' }, tagText + (lines.length === 0 ? ' (deleted)' : '')));
    }
    if (g.k === 'same' && lines.length > 10) el.addEventListener('dblclick', () => { if (this.expanded.has(i)) this.expanded.delete(i); else this.expanded.add(i); this.refresh(i); });
    return el;
  }

  select(i, scroll) {
    const old = this.cur;
    this.cur = i;
    if (old >= 0 && old !== i && this.topEls[old]) { this.topEls[old].classList.remove('cur'); this.outEls[old].classList.remove('cur'); }
    if (i >= 0) {
      const g = this.regions[i];
      if (g.k === 'same' && this.expanded && g.o.length > 10 && !this.expanded.has(i)) { this.expanded.add(i); this.refresh(i); }
      this.topEls[i].classList.add('cur'); this.outEls[i].classList.add('cur');
      if (scroll) {
        this.topEls[i].scrollIntoView({ block: 'center' });
        this.outEls[i].scrollIntoView({ block: 'center' });
      }
    }
    this.updateStatus();
  }
  choose(i, c) {
    if (i == null || i < 0) { toast('Select a region first'); return; }
    const g = this.regions[i];
    if (g.k === 'same') { toast('That region is identical in all files'); return; }
    this.choice[i] = c; this.touchedAny = true;
    this.refresh(i);
    this.setDirty(true);
  }
  chooseAllConflicts(c) {
    let n = 0;
    this.regions.forEach((g, i) => { if (g.k === 'conflict' && this.choice[i] === 'auto') { this.choice[i] = c; n++; } });
    if (n) { this.touchedAny = true; this.setDirty(true); this.renderAll(); }
    toast(n ? n + ' conflict(s) resolved with ' + c : 'No unresolved conflicts');
  }
  async editRegion(i) {
    if (i == null || i < 0) { toast('Select a region first'); return; }
    const g = this.regions[i];
    if (g.k === 'same') { toast('That region is identical in all files'); return; }
    const cur = this.outLines(i) || (g.l || []);
    const ta = h('textarea', { spellcheck: 'false', wrap: 'off', style: { height: '40vh' } });
    ta.value = cur.join('\n');
    const r = await modal('Edit merge result for this region', ta, [{ label: 'Cancel', value: false }, { label: 'Apply', value: true, primary: true }], { escValue: false, dismiss: false });
    if (r !== true) return;
    this.manual[i] = ta.value === '' ? [] : ta.value.replace(/\r\n?/g, '\n').split('\n');
    this.choose(i, 'manual');
  }
  nav(d, conflictsOnly) {
    const n = this.regions.length;
    let i = this.cur;
    for (let k = 0; k < n; k++) {
      i += d;
      if (i < 0 || i >= n) { toast(d > 0 ? 'No more ' + (conflictsOnly ? 'conflicts' : 'changes') : 'No earlier changes'); return; }
      const g = this.regions[i];
      if (conflictsOnly ? (g.k === 'conflict' && this.choice[i] === 'auto') : g.k !== 'same') { this.select(i, true); return; }
    }
  }
  updateStatus() {
    const st = this.status, I = this.info;
    st.innerHTML = '';
    if (!I) return;
    const un = this.unresolved();
    const seg = (t, v) => h('span', null, t + ' ', h('b', null, String(v)));
    st.append(un ? h('b', { style: { color: 'var(--err)' } }, un + ' unresolved conflict(s)') : h('b', { style: { color: 'var(--ok)' } }, I.conflicts ? 'All conflicts resolved' : 'No conflicts'));
    st.append(seg('Conflicts', I.conflicts), seg('Auto-merged', I.autoMerged), seg('Regions', this.regions.filter(g => g.k !== 'same').length));
    if (this.cur >= 0) st.append(h('span', null, 'Selected region ', h('b', null, String(this.cur + 1))));
    st.append(h('span', { class: 'muted' }, 'Click a region, then use the toolbar to choose its result'));
  }

  buildOutput() {
    const lines = [];
    this.regions.forEach((g, i) => {
      const o = this.outLines(i);
      if (o === null) {
        lines.push('<<<<<<< ' + baseName(this.pl.get()), ...(g.l || []), '=======', ...(g.r || []), '>>>>>>> ' + baseName(this.pr.get()));
      } else lines.push(...o);
    });
    return lines;
  }
  async save() {
    if (!this.loaded) return;
    const un = this.unresolved();
    if (un && !(await confirmBox('Unresolved conflicts', un + ' conflict(s) are still unresolved. Save anyway with <<<<<<< ======= >>>>>>> conflict markers?', 'Save with markers', true))) return;
    let path = this.po.get();
    if (!path) { path = await promptBox('Save merge result', 'Output file path', this.pl.get()); if (!path) return; this.po.set(path); }
    const lines = this.buildOutput();
    const ch = EOL_CH[this.info.left.eol] || 'L';
    const eols = lines.map((_, k) => k === lines.length - 1 && !this.info.finalNl ? 'n' : ch).join('');
    const r = await api('save', { path, lines, eols, enc: this.info.left.enc });
    if (!r.ok) { toast(r.error, 'err'); return; }
    toast('Saved ' + baseName(path) + (un ? ' (with conflict markers)' : ''), un ? '' : 'ok');
    this.touchedAny = false; this.setDirty(false);
  }
}

'use strict';
// ---------------------------------------------------------------- folder compare view
const ST_CLASS = { 'same': 'same', 'different': 'diff', 'similar': 'similar', 'left-only': 'left', 'right-only': 'right', 'type-mismatch': 'mismatch', 'error': 'error' };
const ST_SYM = { 'same': '=', 'different': '≠', 'similar': '≈', 'left-only': '◀', 'right-only': '▶', 'type-mismatch': '!', 'error': '✖' };

class DirView extends View {
  constructor(init) {
    super('dir', 'Folder Compare');
    this.noHistory = !!(init && init.noHistory);
    this.opts = Object.assign({ criteria: 'binary', filter: '', recursive: true, tolerance: 0, ignoreDst: false, icaseNames: false }, Settings.get('dirOpts', {}));
    this.show = Object.assign({ same: true, similar: true, left: true, right: true, mismatch: true, newerL: true, newerR: true, otherDiff: true, hideEmpty: false }, Settings.get('dirShow', {}));
    this.entries = []; this.end = []; this.vis = []; this.rows = [];
    this.collapsed = new Set(); this.sel = new Set(); this.cur = -1; this.anchor = -1;
    this.summary = null; this.loaded = false; this.busy = false;
    this.build();
    if (init && init.left) this.pl.set(init.left);
    if (init && init.right) this.pr.set(init.right);
    if (init && init.left && init.right) this.compare();
  }

  build() {
    this.pl = pathBar('Left', 'dir', () => this.compare());
    this.pr = pathBar('Right', 'dir', () => this.compare());
    this.btnCompare = h('button', { class: 'primary', onclick: () => this.compare() }, 'Compare');
    const pb = h('div', { class: 'pathbar' }, this.pl.el, h('button', { title: 'Swap sides', onclick: () => this.swap() }, '⇄'), this.pr.el, this.btnCompare);
    const b = (label, fn, title, cls) => h('button', { onclick: fn, title: title || '', class: cls || '' }, label);
    this.tb = h('div', { class: 'toolbar' },
      b('⟳ Refresh', () => this.compare(true), 'F5'),
      b('▶ Copy to right', () => this.fileOp('copyLR'), 'Copy selected items from left to right'),
      b('◀ Copy to left', () => this.fileOp('copyRL'), 'Copy selected items from right to left'),
      b('🗑 Delete…', e => this.deleteMenu(e), 'Delete selected items', 'danger'),
      b('⇅ Sync…', () => this.syncDialog(), 'Synchronize folders'),
      h('span', { class: 'sep' }),
      b('Select differences', () => this.selectWhere(e => e.status !== 'same' && !e.dirL && !e.dirR)),
      b('Expand all', () => { this.collapsed.clear(); this.layout(); }),
      b('Collapse all', () => { this.entries.forEach(e => { if (e.dirL || e.dirR) this.collapsed.add(e.rel); }); this.layout(); }),
      b('Next diff ↓', () => this.nextDiff(1), 'F8'),
      b('Prev diff ↑', () => this.nextDiff(-1), 'F7'),
      h('span', { class: 'sep' }),
      this.btnShow = b('Show ▾', e => this.showPopover(e)),
      this.btnOpts = b('Options ▾', e => this.optsPopover(e)),
      b('Report…', () => this.reportDialog()),
      b('💾 Session', () => saveSessionDialog('dir', this.pl.get(), this.pr.get())));
    const hp = (name) => h('div', { class: 'hp' }, h('div', { class: 'nm' }, name), h('div', { class: 'sz' }, 'Size'), h('div', { class: 'tm' }, 'Modified'));
    const head = h('div', { class: 'dirhead' }, hp('Left'), h('div', { class: 'hmid' }), hp('Right'));
    this.list = new VList(22, i => this.renderRow(i));
    this.list.el.classList.add('nox');
    this.box = h('div', { class: 'dirbox' }, this.list.el);
    this.pane = new PaneLayout({ n: 2, sc: this.list.el, vars: this.list.inner, box: this.box, head, gaps: [38], fixed: [204, 204], fixedSide: 'r' });
    this.list.el.addEventListener('keydown', e => this.listKey(e));
    this.status = h('div', { class: 'statusbar' });
    this.msg = h('div', { class: 'empty' }, 'Enter two folders above and press Compare.');
    this.listWrap = h('div', { style: { display: 'flex', flexDirection: 'column', flex: 1, minHeight: 0 } }, head, this.box, this.pane.hsrow);
    this.body = h('div', { style: { display: 'flex', flexDirection: 'column', flex: 1, minHeight: 0 } }, this.msg);
    this.el.append(pb, this.tb, this.body, this.status);
  }

  onActivate() { setTimeout(() => this.list.el.focus({ preventScroll: true }), 0); this.pane.layout(); }
  swap() { const l = this.pl.get(); this.pl.set(this.pr.get()); this.pr.set(l); if (this.loaded) this.compare(); }

  buildOpts() {
    const o = this.opts;
    return { criteria: o.criteria, filter: o.filter, recursive: o.recursive, tolerance: +o.tolerance || 0, ignoreDst: o.ignoreDst, icaseNames: o.icaseNames, orphanChildren: true, rules: Settings.get('textOpts', {}) };
  }
  req() { return { left: this.pl.get(), right: this.pr.get(), opts: this.buildOpts() }; }

  async compare(keep) {
    const l = this.pl.get(), r = this.pr.get();
    if (!l || !r) { toast('Both folders are required', 'err'); return; }
    this.btnCompare.disabled = true;
    this.status.innerHTML = '<span class="spin"></span> Comparing…';
    const res = await api('dir', this.req());
    this.btnCompare.disabled = false;
    if (!res.ok) { this.status.textContent = ''; toast(res.error, 'err'); return; }
    this.rootL = res.left; this.rootR = res.right;
    this.entries = res.entries; this.summary = res.summary; this.loaded = true;
    const rels = new Set(this.entries.map(e => e.rel));
    if (!keep) { this.collapsed.clear(); }
    this.sel = new Set([...this.sel].filter(x => rels.has(x)));
    this.collapsed = new Set([...this.collapsed].filter(x => rels.has(x)));
    // subtree ends
    const n = this.entries.length, st = [];
    this.end = new Array(n).fill(n);
    for (let k = 0; k < n; k++) {
      while (st.length && this.entries[st[st.length - 1]].depth >= this.entries[k].depth) this.end[st.pop()] = k;
      st.push(k);
    }
    if (!this.listWrap.isConnected) { this.body.innerHTML = ''; this.body.append(this.listWrap); }
    this.setTitle(baseName(l) + ' ↔ ' + baseName(r));
    if (!this.noHistory) recordHistory('dir', l, r);
    this.layout();
  }

  matches(e) {
    const s = this.show;
    switch (e.status) {
      case 'same': return s.same;
      case 'similar': return s.similar;
      case 'left-only': return s.left;
      case 'right-only': return s.right;
      case 'type-mismatch': return s.mismatch;
      case 'error': return true;
      default: return e.newer === 'left' ? s.newerL : e.newer === 'right' ? s.newerR : s.otherDiff;
    }
  }

  layout() {
    const E = this.entries, n = E.length, vis = new Uint8Array(n), files = new Int32Array(n + 1);
    for (let i = n - 1; i >= 0; i--) {
      const e = E[i], isDir = e.dirL || e.dirR;
      if (!isDir) vis[i] = this.matches(e) ? 1 : 0;
      else {
        let any = false;
        // any visible descendant? (prefix sum of visible files)
        any = files[i + 1] - files[this.end[i]] > 0 || false;
        const hasKids = this.end[i] > i + 1;
        vis[i] = any ? 1 : (!hasKids && !this.show.hideEmpty && this.matches(e)) ? 1 : 0;
      }
      files[i] = files[i + 1] + (!isDir && vis[i] ? 1 : 0);
    }
    this.vis = vis;
    const rows = [];
    for (let i = 0; i < n;) {
      if (!vis[i]) { i = (E[i].dirL || E[i].dirR) ? this.end[i] : i + 1; continue; }
      rows.push(i);
      if ((E[i].dirL || E[i].dirR) && this.collapsed.has(E[i].rel)) i = this.end[i]; else i++;
    }
    this.rows = rows;
    this.cur = Math.min(this.cur, rows.length - 1);
    this.list.setCount(rows.length);
    this.measure();
    this.updateStatus();
  }

  measure() {
    const E = this.entries, font = getComputedStyle(this.list.el).font;
    const one = side => {
      const ix = this.rows.filter(r => side === 0 ? E[r].hasL : E[r].hasR);
      return ix.length ? maxTextWidth(ix.map(r => E[r].name), font, i => E[ix[i]].depth * 16 + 6 + 18 + 22 + 6) : 0;
    };
    this.pane.setContent([one(0), one(1)]);
  }

  updateStatus() {
    const s = this.summary;
    if (!s) return;
    const st = this.status;
    st.innerHTML = '';
    const seg = (t, v) => h('span', null, t + ' ', h('b', null, String(v)));
    st.append(seg('Files', s.files), seg('Folders', s.folders), seg('Same', s.same), seg('Different', s.different), seg('Similar', s.similar), seg('Left only', s.leftOnly), seg('Right only', s.rightOnly));
    if (s.errors) st.append(h('span', { style: { color: 'var(--err)' } }, 'Errors ', h('b', null, String(s.errors))));
    st.append(seg('Shown', this.rows.length), seg('Selected', this.sel.size));
    if (!this.entries.length) this.status.append(h('span', null, 'Nothing to show - the folders contain no files.'));
    else if (!this.rows.length) this.status.append(h('span', null, 'No items match the current "Show" filter.'));
  }

  fmtSz(e, side) {
    if (e.dirL || e.dirR) return '';
    return (side === 'L' ? e.sizeL : e.sizeR).toLocaleString('en-US');
  }

  renderRow(i) {
    const e = this.entries[this.rows[i]];
    const isDir = e.dirL || e.dirR;
    const cls = ST_CLASS[e.status] || 'same';
    const row = h('div', { class: 'dirrow st-' + cls + (this.sel.has(e.rel) ? ' sel' : ''), title: e.error || e.reason ? (e.error || ('differs: ' + e.reason)) : '' });
    const indent = e.depth * 16;
    const nameCell = (side, k) => {
      const has = side === 'L' ? e.hasL : e.hasR;
      const d = side === 'L' ? e.dirL : e.dirR;
      const c = h('div', { class: 'c nm c' + side + (e.newer === (side === 'L' ? 'left' : 'right') ? ' newer' : (e.newer ? ' older' : '')) });
      if (!has) return c;
      const inner = h('span', { class: 'nmi sx' + k, style: { paddingLeft: indent + 'px' } });
      inner.append(isDir ? h('span', { class: 'tw', 'data-tw': '1' }, this.collapsed.has(e.rel) ? '▸' : '▾') : h('span', { class: 'tw' }, ''));
      inner.append(h('span', { class: 'ic' }, d ? '📁' : '📄'), e.name);
      c.append(inner);
      return c;
    };
    const numCell = (side, k) => {
      const has = side === 'L' ? e.hasL : e.hasR;
      const c = h('div', { class: 'c num ' + (k === 's' ? 'sz' : 'tm') + ' c' + side + (e.newer === (side === 'L' ? 'left' : 'right') && k === 't' ? ' newer' : '') });
      if (!has) return c;
      c.textContent = k === 's' ? this.fmtSz(e, side) : fmtTime(side === 'L' ? e.mtimeL : e.mtimeR);
      return c;
    };
    const pane = (side, k) => h('div', { class: 'pn ' + side, 'data-pane': k }, nameCell(side, k), numCell(side, 's'), numCell(side, 't'));
    row.append(pane('L', 0), h('div', { class: 'mid' }, ST_SYM[e.status] || ''), pane('R', 1));
    row.addEventListener('mousedown', ev => {
      // the row is rebuilt by pick(), so the twisty has to react on mousedown (a click would never arrive)
      if (ev.button === 0 && ev.target.dataset && ev.target.dataset.tw) { ev.preventDefault(); ev.stopPropagation(); this.toggle(e); return; }
      if (ev.button === 0 || !this.sel.has(e.rel)) this.pick(i, ev);
      // double click: pick() has rebuilt this row, so the browser's own dblclick event would never reach it
      // (its target is gone); use the click count of the second mousedown instead.
      if (ev.button === 0 && ev.detail === 2 && !ev.shiftKey && !ev.ctrlKey && !ev.metaKey) { ev.preventDefault(); this.open(e); }
    });
    row.addEventListener('contextmenu', ev => { ev.preventDefault(); this.context(ev, e); });
    return row;
  }

  repaint() { this.list.invalidate(); this.updateStatus(); }

  pick(i, ev) {
    const E = this.entries, rel = r => E[this.rows[r]].rel;
    if (ev.shiftKey && this.anchor >= 0) {
      const a = Math.min(this.anchor, i), b = Math.max(this.anchor, i);
      if (!ev.ctrlKey) this.sel.clear();
      for (let k = a; k <= b; k++) this.sel.add(rel(k));
    } else if (ev.ctrlKey || ev.metaKey) {
      const r = rel(i);
      if (this.sel.has(r)) this.sel.delete(r); else this.sel.add(r);
      this.anchor = i;
    } else { this.sel = new Set([rel(i)]); this.anchor = i; }
    this.cur = i;
    this.repaint();
    this.list.el.focus({ preventScroll: true });
  }

  toggle(e) {
    if (this.collapsed.has(e.rel)) this.collapsed.delete(e.rel); else this.collapsed.add(e.rel);
    this.layout();
  }

  moveCursor(to, ev) {
    if (!this.rows.length) return;
    to = Math.max(0, Math.min(this.rows.length - 1, to));
    const rel = r => this.entries[this.rows[r]].rel;
    if (ev && ev.shiftKey) {
      if (this.anchor < 0) this.anchor = this.cur < 0 ? to : this.cur;
      this.sel = new Set();
      for (let k = Math.min(this.anchor, to); k <= Math.max(this.anchor, to); k++) this.sel.add(rel(k));
    } else { this.sel = new Set([rel(to)]); this.anchor = to; }
    this.cur = to;
    this.list.reveal(to);
    this.repaint();
  }

  listKey(ev) {
    const E = this.entries;
    const curE = this.cur >= 0 && this.rows.length ? E[this.rows[this.cur]] : null;
    const page = this.list.visibleCount();
    switch (ev.key) {
      case 'ArrowDown': this.moveCursor(this.cur + 1, ev); break;
      case 'ArrowUp': this.moveCursor(this.cur - 1, ev); break;
      case 'PageDown': this.moveCursor(this.cur + page, ev); break;
      case 'PageUp': this.moveCursor(this.cur - page, ev); break;
      case 'Home': this.moveCursor(0, ev); break;
      case 'End': this.moveCursor(this.rows.length - 1, ev); break;
      case 'ArrowRight': if (curE && (curE.dirL || curE.dirR) && this.collapsed.has(curE.rel)) this.toggle(curE); break;
      case 'ArrowLeft': if (curE && (curE.dirL || curE.dirR) && !this.collapsed.has(curE.rel)) this.toggle(curE); break;
      case 'Enter': if (curE) this.open(curE); break;
      case ' ': if (curE) { if (this.sel.has(curE.rel)) this.sel.delete(curE.rel); else this.sel.add(curE.rel); this.repaint(); } break;
      case 'a': case 'A': if (ev.ctrlKey) { this.sel = new Set(this.rows.map(r => E[r].rel)); this.repaint(); } else return; break;
      case 'Delete': this.deleteMenu(null); break;
      default: return;
    }
    ev.preventDefault();
    ev.stopPropagation();
  }

  onKey(ev) {
    if (ev.key === 'F5') { this.compare(true); return true; }
    if (ev.key === 'F8' || (ev.altKey && ev.key === 'ArrowDown')) { this.nextDiff(1); return true; }
    if (ev.key === 'F7' || (ev.altKey && ev.key === 'ArrowUp')) { this.nextDiff(-1); return true; }
    return false;
  }

  nextDiff(dir) {
    const n = this.rows.length;
    for (let k = 1; k <= n; k++) {
      const i = ((this.cur < 0 ? (dir > 0 ? -1 : 0) : this.cur) + dir * k + n * 2) % n;
      const e = this.entries[this.rows[i]];
      if (e.status !== 'same' && !(e.dirL && e.dirR && e.status === 'different')) { this.moveCursor(i); return; }
    }
    toast('No differences');
  }

  selectWhere(fn) {
    this.sel = new Set();
    this.rows.forEach(r => { if (fn(this.entries[r])) this.sel.add(this.entries[r].rel); });
    this.repaint();
  }

  async open(e) {
    if (e.dirL || e.dirR) {
      if (e.hasL && e.hasR && e.dirL && e.dirR && (this.cur >= 0)) { this.toggle(e); return; }
      this.toggle(e); return;
    }
    if (!e.hasL || !e.hasR) { toast('"' + e.name + '" exists on one side only - nothing to compare'); return; }
    if (e.dirL !== e.dirR) return;
    openCompare(joinPath(this.rootL, e.relL || e.rel), joinPath(this.rootR, e.relR || e.rel), null, { noHistory: true });
  }

  context(ev, e) {
    const one = this.sel.size === 1;
    const isDir = e.dirL || e.dirR;
    ctxMenu(ev.clientX, ev.clientY, [
      { label: 'Compare', disabled: !(e.hasL && e.hasR) || isDir, action: () => this.open(e) },
      { label: 'Compare as text', disabled: !(e.hasL && e.hasR) || isDir, action: () => openCompare(joinPath(this.rootL, e.relL || e.rel), joinPath(this.rootR, e.relR || e.rel), 'text', { noHistory: true }) },
      { label: 'Compare as hex', disabled: !(e.hasL && e.hasR) || isDir, action: () => openCompare(joinPath(this.rootL, e.relL || e.rel), joinPath(this.rootR, e.relR || e.rel), 'hex', { noHistory: true }) },
      { label: 'Compare folders in a new tab', disabled: !(e.hasL && e.hasR && e.dirL && e.dirR), action: () => Tabs.add(new DirView({ left: joinPath(this.rootL, e.relL || e.rel), right: joinPath(this.rootR, e.relR || e.rel), noHistory: true })) },
      '-',
      { label: 'Copy to right  ▶', action: () => this.fileOp('copyLR') },
      { label: 'Copy to left  ◀', action: () => this.fileOp('copyRL') },
      '-',
      { label: 'Delete left…', action: () => this.fileOp('deleteL') },
      { label: 'Delete right…', action: () => this.fileOp('deleteR') },
      { label: 'Delete both…', action: () => this.fileOp('deleteBoth') },
      '-',
      { label: one && isDir ? (this.collapsed.has(e.rel) ? 'Expand' : 'Collapse') : 'Select differences in view', action: () => one && isDir ? this.toggle(e) : this.selectWhere(x => x.status !== 'same' && !x.dirL && !x.dirR) },
      { label: 'Copy path', action: () => navigator.clipboard && navigator.clipboard.writeText(joinPath(this.rootL, e.relL || e.rel)) },
    ]);
  }

  deleteMenu(ev) {
    const r = ev ? ev.currentTarget.getBoundingClientRect() : { left: 200, bottom: 200 };
    ctxMenu(r.left, r.bottom, [
      { label: 'Delete left…', action: () => this.fileOp('deleteL') },
      { label: 'Delete right…', action: () => this.fileOp('deleteR') },
      { label: 'Delete both…', action: () => this.fileOp('deleteBoth') },
    ]);
  }

  opsSummary(ops) {
    const counts = {};
    let bytes = 0;
    for (const o of ops) { counts[o.type] = (counts[o.type] || 0) + 1; bytes += o.bytes || 0; }
    const lines = ops.slice(0, 400).map(o => {
      const t = o.type.padEnd(10);
      return t + (o.type.startsWith('delete') || o.type.startsWith('mkdir') ? (o.src || o.dst) : o.src + '  →  ' + o.dst) + (o.isDir ? '/' : '');
    });
    if (ops.length > 400) lines.push('… and ' + (ops.length - 400) + ' more');
    return h('div', null,
      h('div', null, Object.entries(counts).map(([k, v]) => k + ': ' + v).join(',  ') + (bytes ? '   (' + fmtSize(bytes) + ' to copy)' : '')),
      h('div', { class: 'oplist' }, lines.join('\n')));
  }

  async runPlanned(call, body, title, okLabel, danger) {
    const prev = await api(call, Object.assign({}, body, { dryRun: true }));
    if (!prev.ok) { toast(prev.error, 'err'); return; }
    if (!prev.ops.length) { toast('Nothing to do'); return; }
    if (!(await confirmBox(title, this.opsSummary(prev.ops), okLabel || 'Execute', danger))) return;
    const res = await api(call, Object.assign({}, body, { dryRun: false }));
    if (!res.ok) toast(res.error, 'err');
    else if (res.failed) toast(res.failed + ' operation(s) failed:\n' + res.log.filter(l => /fail|error/i.test(l)).slice(0, 8).join('\n'), 'err');
    else toast(res.ops.length + ' operation(s) done', 'ok');
    await this.compare(true);
  }

  async fileOp(op) {
    if (!this.sel.size) { toast('Select one or more items first'); return; }
    const titles = { copyLR: 'Copy to right', copyRL: 'Copy to left', deleteL: 'Delete from left', deleteR: 'Delete from right', deleteBoth: 'Delete from both sides' };
    await this.runPlanned('dir/op', Object.assign(this.req(), { op, rels: [...this.sel] }), titles[op], op.startsWith('delete') ? 'Delete' : 'Copy', op.startsWith('delete'));
  }

  async syncDialog() {
    const mode = h('select', null,
      [['update:lr', 'Update right (copy new/changed files left → right)'], ['update:rl', 'Update left (copy right → left)'], ['update:both', 'Update both sides (newer wins)'],
      ['mirror:lr', 'Mirror to right (right becomes a copy of left, deletes extras)'], ['mirror:rl', 'Mirror to left (left becomes a copy of right, deletes extras)']].map(([v, t]) => h('option', { value: v }, t)));
    const onlySel = h('input', { type: 'checkbox' });
    const empty = h('input', { type: 'checkbox', checked: true });
    const body = h('div', { style: { display: 'grid', gap: '8px' } }, h('div', null, 'Mode'), mode,
      h('label', null, onlySel, 'Only the selected items (' + this.sel.size + ')'), h('label', null, empty, 'Create empty folders'));
    const r = await modal('Synchronize folders', body, [{ label: 'Cancel', value: false }, { label: 'Preview…', value: true, primary: true }], { escValue: false });
    if (r !== true) return;
    await this.runPlanned('dir/sync', Object.assign(this.req(), { mode: mode.value, rels: onlySel.checked ? [...this.sel] : [], createEmpty: empty.checked }), 'Synchronize: ' + mode.selectedOptions[0].text, 'Synchronize', mode.value.startsWith('mirror'));
  }

  async reportDialog() {
    if (!this.loaded) { toast('Compare first'); return; }
    const style = h('select', null, ['html', 'csv', 'xml', 'tree', 'list', 'summary'].map(s => h('option', { value: s }, s)));
    const diffOnly = h('input', { type: 'checkbox' });
    const r = await modal('Folder report', h('div', { style: { display: 'grid', gap: '8px' } }, h('div', null, 'Format'), style, h('label', null, diffOnly, 'Differences only')),
      [{ label: 'Cancel', value: false }, { label: 'Save report', value: true, primary: true }], { escValue: false });
    if (r !== true) return;
    const res = await api('dir/report', Object.assign(this.req(), { style: style.value, show: diffOnly.checked ? 'diff' : 'all' }));
    if (!res.ok) { toast(res.error, 'err'); return; }
    download('folder-report.' + res.ext, res.content, res.ext === 'html' ? 'text/html' : 'text/plain');
  }

  popover(btn, content) {
    document.querySelectorAll('.popover').forEach(x => x.remove());
    const r = btn.getBoundingClientRect();
    const p = h('div', { class: 'popover', style: { left: Math.min(r.left, innerWidth - 330) + 'px', top: r.bottom + 4 + 'px', position: 'fixed' } }, content);
    document.body.append(p);
    const close = ev => { if (!p.contains(ev.target) && ev.target !== btn) { p.remove(); document.removeEventListener('mousedown', close, true); } };
    setTimeout(() => document.addEventListener('mousedown', close, true), 0);
    return p;
  }

  showPopover(ev) {
    const s = this.show;
    const cb = (key, label) => h('label', null, h('input', { type: 'checkbox', checked: s[key], onchange: e => { s[key] = e.target.checked; Settings.set('dirShow', s); this.layout(); } }), label);
    const preset = (label, v) => h('button', { onclick: () => { Object.assign(s, v); Settings.set('dirShow', s); this.layout(); p.remove(); } }, label);
    const p = this.popover(ev.currentTarget, h('div', { style: { display: 'grid', gap: '6px' } },
      h('h4', null, 'Show'),
      h('div', { style: { display: 'flex', gap: '4px', flexWrap: 'wrap' } },
        preset('All', { same: true, similar: true, left: true, right: true, mismatch: true, newerL: true, newerR: true, otherDiff: true }),
        preset('Differences', { same: false, similar: true, left: true, right: true, mismatch: true, newerL: true, newerR: true, otherDiff: true }),
        preset('Same', { same: true, similar: false, left: false, right: false, mismatch: false, newerL: false, newerR: false, otherDiff: false }),
        preset('Orphans', { same: false, similar: false, left: true, right: true, mismatch: false, newerL: false, newerR: false, otherDiff: false })),
      cb('same', 'Identical'), cb('newerL', 'Different - left is newer'), cb('newerR', 'Different - right is newer'), cb('otherDiff', 'Different - other'), cb('similar', 'Similar (rules-based)'),
      cb('left', 'Left orphans'), cb('right', 'Right orphans'), cb('mismatch', 'File/folder mismatch'), cb('hideEmpty', 'Hide empty folders')));
  }

  optsPopover(ev) {
    const o = this.opts;
    const parts = new Set(o.criteria.split(/[,\s+]+/).filter(Boolean));
    const crit = (k, label) => h('label', null, h('input', { type: 'checkbox', checked: parts.has(k), onchange: e => { if (e.target.checked) parts.add(k); else parts.delete(k); } }), label);
    const filter = h('input', { type: 'text', value: o.filter, placeholder: '*.c;*.h;-*.o;-.git/', style: { width: '100%' } });
    const tol = h('input', { type: 'text', value: o.tolerance, style: { width: '60px' } });
    const rec = h('input', { type: 'checkbox', checked: o.recursive });
    const dst = h('input', { type: 'checkbox', checked: o.ignoreDst });
    const icn = h('input', { type: 'checkbox', checked: o.icaseNames });
    const p = this.popover(ev.currentTarget, h('div', { style: { display: 'grid', gap: '6px', width: '340px' } },
      h('h4', null, 'Compare files by'),
      crit('size', 'Size'), crit('time', 'Modified time'), crit('crc', 'CRC'), crit('binary', 'Binary content'), crit('rules', 'Rules-based (text, uses ignore settings of Text Compare)'),
      h('h4', null, 'Filters'), filter,
      h('label', null, rec, 'Include subfolders'), h('label', null, 'Time tolerance (s)', tol), h('label', null, dst, 'Ignore daylight-saving (1 hour) differences'), h('label', null, icn, 'Ignore case of names'),
      h('button', { class: 'primary', onclick: () => {
        if (!parts.size) parts.add('binary');
        Object.assign(o, { criteria: [...parts].join(','), filter: filter.value, recursive: rec.checked, tolerance: +tol.value || 0, ignoreDst: dst.checked, icaseNames: icn.checked });
        Settings.set('dirOpts', o); p.remove(); if (this.pl.get() && this.pr.get()) this.compare(true);
      } }, 'Apply & compare')));
  }
}

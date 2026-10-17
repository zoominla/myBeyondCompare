'use strict';
// ---------------------------------------------------------------- home tab
class HomeView extends View {
  constructor() {
    super('home', 'Home');
    this.items = []; this.closed = new Set(); this.selName = null; this.filter = '';
    this.build();
    this.reload();
  }
  build() {
    const card = (big, nm, ds, fn) => h('div', { class: 'card', onclick: fn }, h('div', { class: 'big' }, big), h('div', { class: 'nm' }, nm), h('div', { class: 'ds' }, ds));
    const cards = h('div', { class: 'cards' },
      card('📁', 'Folder Compare', 'Compare two folders recursively', () => Tabs.add(new DirView())),
      card('📄', 'Text Compare', 'Side-by-side text diff with editing', () => Tabs.add(new TextView())),
      card('🔢', 'Hex Compare', 'Byte-level comparison', () => Tabs.add(new HexView())),
      card('🔀', 'Text Merge', 'Three-way merge', () => Tabs.add(new MergeView())));
    // quick compare
    this.ql = pathBar('Left', 'file'); this.qr = pathBar('Right', 'file');
    const kind = this.kindSel = h('select', null, [['auto', 'Auto detect'], ['dir', 'Folders'], ['text', 'Text'], ['hex', 'Hex']].map(([v, t]) => h('option', { value: v }, t)));
    const go = () => {
      const l = this.ql.get(), r = this.qr.get();
      if (!l || !r) { toast('Enter both paths', 'err'); return; }
      openCompare(l, r, kind.value === 'auto' ? null : kind.value);
    };
    this.ql.inp.addEventListener('keydown', e => { if (e.key === 'Enter') go(); });
    this.qr.inp.addEventListener('keydown', e => { if (e.key === 'Enter') go(); });
    for (const p of [this.ql, this.qr]) p.el.append(h('button', { title: 'Browse for a folder…', onclick: async () => { const v = await pickPath(p.inp.value, 'dir'); if (v) p.set(v); } }, '📁'));
    const quick = h('div', { class: 'panel' }, h('h3', null, 'Quick compare'),
      h('div', { class: 'pathbar', style: { padding: 0, border: 0 } }, this.ql.el, this.qr.el, kind, h('button', { class: 'primary', onclick: go }, 'Compare')));
    // sessions
    this.sessBox = h('div', { class: 'sess' });
    this.filterIn = h('input', { type: 'text', placeholder: 'filter sessions…', oninput: e => { this.filter = e.target.value.toLowerCase(); this.renderSessions(); } });
    this.sessInfo = h('span', { class: 'muted' });
    const sessions = h('div', { class: 'panel' }, h('h3', null, 'Sessions ', this.sessInfo),
      h('div', { class: 'toolbar', style: { padding: '0 0 6px', border: 0, background: 'none' } }, this.filterIn,
        h('button', { onclick: () => this.openSel() }, 'Open'),
        h('button', { onclick: () => this.renameSel() }, 'Rename'),
        h('button', { class: 'danger', onclick: () => this.deleteSel() }, 'Delete'),
        h('button', { onclick: () => this.reload() }, '⟳')),
      this.sessBox);
    this.el.append(h('div', { class: 'home' }, h('h2', { style: { margin: '0 0 12px' } }, 'mbc - My Beyond Compare'), cards, quick, sessions));
  }
  async reload() {
    const r = await api('sessions/list');
    if (!r.ok) { this.sessBox.textContent = r.error; this.sessBox.style.padding = '8px'; return; }
    this.items = r.items; this.sessPath = r.path;
    this.sessInfo.textContent = '(' + r.items.length + ' in ' + r.path + ')';
    this.renderSessions();
  }
  renderSessions() {
    const box = this.sessBox;
    box.innerHTML = '';
    box.style.padding = '';
    const f = this.filter;
    const list = this.items.filter(s => !f || s.name.toLowerCase().includes(f) || s.left.toLowerCase().includes(f) || s.right.toLowerCase().includes(f));
    if (!list.length) { box.append(h('div', { class: 'empty' }, this.items.length ? 'No sessions match.' : 'No sessions yet. Every comparison is recorded here automatically under History; click "Session" to save one with a name.')); return; }
    const byFolder = new Map();
    for (const s of list) { const k = s.folder || ''; if (!byFolder.has(k)) byFolder.set(k, []); byFolder.get(k).push(s); }
    const icon = { dir: '📁', text: '📄', hex: '🔢', merge: '🔀' };
    const folders = [...byFolder.keys()].sort();
    for (const fo of folders) {
      if (fo) {
        const open = !this.closed.has(fo) || !!f;
        box.append(h('div', { class: 'sessrow', onclick: () => { if (this.closed.has(fo)) this.closed.delete(fo); else this.closed.add(fo); this.renderSessions(); } },
          h('span', { class: 'tw' }, open ? '▾' : '▸'), h('b', { class: 'nm', 'data-tr': '1', style: { gridColumn: '2 / -1' } }, '📂 ' + fo + ' (' + byFolder.get(fo).length + ')')));
        if (!open) continue;
      }
      for (const s of byFolder.get(fo)) {
        const row = h('div', { class: 'sessrow' + (this.selName === s.name ? ' sel' : ''), title: s.left + '\n' + s.right + (s.center ? '\n' + s.center : ''),
          onclick: () => { this.selName = s.name; this.renderSessions(); }, ondblclick: () => this.open(s),
          oncontextmenu: e => { e.preventDefault(); this.selName = s.name; this.renderSessions(); ctxMenu(e.clientX, e.clientY, [{ label: 'Open', action: () => this.open(s) }, { label: 'Rename…', action: () => this.renameSel() }, { label: 'Delete…', action: () => this.deleteSel() }]); } },
          h('span', null, icon[s.kind] || '•'), h('span', { class: 'nm', style: { paddingLeft: fo ? '16px' : '0' } }, s.name), h('span', { class: 'p' }, s.left + '  ↔  ' + s.right), h('span', { class: 'p' }, s.lastModified));
        box.append(row);
      }
    }
  }
  open(s) {
    if (s.kind === 'merge') Tabs.add(new MergeView({ left: s.left, right: s.right, base: s.center }));
    else if (s.kind === 'dir') Tabs.add(new DirView({ left: s.left, right: s.right }));
    else if (s.kind === 'hex') Tabs.add(new HexView({ left: s.left, right: s.right }));
    else Tabs.add(new TextView({ left: s.left, right: s.right }));
  }
  sel() { return this.items.find(s => s.name === this.selName); }
  openSel() { const s = this.sel(); if (!s) { toast('Select a session first'); return; } this.open(s); }
  async renameSel() {
    const s = this.sel(); if (!s) { toast('Select a session first'); return; }
    const nn = await promptBox('Rename session', 'New name', s.name);
    if (!nn || nn === s.name) return;
    const r = await api('sessions/rename', { name: s.name, newName: nn });
    if (!r.ok) toast(r.error, 'err'); else { this.selName = nn; this.reload(); }
  }
  async deleteSel() {
    const s = this.sel(); if (!s) { toast('Select a session first'); return; }
    if (!(await confirmBox('Delete session', 'Delete session "' + s.name + '"?', 'Delete', true))) return;
    const r = await api('sessions/delete', { name: s.name });
    if (!r.ok) toast(r.error, 'err'); else { this.selName = null; this.reload(); }
  }
}
const HomeTab = { view: null, reload() { if (this.view) this.view.reload(); } };

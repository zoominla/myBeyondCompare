'use strict';
// ---------------------------------------------------------------- tabs
class View {
  constructor(kind, title) {
    this.kind = kind; this.title = title; this.dirty = false;
    this.el = h('div', { class: 'view' });
  }
  onActivate() { }
  onKey(e) { return false; }
  async canClose() { return true; }
  dispose() { }
  setTitle(t) { this.title = t; Tabs.render(); }
  setDirty(d) { if (this.dirty !== d) { this.dirty = d; Tabs.render(); } }
}

const Tabs = {
  list: [], active: null,
  add(v, activate = true) {
    this.list.push(v);
    document.getElementById('views').append(v.el);
    if (activate) this.activate(v); else this.render();
    return v;
  },
  activate(v) {
    this.active = v;
    this.list.forEach(x => x.el.classList.toggle('active', x === v));
    this.render();
    v.onActivate();
  },
  async close(v) {
    if (v.kind === 'home') return;
    if (!(await v.canClose())) return;
    const i = this.list.indexOf(v);
    this.list.splice(i, 1);
    v.dispose();
    v.el.remove();
    if (this.active === v) this.activate(this.list[Math.min(i, this.list.length - 1)]);
    else this.render();
  },
  render() {
    const bar = document.getElementById('tabbar');
    bar.innerHTML = '';
    for (const v of this.list) {
      bar.append(h('div', { class: 'tab' + (v === this.active ? ' active' : '') + (v.dirty ? ' dirty' : ''), title: v.title, onclick: () => this.activate(v), onauxclick: e => { if (e.button === 1) this.close(v); } },
        h('span', { class: 't' }, v.title),
        v.kind !== 'home' ? h('span', { class: 'x', onclick: e => { e.stopPropagation(); this.close(v); } }, '✕') : null));
    }
    bar.append(h('div', { class: 'tab-quit', title: 'Stop the mbc gui server', onclick: quitServer }, '⏻ Exit'));
    bar.append(h('div', { class: 'tab-lang', title: 'Language', onclick: () => Lang.toggle() }, Lang.cur === 'zh' ? 'EN' : '中文'));
    document.title = (this.active ? Lang.t(this.active.title) + ' - ' : '') + 'mbc';
  },
};

async function quitServer() {
  const dirty = Tabs.list.filter(v => v.dirty);
  if (dirty.length && !(await confirmBox('Exit', 'There are unsaved changes in ' + dirty.length + ' tab(s). Exit anyway?', 'Exit', true))) return;
  await api('quit');
  document.body.innerHTML = '<div class="empty"><h2>mbc gui has stopped</h2>You can close this window.</div>';
}

window.addEventListener('beforeunload', e => {
  if (Tabs.list.some(v => v.dirty)) { e.preventDefault(); e.returnValue = ''; }
});
document.addEventListener('keydown', e => {
  if (document.querySelector('.modal-bg')) return;
  if (Tabs.active && Tabs.active.onKey(e)) { e.preventDefault(); return; }
  if (e.ctrlKey && e.key === 'w' && Tabs.active && Tabs.active.kind !== 'home') { e.preventDefault(); Tabs.close(Tabs.active); }
});

// Open the right kind of view for two paths (auto-detects folder / text / binary).
async function openCompare(left, right, forceKind, extra) {
  extra = extra || {};
  if (forceKind === 'merge') return Tabs.add(new MergeView(Object.assign({ left, right }, extra)));
  const r = await api('probe', { left, right });
  if (!r.ok) { toast(r.error, 'err'); return null; }
  const kind = forceKind || r.kind;
  if (kind === 'dir') return Tabs.add(new DirView(Object.assign({ left, right }, extra)));
  if (kind === 'hex') return Tabs.add(new HexView(Object.assign({ left, right }, extra)));
  return Tabs.add(new TextView(Object.assign({ left, right, forceText: forceKind === 'text' }, extra)));
}

// Automatic history: every comparison the user starts is recorded (server side) in the "History" session folder.
async function recordHistory(kind, left, right, center) {
  if (!left || !right) return;
  const r = await api('sessions/record', { kind, left, right, center: center || '' });
  if (r.ok && r.changed && window.HomeTab) HomeTab.reload();
}

// Save the current comparison as a named session
async function saveSessionDialog(kind, left, right, center) {
  const def = baseName(left) + ' <--> ' + baseName(right);
  const name = await promptBox('Save session', 'Session name', def);
  if (!name) return;
  const r = await api('sessions/save', { name, kind, left, right, center: center || '', folder: '' });
  if (!r.ok) toast(r.error, 'err'); else { toast('Session "' + name + '" saved', 'ok'); if (window.HomeTab) HomeTab.reload(); }
}

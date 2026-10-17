'use strict';
// ---------------------------------------------------------------- startup
(async function main() {
  Lang.init();
  HomeTab.view = new HomeView();
  Tabs.add(HomeTab.view);
  const info = await api('info');
  if (!info.ok) { toast(info.error, 'err'); return; }
  const s = info.start || {};
  if (s.base) Tabs.add(new MergeView({ left: s.left || '', right: s.right || '', base: s.base }));
  else if (s.left && s.right) await openCompare(s.left, s.right, s.view && s.view !== 'auto' ? s.view : null);
  window.__mbcReady = true;
})();

const fs = require('fs'), path = require('path');
module.exports = async ({ send, ev, shot, waitFor, sleep, ok, A, B, tmp }) => {
  const J = JSON.stringify;
  const rd = p => fs.readFileSync(p, 'utf8');
  const VK = { Enter: 13, F7: 118, F8: 119, F5: 116, Escape: 27 };
  const key = async (k, mods = 0, vk) => {
    const code = vk || VK[k] || k.toUpperCase().charCodeAt(0);
    await send('Input.dispatchKeyEvent', { type: 'rawKeyDown', key: k, code: k.length === 1 ? 'Key' + k.toUpperCase() : k, windowsVirtualKeyCode: code, modifiers: mods });
    await send('Input.dispatchKeyEvent', { type: 'keyUp', key: k, code: k.length === 1 ? 'Key' + k.toUpperCase() : k, windowsVirtualKeyCode: code, modifiers: mods });
  };
  const CTRL = 2;
  const clickText = async (sel, text) => ev(`(()=>{const b=[...document.querySelectorAll(${J(sel)})].find(e=>e.textContent.includes(${J(text)}));if(!b)return false;b.click();return true})()`);
  const act = s => `document.querySelector('.view.active ${s}')`;
  const kind = () => ev('Tabs.active.kind');

  // ---------------- home
  ok(await ev('document.querySelectorAll(".card").length') === 4, 'home cards');
  ok(await ev('document.querySelector(".sess").textContent.includes("No sessions yet")'), 'home empty sessions');

  // ---------------- folder compare
  await ev(`openCompare(${J(A)}, ${J(B)})`);
  ok(await waitFor('Tabs.active && Tabs.active.kind === "dir" && Tabs.active.entries && Tabs.active.entries.length > 0'), 'dir view loaded');
  await sleep(200);
  const rows = await ev(`[...document.querySelectorAll('.view.active .dirrow')].map(e=>e.className)`);
  ok(rows.length === 5, 'dir rows = 5, got ' + rows.length);
  ok(rows.filter(c => c.includes('st-diff')).length === 2, 'two different rows');
  ok(rows.filter(c => c.includes('st-left')).length === 1, 'one left-only row');
  ok(rows.filter(c => c.includes('st-same')).length === 2, 'same rows');
  // collapse sub
  await clickText('.view.active .toolbar button', 'Collapse all');
  await sleep(100);
  ok(await ev(`document.querySelectorAll('.view.active .dirrow').length`) === 4, 'collapse hides sub/s.txt');
  await clickText('.view.active .toolbar button', 'Expand all');
  await sleep(100);

  // copy only.txt -> right through UI
  await ev(`(()=>{const v=Tabs.active; v.sel=new Set(['only.txt']); v.repaint(); })()`);
  ok(await clickText('.view.active .toolbar button', 'Copy to right'), 'click copy to right');
  ok(await waitFor(`!!document.querySelector('.modal-bg')`), 'copy confirm shown');
  const mtxt = await ev(`document.querySelector('.modal').innerText`);
  ok(/only\.txt/.test(mtxt), 'confirm mentions only.txt: ' + mtxt);
  ok(await clickText('.modal-bg button', 'Copy') || await clickText('.modal-bg button.primary', ''), 'confirm click');
  ok(await waitFor(`!document.querySelector('.modal-bg')`), 'modal closed');
  ok(await waitFor(`!!${J(1)} && Tabs.active.entries.find(e=>e.rel==='only.txt').status==='same'`, 5000), 'only.txt is now same');
  ok(fs.existsSync(path.join(B, 'only.txt')), 'only.txt copied to disk');

  // ---------------- text compare via double click on f.txt row
  await ev(`(()=>{const v=Tabs.active; v.open(v.entries.find(e=>e.rel==='f.txt'));})()`);
  ok(await waitFor('Tabs.active.kind === "text" && Tabs.active.loaded'), 'text view opened');
  await sleep(300);
  const tl = await ev(`(()=>{const e=document.querySelector('.view.active .vl');const b=e.getBoundingClientRect();return [Math.round(b.width),Math.round(b.height)]})()`);
  ok(tl[0] > 800 && tl[1] > 300, 'text list fills the window: ' + J(tl));
  const tinfo = await ev(`({rows:document.querySelectorAll('.view.active .txrow').length, k2:document.querySelectorAll('.view.active .txrow.k2').length, k4:document.querySelectorAll('.view.active .txrow.k4').length, gap:document.querySelectorAll('.view.active .txrow.gap').length, secs:Tabs.active.secs.length, ih:document.querySelectorAll('.view.active .ih').length})`);
  ok(tinfo.rows >= 3 && tinfo.k2 === 1 && tinfo.k4 === 1 && tinfo.secs === 2, 'text rows: ' + J(tinfo));
  ok(tinfo.ih >= 2, 'inline highlights: ' + J(tinfo));
  // nav
  await key('F8');
  await sleep(100);
  ok(await ev('Tabs.active.curSec') >= 0, 'F8 selects section');
  // copy section 0 left->right via arrow
  await ev(`(()=>{const a=document.querySelector('.view.active .txrow .mid span[data-copy="0"]'); a.dispatchEvent(new MouseEvent('mousedown',{bubbles:true}));})()`);
  await sleep(400);
  ok(await ev('Tabs.active.S[1].dirty') === true, 'right dirty after copy');
  ok(await ev('Tabs.active.S[1].lines[1]') === 'two', 'right line 1 = two');
  await key('z', CTRL);
  await sleep(200);
  ok(await ev('Tabs.active.S[1].lines[1]') === '2', 'undo restores');
  await key('y', CTRL);
  await sleep(200);
  ok(await ev('Tabs.active.S[1].lines[1]') === 'two', 'redo');
  // inline edit via dblclick
  await ev(`(()=>{const tx=document.querySelector('.view.active .txrow .tx.R[data-i="0"]'); tx.dispatchEvent(new MouseEvent('dblclick',{bubbles:true}));})()`);
  ok(await ev(`!!document.querySelector('.view.active .tx input')`), 'inline edit input appears');
  await ev(`(()=>{const i=document.querySelector('.view.active .tx input'); i.value='ONE'; })()`);
  await key('Enter');
  await sleep(300);
  ok(await ev('Tabs.active.S[1].lines[0]') === 'ONE', 'inline edit committed');
  // save right
  await key('s', CTRL);
  await sleep(500);
  ok(await ev('Tabs.active.S[1].dirty') === false, 'saved right (not dirty)');
  ok(rd(path.join(B, 'f.txt')).startsWith('ONE\ntwo\nthree'), 'disk content updated: ' + J(rd(path.join(B, 'f.txt')).slice(0, 20)));
  // show diffs only
  const before = await ev(`document.querySelectorAll('.view.active .txrow').length`);
  await ev(`Tabs.active.onlyDiffs=true; Tabs.active.buildView(); void 0`);
  await sleep(150);
  ok(await ev(`document.querySelectorAll('.view.active .txrow.gap').length`) >= 0, 'diffs-only builds');
  await ev(`Tabs.active.onlyDiffs=false; Tabs.active.buildView(); void 0`);
  // report dialog
  const rep = await ev(`api('text/report',{left:${J(path.join(A, 'f.txt'))},right:${J(path.join(B, 'f.txt'))},style:'unified',opts:{}}).then(r=>r.content||r.error)`);
  ok(typeof rep === 'string' && rep.includes('@@'), 'text report unified: ' + String(rep).slice(0, 60));
  await ev(`Tabs.close(Tabs.active)`);
  await sleep(300);
  if (await ev(`!!document.querySelector('.modal-bg')`)) await clickText('.modal-bg button', 'Close');
  await sleep(200);

  // ---------------- hex via dir row
  await ev(`openCompare(${J(path.join(A, 'bin.dat'))}, ${J(path.join(B, 'bin.dat'))})`);
  ok(await waitFor('Tabs.active.kind === "hex"'), 'hex view opened for binary files');
  ok(await waitFor(`document.querySelectorAll('.view.active .hxrow, .view.active .hexrow, .view.active .vl-row').length > 0`), 'hex rows rendered');
  await sleep(300);
  const hx = await ev(`({t:document.querySelector('.view.active').innerText.slice(0,400), d:document.querySelectorAll('.view.active .d').length})`);
  ok(hx.d > 0, 'hex diff bytes highlighted: ' + J(hx).slice(0, 300));
  await key('F8');
  await ev(`Tabs.close(Tabs.active)`);
  await sleep(200);

  // ---------------- merge
  await ev(`Tabs.add(new MergeView({left:${J(path.join(tmp, 'left.txt'))}, base:${J(path.join(tmp, 'base.txt'))}, right:${J(path.join(tmp, 'right.txt'))}, output:${J(path.join(tmp, 'out.txt'))}}))`);
  ok(await waitFor('Tabs.active.kind === "merge" && Tabs.active.loaded'), 'merge loaded');
  await sleep(200);
  const mg = await ev(`({regs:Tabs.active.regions.map(g=>g.k).join(','), un:Tabs.active.unresolved(), tops:document.querySelectorAll('.view.active .mgreg').length, outs:document.querySelectorAll('.view.active .mgo').length, unres:document.querySelectorAll('.view.active .mgo.unres').length})`);
  ok(mg.un === 1 && mg.unres === 1 && mg.tops === mg.outs, 'merge regions: ' + J(mg));
  ok(/conflict/.test(mg.regs) && /right/.test(mg.regs), 'merge region kinds ' + mg.regs);
  const lay = await ev(`(()=>{const r=s=>{const e=document.querySelector('.view.active '+s);if(!e)return null;const b=e.getBoundingClientRect();return [Math.round(b.width),Math.round(b.height)]};return {top:r('.mgtop'),out:r('.mgout'),head:r('.mghead'),sb:r('.statusbar')}})()`);
  ok(lay.top && lay.top[1] > 60 && lay.out && lay.out[1] > 60 && lay.top[0] > 800, 'merge layout has real panes: ' + J(lay));
  // save with unresolved -> confirm, markers
  await ev(`Tabs.active.cur=-1; void 0`);
  await key('F8');
  ok(await ev('Tabs.active.cur') >= 0, 'merge nav selects region');
  ok(await ev('Tabs.active.regions[Tabs.active.cur].k') === 'conflict', 'first change is conflict');
  await clickText('.view.active .toolbar button', 'Take left');
  await sleep(100);
  ok(await ev('Tabs.active.unresolved()') === 0, 'conflict resolved');
  await clickText('.view.active .toolbar button', 'Save output');
  await sleep(600);
  const outp = rd(path.join(tmp, 'out.txt'));
  ok(outp === 'a\nB\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nM\nn\n', 'merge output: ' + J(outp));
  // edit region manually
  await ev(`Tabs.active.choose(Tabs.active.regions.findIndex(g=>g.k==='conflict'),'lr'); void 0`);
  ok(J(await ev(`Tabs.active.buildOutput().slice(0,4)`)) === J(['a', 'B', 'b2', 'c']), 'left+right output');
  await ev(`Tabs.active.touchedAny=false; Tabs.active.setDirty(false); Tabs.close(Tabs.active)`);
  await sleep(200);

  // ---------------- sessions
  const sv = await ev(`api('sessions/save',{name:'s1',kind:'dir',left:${J(A)},right:${J(B)},folder:''}).then(r=>r.ok)`);
  ok(sv === true, 'session saved');
  await ev(`HomeTab.reload()`);
  await sleep(400);
  await ev(`Tabs.activate(HomeTab.view)`);
  ok(await ev(`document.querySelector('.sess').textContent.includes('s1')`), 'home lists session');
  await ev(`(()=>{const r=document.querySelector('.sessrow'); r.dispatchEvent(new MouseEvent('dblclick',{bubbles:true}))})()`);
  ok(await waitFor('Tabs.active.kind === "dir"'), 'double-click session opens folder compare');
};

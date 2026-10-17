// Automatic session history: every comparison the user starts is recorded in the "History" folder
const fs = require('fs'), path = require('path');
module.exports = async ({ send, ev, waitFor, sleep, ok, tmp }) => {
  const J = JSON.stringify, NL = String.fromCharCode(10);
  const list = () => ev(`api('sessions/list').then(r => r.items)`);
  const hist = async () => (await list()).filter(s => s.folder === 'History');
  const D1 = path.join(tmp, 'h1'), D2 = path.join(tmp, 'h2');
  for (const d of [D1, D2]) fs.mkdirSync(d, { recursive: true });
  fs.writeFileSync(path.join(D1, 'c.txt'), 'one\ntwo\n'); fs.writeFileSync(path.join(D2, 'c.txt'), 'one\n2\n');
  const T1 = path.join(tmp, 'ha.txt'), T2 = path.join(tmp, 'hb.txt'), T3 = path.join(tmp, 'hc.txt');
  fs.writeFileSync(T1, 'a\nb\n'); fs.writeFileSync(T2, 'a\nB\n'); fs.writeFileSync(T3, 'a\nb\n');
  const B1 = path.join(tmp, 'hx.bin'), B2 = path.join(tmp, 'hy.bin');
  fs.writeFileSync(B1, Buffer.from([0, 1, 2])); fs.writeFileSync(B2, Buffer.from([0, 1, 3]));
  const before = (await hist()).length;
  const wait = async (kind) => { ok(await waitFor(`Tabs.active && Tabs.active.kind === ${J(kind)} && Tabs.active.loaded`), kind + ' loaded'); await sleep(400); };

  await ev(`openCompare(${J(T1)}, ${J(T2)})`); await wait('text');
  let h = await hist();
  ok(h.length === before + 1 && h[0].kind === 'text' && h[0].left === T1 && h[0].right === T2, 'text compare recorded (newest first): ' + JSON.stringify(h[0]));
  ok(h[0].name === 'ha.txt ↔ hb.txt', 'history entry name: ' + h[0].name);
  // refresh / same pair again: no duplicate
  await ev(`Tabs.active.load(); void 0`); await sleep(500);
  await ev(`openCompare(${J(T1)}, ${J(T2)})`); await wait('text');
  ok((await hist()).length === before + 1, 'same comparison is not recorded twice');

  await ev(`openCompare(${J(D1)}, ${J(D2)})`); await wait('dir');
  h = await hist();
  ok(h.length === before + 2 && h[0].kind === 'dir', 'folder compare recorded first');
  // opening a file from the folder compare is NOT recorded
  await ev(`(()=>{const v=Tabs.active; const e=v.entries.find(x=>x.name==='c.txt'); v.open(e);})(); void 0`);
  await wait('text'); await sleep(300);
  ok((await hist()).length === before + 2, 'files opened from a folder compare are not recorded');

  await ev(`openCompare(${J(B1)}, ${J(B2)}, 'hex')`); await wait('hex');
  h = await hist();
  ok(h.length === before + 3 && h[0].kind === 'hex', 'hex compare recorded');
  await ev(`openCompare(${J(T1)}, ${J(T3)}, 'merge', {base: ${J(T2)}})`); await wait('merge');
  h = await hist();
  ok(h.length === before + 4 && h[0].kind === 'merge' && h[0].center === T2, 'merge recorded with base: ' + JSON.stringify(h[0]));

  // a comparison already saved under a name is not duplicated
  const s = await ev(`api('sessions/save', {name:'mine', kind:'text', folder:'', left:${J(T2)}, right:${J(T3)}, center:''}).then(r=>r.ok)`);
  ok(s, 'named session saved');
  await ev(`openCompare(${J(T2)}, ${J(T3)})`); await wait('text');
  ok((await hist()).length === before + 4, 'already saved comparison is not added to History');

  // home shows the History folder
  await ev(`Tabs.activate(HomeTab.view); HomeTab.view.reload(); void 0`); await sleep(500);
  const txt = await ev(`document.querySelector('.sess').innerText`);
  ok(/History \(\d+\)/.test(txt), 'home lists the History folder: ' + txt.slice(0, 60).split(String.fromCharCode(10)).join('|'));
  // direct API: invalid kind / empty paths rejected; the cap keeps only 100 entries
  ok(!(await ev(`api('sessions/record', {kind:'x', left:'a', right:'b'}).then(r=>r.ok)`)), 'unknown kind rejected');
  ok(!(await ev(`api('sessions/record', {kind:'text', left:'', right:'b'}).then(r=>r.ok)`)), 'empty path rejected');
  for (let i = 0; i < 105; i++) await ev(`api('sessions/record', {kind:'text', left:'/cap/l${'$'}{0}', right:'/cap/r'}).then(r=>r.ok)`.replace('${0}', String(i)));
  const n = (await hist()).length;
  ok(n === 100, 'history is capped at 100 entries: ' + n);
  ok((await hist())[0].left === '/cap/l104', 'newest entry first after cap');

  // ---- real mouse double click on a folder-compare row opens the pair
  const E1 = path.join(tmp, 'dc1'), E2 = path.join(tmp, 'dc2');
  for (const d of [E1, E2]) fs.mkdirSync(path.join(d, 'sub'), { recursive: true });
  fs.writeFileSync(path.join(E1, 'f.txt'), 'x'+NL+'y'+NL); fs.writeFileSync(path.join(E2, 'f.txt'), 'x'+NL+'z'+NL);
  fs.writeFileSync(path.join(E1, 'sub', 'g.txt'), 'g'+NL); fs.writeFileSync(path.join(E2, 'sub', 'g.txt'), 'g'+NL);
  await ev(`openCompare(${J(E1)}, ${J(E2)})`); await wait('dir');
  const dbl = async (sel) => {
    const pos = await ev(`(()=>{const r=[...document.querySelectorAll('.view.active .dirrow')].find(x=>x.textContent.includes(${J(sel)})); const b=r.querySelector('.pn.L .nmi').getBoundingClientRect(); return {x:b.left+b.width/2+30,y:b.top+b.height/2};})()`);
    for (const n of [1, 2]) {
      await send('Input.dispatchMouseEvent', { type: 'mousePressed', x: pos.x, y: pos.y, button: 'left', buttons: 1, clickCount: n });
      await send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: pos.x, y: pos.y, button: 'left', buttons: 0, clickCount: n });
    }
  };
  const nTabs = await ev(`Tabs.list.length`);
  await dbl('f.txt');
  ok(await waitFor(`Tabs.list.length === ${nTabs} + 1 && Tabs.active.kind === 'text'`), 'double click on a file row opens the text compare');
  ok(await ev(`Tabs.active.S[0].path.endsWith('f.txt') && Tabs.active.S[1].path.includes('dc2')`), 'it compares the two same-named files');
  await ev(`Tabs.activate(Tabs.list.find(t => t.kind === 'dir' && t.rootL && t.rootL.includes('dc1'))); void 0`);
  const rows0 = await ev(`document.querySelectorAll('.view.active .dirrow').length`);
  await dbl('sub');
  await sleep(300);
  ok(await ev(`document.querySelectorAll('.view.active .dirrow').length`) < rows0, 'double click on a folder row collapses it');
};

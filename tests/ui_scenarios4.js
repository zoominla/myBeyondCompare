// Pane layout of folder / hex / merge views, and the folder twisty.
const fs = require('fs'), path = require('path');
module.exports = async ({ send, ev, waitFor, sleep, ok, tmp }) => {
  const J = JSON.stringify;
  const act = s => `document.querySelector('.view.active ${s}')`;
  const rect = s => ev(`(()=>{const e=${act(s)};if(!e)return null;const b=e.getBoundingClientRect();return {x:b.left,y:b.top,w:b.width,h:b.height,vis:getComputedStyle(e).display!=='none'}})()`);
  const mouse = (type, x, y) => send('Input.dispatchMouseEvent', { type, x, y, button: 'left', buttons: type === 'mouseReleased' ? 0 : 1, clickCount: 1 });
  const click = async (x, y) => { await mouse('mouseMoved', x, y); await mouse('mousePressed', x, y); await mouse('mouseReleased', x, y); };
  const near = (a, b, t = 2) => Math.abs(a - b) <= t;
  const dividerVisible = () => ev(`(()=>{const handles=[...document.querySelectorAll('.view.active .vsplit')];return handles.length>0&&handles.every(e=>{const s=getComputedStyle(e,'::after');return e.getBoundingClientRect().height>0&&s.content==='""'&&s.width==='1px'&&s.top==='0px'&&s.bottom==='0px'&&s.backgroundColor!=='rgba(0, 0, 0, 0)'&&s.pointerEvents==='none'})})()`);

  // ---------------- folder compare
  const D1 = path.join(tmp, 'pl1'), D2 = path.join(tmp, 'pl2');
  for (const d of [D1, D2]) fs.mkdirSync(path.join(d, 'sub'), { recursive: true });
  const longName = 'very_long_file_name_' + 'x'.repeat(140) + '.txt';
  fs.writeFileSync(path.join(D1, 'sub', longName), 'one');
  fs.writeFileSync(path.join(D1, 'sub', 'a.txt'), 'A');
  fs.writeFileSync(path.join(D2, 'sub', 'a.txt'), 'A');
  fs.writeFileSync(path.join(D1, 'top.txt'), 'T1');
  fs.writeFileSync(path.join(D2, 'top.txt'), 'T2');
  await ev(`openCompare(${J(D1)}, ${J(D2)})`);
  ok(await waitFor('Tabs.active && Tabs.active.kind === "dir" && Tabs.active.entries && Tabs.active.entries.length > 0'), 'pl dir loaded');
  await sleep(300);
  ok(await dividerVisible(), 'folder: permanent full-height divider line');
  const nrows = () => ev(`document.querySelectorAll('.view.active .dirrow').length`);
  ok(await nrows() === 4, 'dir rows = 4 (sub, 2 in sub, top), got ' + await nrows());
  let l = await rect('.dirrow .pn.L'), r = await rect('.dirrow .pn.R');
  ok(near(l.w, r.w, 40), `dir panes about equal (${l.w} / ${r.w})`);  // L is calc, R takes the rest incl. fixed columns
  const hd = await ev(`[...document.querySelectorAll('.view.active .dirhead .hp')].map(e=>Math.round(e.getBoundingClientRect().width))`);
  ok(near(hd[0], l.w, 2) && near(hd[1], r.w, 2), 'dir header columns match pane widths ' + J(hd) + ' vs ' + l.w + '/' + r.w);
  // real mouse click on the twisty of the folder row collapses / expands it
  const tw = await ev(`(()=>{const e=document.querySelector('.view.active .dirrow .pn.L .tw[data-tw]');if(!e)return null;const b=e.getBoundingClientRect();return {x:b.left+b.width/2,y:b.top+b.height/2}})()`);
  ok(!!tw, 'twisty exists on folder row');
  await click(tw.x, tw.y); await sleep(150);
  ok(await nrows() === 2, 'twisty click collapses folder, rows=' + await nrows());
  const tw2 = await ev(`(()=>{const e=document.querySelector('.view.active .dirrow .pn.L .tw[data-tw]');const b=e.getBoundingClientRect();return {x:b.left+b.width/2,y:b.top+b.height/2}})()`);
  await click(tw2.x, tw2.y); await sleep(150);
  ok(await nrows() === 4, 'twisty click expands folder again, rows=' + await nrows());
  // the right-hand twisty works too (folder exists on both sides)
  const twr = await ev(`(()=>{const e=document.querySelector('.view.active .dirrow .pn.R .tw[data-tw]');if(!e)return null;const b=e.getBoundingClientRect();return {x:b.left+b.width/2,y:b.top+b.height/2}})()`);
  ok(!!twr, 'right twisty exists');
  await click(twr.x, twr.y); await sleep(150);
  ok(await nrows() === 2, 'right twisty collapses');
  const twr2 = await ev(`(()=>{const e=document.querySelector('.view.active .dirrow .pn.R .tw[data-tw]');const b=e.getBoundingClientRect();return {x:b.left+b.width/2,y:b.top+b.height/2}})()`);
  await click(twr2.x, twr2.y); await sleep(150);
  ok(await nrows() === 4, 'right twisty expands');
  // scrollbars: only the left has a long name
  const hs = async () => ev(`[...document.querySelectorAll('.view.active .hsrow .hs')].map(e=>getComputedStyle(e).display!=='none'&&e.scrollWidth>e.clientWidth+1)`);
  let h = await hs();
  ok(h[0] === true && h[1] === false, 'dir: only left pane has a horizontal scrollbar ' + J(h));
  // scroll the left scrollbar, the right pane stays
  await ev(`(()=>{const e=document.querySelectorAll('.view.active .hsrow .hs')[0];e.scrollLeft=e.scrollWidth;e.dispatchEvent(new Event('scroll'))})()`);
  await sleep(100);
  const tr = await ev(`(()=>{const rows=[...document.querySelectorAll('.view.active .dirrow')];return rows.map(r=>[...r.querySelectorAll('.nmi')].map(n=>getComputedStyle(n).transform))})()`);
  ok(tr.some(x => x[0] && x[0] !== 'none'), 'left names shifted: ' + J(tr).slice(0, 120));
  ok(tr.every(x => x.length < 2 || x[1] === 'none' || /matrix\(1, 0, 0, 1, 0, 0\)/.test(x[1])), 'right names not shifted');
  // drag the divider
  const hx0 = await ev(`(()=>{const b=document.querySelector('.view.active .vsplit').getBoundingClientRect();return {x:b.left+b.width/2,y:b.top+40}})()`);
  const w0 = (await rect('.dirrow .pn.L')).w;
  await mouse('mouseMoved', hx0.x, hx0.y); await mouse('mousePressed', hx0.x, hx0.y);
  await mouse('mouseMoved', hx0.x - 120, hx0.y); await mouse('mouseReleased', hx0.x - 120, hx0.y);
  await sleep(100);
  const w1 = (await rect('.dirrow .pn.L')).w;
  ok(near(w1, w0 - 120, 6), `dir divider drag left shrinks left (${w0} -> ${w1})`);
  await ev(`Tabs.close(Tabs.active)`); await sleep(150);

  // ---------------- hex
  const H1 = path.join(tmp, 'pl1.bin'), H2 = path.join(tmp, 'pl2.bin');
  const buf = Buffer.alloc(3000); for (let i = 0; i < buf.length; i++) buf[i] = (i * 7) & 255;
  fs.writeFileSync(H1, buf); buf[1500] ^= 0xff; fs.writeFileSync(H2, buf);
  await ev(`openCompare(${J(H1)}, ${J(H2)})`);
  ok(await waitFor('Tabs.active.kind === "hex"'), 'pl hex opened');
  ok(await waitFor(`document.querySelectorAll('.view.active .hexrow .pn.L').length > 3`), 'pl hex rows');
  await sleep(300);
  ok(await dividerVisible(), 'hex: permanent full-height divider line');
  const hl = await rect('.hexrow:not(.hexhead) .pn.L'), hr = await rect('.hexrow:not(.hexhead) .pn.R');
  ok(near(hl.w, hr.w - 3, 3), `hex panes equal (${hl.w} / ${hr.w})`);
  const hh = await ev(`[...document.querySelectorAll('.view.active .hexhead .pn')].map(e=>Math.round(e.getBoundingClientRect().width))`);
  ok(near(hh[0], hl.w, 2) && near(hh[1], hr.w, 2), 'hex header aligned ' + J(hh));
  // shrink the left pane a lot -> left scrollbar, right still fits (is wide)
  const hx1 = await ev(`(()=>{const b=document.querySelector('.view.active .vsplit').getBoundingClientRect();return {x:b.left+b.width/2,y:b.top+60}})()`);
  await mouse('mouseMoved', hx1.x, hx1.y); await mouse('mousePressed', hx1.x, hx1.y);
  await mouse('mouseMoved', hx1.x - 300, hx1.y); await mouse('mouseReleased', hx1.x - 300, hx1.y);
  await sleep(150);
  h = await hs();
  ok(h[0] === true, 'hex: left pane gets a horizontal scrollbar when narrowed ' + J(h));
  const hl2 = await rect('.hexrow:not(.hexhead) .pn.L');
  ok(hl2.w < hl.w - 200, `hex left pane shrank (${hl.w} -> ${hl2.w})`);
  await ev(`(()=>{const e=document.querySelectorAll('.view.active .hsrow .hs')[0];e.scrollLeft=e.scrollWidth;e.dispatchEvent(new Event('scroll'))})()`);
  await sleep(100);
  const asc = await ev(`(()=>{const e=document.querySelector('.view.active .hexrow:not(.hexhead) .pn.L .asc').getBoundingClientRect();const p=document.querySelector('.view.active .hexrow:not(.hexhead) .pn.L').getBoundingClientRect();return {r:e.right,pr:p.right}})()`);
  ok(near(asc.r, asc.pr, 3), `hex left ASCII column reachable by scrolling (${asc.r} vs ${asc.pr})`);
  await ev(`Tabs.close(Tabs.active)`); await sleep(150);

  // ---------------- merge
  const base = ['a', 'b', 'c', 'd'].join('\n') + '\n';
  const longL = 'L'.repeat(300);
  fs.writeFileSync(path.join(tmp, 'pm_base.txt'), base);
  fs.writeFileSync(path.join(tmp, 'pm_left.txt'), ['a', longL, 'c', 'd'].join('\n') + '\n');
  fs.writeFileSync(path.join(tmp, 'pm_right.txt'), ['a', 'b', 'c', 'D'].join('\n') + '\n');
  await ev(`Tabs.add(new MergeView({left:${J(path.join(tmp, 'pm_left.txt'))}, base:${J(path.join(tmp, 'pm_base.txt'))}, right:${J(path.join(tmp, 'pm_right.txt'))}, output:${J(path.join(tmp, 'pm_out.txt'))}}))`);
  ok(await waitFor('Tabs.active.kind === "merge" && Tabs.active.loaded'), 'pm merge loaded');
  await sleep(400);
  ok(await dividerVisible(), 'merge: permanent full-height divider lines');
  const cw = await ev(`(()=>{const g=document.querySelector('.view.active .mgreg');return [...g.children].map(e=>Math.round(e.getBoundingClientRect().width))})()`);
  ok(cw.length === 3 && near(cw[0], cw[1], 2) && near(cw[1], cw[2], 3), 'merge: 3 equal columns ' + J(cw));
  const mh = await ev(`[...document.querySelectorAll('.view.active .mghead > *')].map(e=>Math.round(e.getBoundingClientRect().width))`);
  ok(mh.length >= 3 && near(mh[0], cw[0], 2) && near(mh[1], cw[1], 2), 'merge: header aligned ' + J(mh) + ' vs ' + J(cw));
  h = await hs();
  ok(h.length === 3 && h[0] === true && h[1] === false && h[2] === false, 'merge: only left column scrolls ' + J(h));
  const hv = await ev(`[...document.querySelectorAll('.view.active .vsplit')].length`);
  ok(hv === 2, 'merge: two dividers, ' + hv);
  const m0 = await ev(`(()=>{const b=document.querySelectorAll('.view.active .vsplit')[0].getBoundingClientRect();return {x:b.left+b.width/2,y:b.top+20}})()`);
  await mouse('mouseMoved', m0.x, m0.y); await mouse('mousePressed', m0.x, m0.y);
  await mouse('mouseMoved', m0.x + 100, m0.y); await mouse('mouseReleased', m0.x + 100, m0.y);
  await sleep(150);
  const cw2 = await ev(`(()=>{const g=document.querySelector('.view.active .mgreg');return [...g.children].map(e=>Math.round(e.getBoundingClientRect().width))})()`);
  ok(near(cw2[0], cw[0] + 100, 6) && cw2[1] < cw[1] - 50, `merge: divider drag right grows left, shrinks middle (${J(cw)} -> ${J(cw2)})`);
  await ev(`Tabs.close(Tabs.active)`); await sleep(150);
};

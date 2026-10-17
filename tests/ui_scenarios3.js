const fs = require('fs'), path = require('path');
module.exports = async ({ send, ev, waitFor, sleep, ok, tmp }) => {
  const J = JSON.stringify;
  const long1 = 'L' + 'abcdefghij'.repeat(80) + 'END_LEFT';
  const long2 = 'R' + '0123456789'.repeat(3) + 'END_RIGHT';
  const lines1 = ['short', long1, 'same', 'x\ty'], lines2 = ['short', long2, 'same', 'x\tz'];
  fs.writeFileSync(path.join(tmp, 'w1.txt'), lines1.join('\n') + '\n');
  fs.writeFileSync(path.join(tmp, 'w2.txt'), lines2.join('\n') + '\n');
  await ev(`Tabs.close(Tabs.active); void 0`);
  await sleep(200);
  await ev(`openCompare(${J(path.join(tmp, 'w1.txt'))}, ${J(path.join(tmp, 'w2.txt'))})`);
  ok(await waitFor('Tabs.active.kind === "text" && Tabs.active.loaded'), 'wide files loaded');
  await sleep(400);
  const geo = () => ev(`(()=>{
    const q=s=>document.querySelector('.view.active '+s), r=e=>{const b=e.getBoundingClientRect();return {x:b.left,w:b.width,r:b.right}};
    const row=q('.txrow');
    return {L:r(row.querySelector('.pn.L')),R:r(row.querySelector('.pn.R')),mid:r(row.querySelector('.mid')),
      hL:r(q('.txhead').children[0]),hR:r(q('.txhead').children[2]),
      hsL:r(q('.hs')),hsR:r(q('.hs:last-child')),hsRowShown:getComputedStyle(q('.hsrow')).display!=='none',
      hsVis:[...document.querySelectorAll('.view.active .hs')].map(e=>e.style.visibility),
      vs:r(q('.vsplit')), body:r(q('.vl'))}})()`);
  let g = await geo();
  ok(Math.abs(g.L.w - g.R.w) < 1.5, 'panes have equal width: ' + g.L.w + ' / ' + g.R.w);
  ok(Math.abs(g.hL.w - g.L.w) < 1.5 && Math.abs(g.hR.w - g.R.w) < 1.5, 'headers align with panes: ' + J([g.hL.w, g.hR.w]));
  ok(g.hsRowShown && g.hsVis[0] === 'visible', 'left pane has its own horizontal scrollbar');
  ok(g.hsVis[1] === 'hidden', 'right pane (short lines) has no scrollbar: ' + J(g.hsVis));
  ok(Math.abs(g.mid.x - g.L.r) < 1.5 && g.vs.x < g.mid.x + 27 && g.vs.r > g.mid.x + 27, 'splitter sits on the middle column');
  ok(await ev(`(()=>{const e=document.querySelector('.view.active .vsplit'), s=getComputedStyle(e,'::after');return s.content==='""'&&s.width==='1px'&&s.top==='0px'&&s.bottom==='0px'&&s.backgroundColor!=='rgba(0, 0, 0, 0)'&&s.pointerEvents==='none'})()`), 'text: permanent full-height divider line without blocking dragging');

  // horizontal scrolling is independent and reveals the hidden end of the line
  const tail = side => ev(`(()=>{const row=document.querySelector('.view.active .vl-row:nth-child(2)'); const rows=[...document.querySelectorAll('.view.active .txrow')].find(r=>r.textContent.includes('END_LEFT')); const tx=rows.querySelector('.tx.${side} '); const c=tx.querySelector('.c'); const a=c.getBoundingClientRect(), b=tx.getBoundingClientRect(); return {c0:a.left-b.left, cr:a.right-b.right}})()`);
  const t0 = await tail('L');
  ok(t0.c0 >= -0.5 && t0.cr > 50, 'left line initially clipped at the right: ' + J(t0));
  await ev(`(()=>{const hs=document.querySelector('.view.active .hs'); hs.scrollLeft=hs.scrollWidth;})()`);
  await sleep(200);
  const t1 = await tail('L');
  ok(t1.cr <= 1 && t1.cr >= -14, 'scrolled to the end shows the end of the long line: ' + J(t1));
  const rt = await tail('R');
  ok(Math.abs(rt.c0) < 0.5, 'right pane did not move: ' + J(rt));
  // shift+wheel over the left pane scrolls only that pane
  await ev(`(()=>{const hs=document.querySelector('.view.active .hs'); hs.scrollLeft=0;})()`);
  await sleep(100);
  const pos = await ev(`(()=>{const r=document.querySelector('.view.active .pn.L .tx').getBoundingClientRect(); return {x:r.left+50,y:r.top+8}})()`);
  await send('Input.dispatchMouseEvent', { type: 'mouseWheel', x: pos.x, y: pos.y, deltaX: 120, deltaY: 0 });
  await sleep(200);
  ok(await ev(`document.querySelector('.view.active .hs').scrollLeft`) > 50, 'horizontal wheel scrolls the left pane');
  await ev(`(()=>{const hs=document.querySelector('.view.active .hs'); hs.scrollLeft=0;})()`);

  // drag the divider left: left shrinks, right grows by the same amount
  const drag = async (dx) => {
    const m = await ev(`(()=>{const r=document.querySelector('.view.active .vsplit').getBoundingClientRect(); return {x:r.left+r.width/2,y:r.top+r.height/2}})()`);
    await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: m.x, y: m.y });
    await send('Input.dispatchMouseEvent', { type: 'mousePressed', x: m.x, y: m.y, button: 'left', clickCount: 1 });
    for (let i = 1; i <= 5; i++) await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: m.x + dx * i / 5, y: m.y, button: 'left', buttons: 1 });
    await send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: m.x + dx, y: m.y, button: 'left', clickCount: 1 });
    await sleep(200);
  };
  const g0 = await geo();
  await drag(-150);
  const g1 = await geo();
  ok(Math.abs((g0.L.w - g1.L.w) - 150) < 3, 'drag left shrinks left pane by 150: ' + (g0.L.w - g1.L.w));
  ok(Math.abs((g1.R.w - g0.R.w) - 150) < 3, 'drag left grows right pane by 150: ' + (g1.R.w - g0.R.w));
  ok(Math.abs(g1.L.w + g1.R.w + 54 - (g0.L.w + g0.R.w + 54)) < 1.5, 'total width constant');
  ok(Math.abs(g1.hL.w - g1.L.w) < 1.5 && Math.abs(g1.hR.w - g1.R.w) < 1.5, 'headers follow the divider');
  ok(g1.hsVis[0] === 'visible', 'left scrollbar remains');
  await drag(300);
  const g2 = await geo();
  ok(Math.abs((g2.L.w - g1.L.w) - 300) < 3 && Math.abs((g1.R.w - g2.R.w) - 300) < 3, 'drag right grows left and shrinks right by 300: ' + J([g2.L.w - g1.L.w, g1.R.w - g2.R.w]));
  await drag(5000);
  const g3 = await geo();
  ok(g3.R.w >= 60 && g3.L.w > g2.L.w, 'divider is clamped, right pane keeps a minimum width: ' + g3.R.w);
  ok(g3.hsVis[1] === 'visible', 'right pane scrollbar appears when its text no longer fits: ' + J(g3.hsVis));
  await ev(`(()=>{const hs=[...document.querySelectorAll('.view.active .hs')]; hs[0].scrollLeft=0; hs[1].scrollLeft=hs[1].scrollWidth;})()`);
  await sleep(200);
  const rr = await ev(`(()=>{const rw=[...document.querySelectorAll('.view.active .txrow')].find(r=>r.textContent.includes('END_RIGHT')); const t=s=>rw.querySelector('.tx.'+s+' .c').getBoundingClientRect().left-rw.querySelector('.tx.'+s).getBoundingClientRect().left; return [t('L'),t('R')]})()`);
  ok(rr[0] >= -0.5 && rr[1] < -5, 'right scrollbar moves only the right pane: ' + J(rr));
  // dblclick on the divider resets to 50/50
  const m = await ev(`(()=>{const r=document.querySelector('.view.active .vsplit'); r.dispatchEvent(new MouseEvent('dblclick',{bubbles:true})); const row=document.querySelector('.view.active .txrow'); return [row.querySelector('.pn.L').getBoundingClientRect().width,row.querySelector('.pn.R').getBoundingClientRect().width]})()`);
  ok(Math.abs(m[0] - m[1]) < 1.5, 'double-click resets to equal panes: ' + J(m));
  // line numbers and arrows are still wired up after the restructure
  const w = await ev(`({ln:document.querySelectorAll('.view.active .txrow .ln').length>0, copy:document.querySelectorAll('.view.active [data-copy]').length>0})`);
  ok(w.ln && w.copy, 'line numbers and copy arrows present');
  // narrowing a window keeps panes equal
  await send('Emulation.setDeviceMetricsOverride', { width: 700, height: 600, deviceScaleFactor: 1, mobile: false });
  await sleep(400);
  const gn = await geo();
  ok(Math.abs(gn.L.w - gn.R.w) < 1.5, 'equal after window resize: ' + J([gn.L.w, gn.R.w]));
  await send('Emulation.clearDeviceMetricsOverride');
  await sleep(300);
  await ev(`Tabs.close(Tabs.active); void 0`);
};

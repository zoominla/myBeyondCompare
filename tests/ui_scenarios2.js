const fs = require('fs'), path = require('path');
module.exports = async ({ send, ev, shot, waitFor, sleep, ok, A, B, tmp }) => {
  const J = JSON.stringify;
  const rd = p => fs.readFileSync(p, 'utf8');
  const CTRL = 2;
  const key = async (k, mods = 0, vk) => {
    const code = vk || { Enter: 13, F7: 118, F8: 119, F5: 116, Escape: 27 }[k] || k.toUpperCase().charCodeAt(0);
    await send('Input.dispatchKeyEvent', { type: 'rawKeyDown', key: k, code: k.length === 1 ? 'Key' + k.toUpperCase() : k, windowsVirtualKeyCode: code, modifiers: mods });
    await send('Input.dispatchKeyEvent', { type: 'keyUp', key: k, code: k.length === 1 ? 'Key' + k.toUpperCase() : k, windowsVirtualKeyCode: code, modifiers: mods });
  };
  const clickText = async (sel, text) => ev(`(()=>{const b=[...document.querySelectorAll(${J(sel)})].find(e=>e.textContent.includes(${J(text)}));if(!b)return false;b.click();return true})()`);
  const closeActive = async () => { await ev('Tabs.close(Tabs.active)'); await sleep(200); if (await ev(`!!document.querySelector('.modal-bg')`)) { await clickText('.modal-bg button', 'Close'); await sleep(150); } };

  fs.writeFileSync(path.join(tmp, 'c1.txt'), 'Hello\nWorld\nsame\n');
  fs.writeFileSync(path.join(tmp, 'c2.txt'), 'hello\nWorld\nsame\nX\n');
  // ---- text options: ignore case
  await ev(`openCompare(${J(path.join(tmp, 'c1.txt'))}, ${J(path.join(tmp, 'c2.txt'))})`);
  ok(await waitFor('Tabs.active.kind === "text" && Tabs.active.loaded'), 'c1/c2 loaded');
  await sleep(250);
  ok(await ev(`document.querySelectorAll('.view.active .txrow.k2').length`) === 1, 'case diff is a Different row');
  await ev(`(()=>{const b=[...document.querySelectorAll('.view.active .toolbar button')].find(e=>/Rules/.test(e.textContent)); b.click();})()`);
  ok(await waitFor(`!!document.querySelector('.popover')`), 'rules popover opens');
  await ev(`(()=>{const l=[...document.querySelectorAll('.popover label')].find(e=>/Ignore case/.test(e.textContent)); l.querySelector('input').click(); [...document.querySelectorAll('.popover button')].find(e=>e.textContent==='Apply').click();})()`);
  await sleep(500);
  ok(await ev(`document.querySelectorAll('.view.active .txrow.k2').length`) === 0, 'ignore case removes the Different row');
  ok(await ev(`document.querySelectorAll('.view.active .txrow.k4').length`) === 1, 'extra line remains right-only');
  await ev(`Settings.set('textOpts', Object.assign(Tabs.active.opts, {ignoreCase:false}))`);
  // ---- find
  await key('f', CTRL);
  await sleep(100);
  ok(await ev(`Tabs.active.findBar.style.display`) === 'flex', 'Ctrl+F opens find bar');
  await ev(`(()=>{const i=Tabs.active.findInp; i.value='world'; i.dispatchEvent(new Event('input'));})()`);
  ok(await ev('Tabs.active.matches.length') === 2, 'find matches both sides (case-insensitive)');
  await key('Escape');
  // ---- session dialog from text view
  await clickText('.view.active .toolbar button', 'Session');
  ok(await waitFor(`!!document.querySelector('.modal-bg input')`), 'session name prompt');
  await ev(`(()=>{const i=document.querySelector('.modal-bg input'); i.value='my text sess';})()`);
  ok(await clickText('.modal-bg button', 'OK') || await clickText('.modal-bg button', 'Save'), 'confirm session name');
  await sleep(500);
  const sl = await ev(`api('sessions/list').then(r=>r.items.map(s=>s.name+':'+s.kind))`);
  ok(sl.includes('my text sess:text'), 'text session saved: ' + J(sl));
  // ---- text report
  await closeActive();

  // ---- text compare colours: one-sided blocks (red / blue for comments) and changed lines
  const rgb = n => ev(`(()=>{const h=getComputedStyle(document.documentElement).getPropertyValue(${J(n)}).trim(); if(!/^#/.test(h))return h; const v=parseInt(h.slice(1),16); return 'rgb('+((v>>16)&255)+', '+((v>>8)&255)+', '+(v&255)+')'})()`);
  const cs = (sel, prop) => ev(`(()=>{const e=document.querySelector('.view.active ' + ${J(sel)}); return e ? getComputedStyle(e)[${J(prop)}] : null})()`);
  const cAddBg = await rgb('--add-bg'), cAddFg = await rgb('--add-fg'), cCmtBg = await rgb('--cmt-bg'), cCmtFg = await rgb('--cmt-fg');
  const cText = await rgb('--text'), cDiffBg = await rgb('--diff-bg'), cDiffFg = await rgb('--diff-fg'), cInl = await rgb('--diff-inl');
  fs.writeFileSync(path.join(tmp, 'co1.txt'), 'a\nkeep\nvalue = 1;\n');
  fs.writeFileSync(path.join(tmp, 'co2.txt'), 'a\nkeep\nvalue = 2;\nadded line\n// note only right\n');
  await ev(`openCompare(${J(path.join(tmp, 'co1.txt'))}, ${J(path.join(tmp, 'co2.txt'))})`);
  ok(await waitFor('Tabs.active.kind === "text" && Tabs.active.loaded'), 'colour fixture loaded');
  await sleep(250);
  ok(await cs('.txrow.k2 .tx.has', 'backgroundColor') === cDiffBg && await cs('.txrow.k2 .tx.has', 'color') === cText, 'changed row: light red background, unchanged text keeps normal colour');
  ok(await cs('.txrow.k2 .ih', 'color') === cDiffFg && await cs('.txrow.k2 .ih', 'backgroundColor') === cInl, 'changed row: differing parts are red');
  ok(await cs('.txrow.k4 .tx.R', 'color') === cAddFg && await cs('.txrow.k4 .tx.R', 'backgroundColor') === cAddBg, 'right-only block: light red text and background');
  ok(/linear-gradient\(to right/.test(await cs('.txrow.k4 .tx.R', 'backgroundImage')), 'right-only block fades to the right');
  ok(await cs('.txrow.k4 .tx.L', 'backgroundColor') !== cAddBg, 'left side of a right-only block stays as filler');
  const cmtCol = await ev(`(()=>{const r=[...document.querySelectorAll('.view.active .txrow.k4')].find(x=>/note only right/.test(x.textContent)); const t=r && r.querySelector('.tx.R.cmt'); return t ? getComputedStyle(t).color + '|' + getComputedStyle(t).backgroundColor : null})()`);
  ok(cmtCol === cCmtFg + '|' + cCmtBg, 'comment-only block is blue: ' + J(cmtCol));
  await closeActive();

  // ---- dir: sync / delete / report
  fs.writeFileSync(path.join(A, 'new.txt'), 'new\n');
  await ev(`openCompare(${J(A)}, ${J(B)})`);
  ok(await waitFor('Tabs.active.kind === "dir" && Tabs.active.loaded'), 'dir reopened');
  await sleep(300);
  await clickText('.view.active .toolbar button', 'Sync');
  ok(await waitFor(`!!document.querySelector('.modal-bg select')`), 'sync dialog');
  await clickText('.modal-bg button', 'Preview');
  ok(await waitFor(`!!document.querySelector('.modal-bg') && /new\\.txt/.test(document.querySelector('.modal').innerText)`), 'sync preview lists new.txt');
  await clickText('.modal-bg button', 'Cancel');
  await sleep(150);
  ok(!fs.existsSync(path.join(B, 'new.txt')), 'cancel leaves right untouched');
  await clickText('.view.active .toolbar button', 'Sync');
  await waitFor(`!!document.querySelector('.modal-bg select')`);
  await clickText('.modal-bg button', 'Preview');
  await waitFor(`!!document.querySelector('.modal-bg') && /new\\.txt/.test(document.querySelector('.modal').innerText)`);
  await clickText('.modal-bg button', 'Synchronize');
  await sleep(700);
  ok(fs.existsSync(path.join(B, 'new.txt')), 'sync copied new.txt');
  // delete from right
  await ev(`(()=>{const v=Tabs.active; v.sel=new Set(['new.txt']); v.repaint();})()`);
  await clickText('.view.active .toolbar button', 'Delete');
  await waitFor(`!!document.querySelector('.ctxmenu, .modal-bg')`);
  const menu = await ev(`!!document.querySelector('.ctxmenu')`);
  if (menu) { await ev(`[...document.querySelectorAll('.ctxmenu *')].find(e=>/right/i.test(e.textContent)&&e.children.length===0).click()`); }
  await waitFor(`!!document.querySelector('.modal-bg')`);
  await clickText('.modal-bg button', 'Delete');
  await sleep(700);
  ok(!fs.existsSync(path.join(B, 'new.txt')), 'delete removed new.txt from right: menu=' + menu);
  ok(fs.existsSync(path.join(A, 'new.txt')), 'left new.txt still exists');
  // report (capture download)
  await ev(`window.__dl=null; window.download=function(n,c,m){window.__dl={n,c,m}}; void 0`);
  await clickText('.view.active .toolbar button', 'Report');
  await waitFor(`!!document.querySelector('.modal-bg select')`);
  await clickText('.modal-bg button', 'Save report');
  ok(await waitFor('!!window.__dl'), 'report downloaded');
  const dl = await ev('window.__dl');
  ok(dl && dl.n.endsWith('.html') && dl.c.includes('f.txt'), 'html report content ' + J(dl && dl.n));
  // show filter popover
  await ev(`(()=>{const b=[...document.querySelectorAll('.view.active .toolbar button')].find(e=>/Show/.test(e.textContent)); b.click();})()`);
  ok(await waitFor(`!!document.querySelector('.popover')`), 'show popover');
  await key('Escape');
  await ev(`document.querySelectorAll('.popover').forEach(x=>x.remove()); void 0`);
  await closeActive();

  // ---- hex goto
  const big = Buffer.alloc(40000); for (let i = 0; i < big.length; i++) big[i] = i & 255;
  const big2 = Buffer.from(big); big2[30000] ^= 0xff;
  fs.writeFileSync(path.join(tmp, 'big1.bin'), big); fs.writeFileSync(path.join(tmp, 'big2.bin'), big2);
  await ev(`openCompare(${J(path.join(tmp, 'big1.bin'))}, ${J(path.join(tmp, 'big2.bin'))})`);
  ok(await waitFor('Tabs.active.kind === "hex"'), 'big hex opens');
  await sleep(400);
  await ev(`(()=>{const v=Tabs.active; v.goto.value='0x7530'; v.gotoOffset('0x7530');})()`);
  await sleep(600);
  const hd = await ev(`document.querySelectorAll('.view.active .d').length`);
  ok(hd >= 1, 'goto 0x7530 shows differing byte, d=' + hd);
  await closeActive();

  // ---- picker
  await ev(`window.__pick = pickPath(${J(tmp)}, 'file'); void 0`);
  ok(await waitFor(`!!document.querySelector('.modal-bg') && document.querySelector('.modal-bg').innerText.includes('c1.txt')`), 'picker lists files');
  await ev(`(()=>{const e=[...document.querySelectorAll('.modal-bg *')].find(x=>x.children.length===0 && x.textContent.trim().endsWith('c1.txt')); e.dispatchEvent(new MouseEvent('dblclick',{bubbles:true})); e.click();})()`);
  await sleep(200);
  if (await ev(`!!document.querySelector('.modal-bg')`)) await clickText('.modal-bg button', 'OK') || await clickText('.modal-bg button', 'Select') || await clickText('.modal-bg button', 'Open');
  await sleep(200);
  const picked = await ev(`Promise.race([window.__pick, new Promise(r=>setTimeout(()=>r('TIMEOUT'),1500))])`);
  ok(typeof picked === 'string' && picked.endsWith('c1.txt'), 'picker returns path: ' + picked);
  if (await ev(`!!document.querySelector('.modal-bg')`)) { await key('Escape'); }
};

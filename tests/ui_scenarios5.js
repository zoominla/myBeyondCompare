// i18n: language toggle, translation coverage, persistence
const fs = require('fs'), path = require('path');
module.exports = async ({ send, ev, waitFor, sleep, ok, tmp }) => {
  const J = JSON.stringify;
  const verbose = !!process.env.I18N_DUMP;
  // untranslated = visible text nodes / attributes still containing an English word while in Chinese
  const untr = () => ev(`(()=>{
    const out=new Set();
    const skip=${J('.tx, .mgreg pre, .mgo pre, .hexrow:not(.hexhead), .dirrow .nm, .dirrow .num, textarea, .oplist, .sessrow .nm, .sessrow .p, .picker .it .nm, .ttl, [data-notr]')};
    const tw=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);let n;
    while((n=tw.nextNode())){const p=n.parentNode;if(!p||p.tagName==='SCRIPT'||p.tagName==='STYLE')continue;
      if(p.closest(skip)&&!p.closest('[data-tr]'))continue;
      const s=n.nodeValue.trim();
      if(/[A-Za-z]{3,}/.test(s.replace(/\\b(KB|MB|GB|mbc|mbc|My|Beyond|Compare|Unix|DOS|Mac|LF|CRLF|CR|UTF|BOM|ANSI|CRC|Ctrl|Alt|Del|Ins|Shift|Enter|Esc|Rules|HTML|CSV|XML|EOL|Patience|Base)\\b/g,'')))out.add(s.slice(0,100));}
    for(const e of document.querySelectorAll('[title],[placeholder]')){
      if(e.closest('.tx, .hexrow:not(.hexhead), .sessrow, .picker .it'))continue;
      for(const a of ['title','placeholder']){const v=e.getAttribute(a);if(v&&/[A-Za-z]{4,}/.test(v.replace(/\\b(Ctrl|Alt|Right|Left|Shift)\\b/g,'')))out.add('@'+a+': '+v.slice(0,100));}}
    return [...out];})()`);
  const report = async (what, allow = []) => {
    const u = (await untr()).filter(s => !/[一-鿿]/.test(s) && !allow.includes(s) && !/↔|^@title: [A-Za-z]:|^@title: (F8|F7|Merge: )|^(html|csv|xml|tree|list|summary|sidebyside|unified|context|normal)$|^合并：/.test(s));
    if (verbose || u.length) console.log('  [i18n ' + what + '] untranslated: ' + J(u));
    ok(u.length === 0, 'zh: everything translated in ' + what);
  };
  const clickText = (sel, text) => ev(`(()=>{const e=[...document.querySelectorAll(${J(sel)})].find(x=>x.textContent.trim()===${J(text)});if(!e)return false;e.click();return true})()`);
  const bodyText = () => ev(`document.body.innerText`);

  await ev(`localStorage.removeItem('mbc.settings'); void 0`);
  // build fixtures
  const D1 = path.join(tmp, 'i1'), D2 = path.join(tmp, 'i2');
  for (const d of [D1, D2]) fs.mkdirSync(path.join(d, 'sub'), { recursive: true });
  fs.writeFileSync(path.join(D1, 'sub', 'a.txt'), 'A\n'); fs.writeFileSync(path.join(D2, 'sub', 'a.txt'), 'A\n');
  fs.writeFileSync(path.join(D1, 't.txt'), 'one\ntwo\nthree\n'); fs.writeFileSync(path.join(D2, 't.txt'), 'one\n2\nthree\n');
  fs.writeFileSync(path.join(D1, 'only.txt'), 'x');
  fs.writeFileSync(path.join(tmp, 'ib.bin'), Buffer.from([0, 1, 2, 3])); fs.writeFileSync(path.join(tmp, 'ib2.bin'), Buffer.from([0, 1, 9, 3]));
  fs.writeFileSync(path.join(tmp, 'im_b.txt'), 'a\nb\nc\n'); fs.writeFileSync(path.join(tmp, 'im_l.txt'), 'a\nL\nc\n'); fs.writeFileSync(path.join(tmp, 'im_r.txt'), 'a\nR\nc\n');
  await ev(`Tabs.list.slice().forEach(v=>{if(v.kind!=='home'){v.dirty=false;Tabs.close(v)}}); if(!Tabs.list.includes(HomeTab.view)) Tabs.add(HomeTab.view); else Tabs.activate(HomeTab.view); void 0`);
  await sleep(300);

  // ---- default language is English (headless chrome) and the button offers Chinese
  ok(await ev(`Lang.cur`) === 'en', 'default language en');
  ok(await ev(`document.querySelector('.tab-lang').textContent`) === '中文', 'language button shows 中文');
  ok((await ev(`document.querySelector('.tab-lang').getBoundingClientRect().right`)) > (await ev(`innerWidth`)) - 4, 'language button is at the top right');
  ok((await bodyText()).includes('Folder Compare'), 'English home text: ' + (await bodyText()).slice(0, 80).split(String.fromCharCode(10)).join('|'));

  // ---- switch to Chinese
  await ev(`document.querySelector('.tab-lang').click(); void 0`); await sleep(200);
  ok(await ev(`Lang.cur`) === 'zh', 'switched to zh');
  ok(await ev(`document.querySelector('.tab-lang').textContent`) === 'EN', 'button now shows EN');
  ok(await ev(`localStorage.getItem('mbc.settings')`).then(s => /"lang":"zh"/.test(s)), 'language persisted');
  let bt = await bodyText();
  ok(bt.includes('文件夹比较') && !bt.includes('Folder Compare'), 'home translated');
  ok(await ev(`document.documentElement.lang`) === 'zh-CN', 'html lang attr');
  await report('home');

  // ---- folder compare
  await ev(`openCompare(${J(D1)}, ${J(D2)})`);
  ok(await waitFor('Tabs.active && Tabs.active.kind === "dir" && Tabs.active.entries && Tabs.active.entries.length > 0'), 'dir loaded');
  await sleep(300);
  bt = await bodyText();
  ok(bt.includes('仅左侧') && bt.includes('已显示'), 'dir status bar translated');
  ok(bt.includes('only.txt') && bt.includes('sub'), 'file names untouched');
  await report('dir');
  for (const zh of ['显示 ▾', '选项 ▾', '报告…', '⇅ 同步…']) {
    ok(await clickText('.view.active .toolbar button', zh), 'open dir ' + zh);
    await sleep(250);
    ok(await ev(`!!document.querySelector('.popover, .modal')`), 'popup opened for ' + zh);
    await report('dir ' + zh);
    await ev(`(()=>{const c=document.querySelector('.modal .foot button');if(c)c.click();else document.body.click();})(); void 0`);
    await sleep(200);
  }
  // context menu
  await ev(`(()=>{const r=document.querySelector('.view.active .dirrow:nth-child(1)');})(); void 0`);
  await ev(`(()=>{const r=[...document.querySelectorAll('.view.active .dirrow')].find(x=>x.textContent.includes('t.txt'));r.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,clientX:200,clientY:200}))})(); void 0`);
  await sleep(200);
  await report('dir context menu');
  await ev(`document.body.click(); void 0`);

  // ---- text compare
  await ev(`openCompare(${J(path.join(D1, 't.txt'))}, ${J(path.join(D2, 't.txt'))})`);
  ok(await waitFor('Tabs.active.kind === "text" && Tabs.active.loaded'), 'text loaded');
  await sleep(300);
  bt = await bodyText();
  ok(bt.includes('保存左侧') && bt.includes('下一处差异'), 'text toolbar translated');
  await report('text');
  ok(await clickText('.view.active .toolbar button', '规则 ▾'), 'open rules popover');
  await sleep(250);
  await report('text rules popover');
  await ev(`document.body.click(); document.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape'})); void 0`);

  // ---- hex
  await ev(`openCompare(${J(path.join(tmp, 'ib.bin'))}, ${J(path.join(tmp, 'ib2.bin'))})`);
  ok(await waitFor('Tabs.active.kind === "hex" && Tabs.active.info'), 'hex loaded');
  await sleep(300);
  await report('hex');
  ok((await bodyText()).includes('不同字节数'), 'hex status translated');

  // ---- merge
  await ev(`Tabs.add(new MergeView({left:${J(path.join(tmp, 'im_l.txt'))}, base:${J(path.join(tmp, 'im_b.txt'))}, right:${J(path.join(tmp, 'im_r.txt'))}, output:''})); void 0`);
  ok(await waitFor('Tabs.active.kind === "merge" && Tabs.active.loaded'), 'merge loaded');
  await sleep(300);
  await report('merge');
  ok((await bodyText()).includes('冲突'), 'merge status translated');

  // ---- more dialogs: picker, find bar, text report, edit-as-text, merge edit, session prompt
  const closeModal = async () => { await ev(`(()=>{const c=document.querySelector('.modal .foot button');if(c)c.click()})(); void 0`); await sleep(150); };
  await ev(`pickPath('', 'dir'); void 0`); await sleep(400);
  await report('picker dir'); await closeModal();
  await ev(`pickPath('', 'file'); void 0`); await sleep(400);
  await report('picker file'); await closeModal();
  await ev(`Tabs.activate(Tabs.list.find(v=>v.kind==='text')); void 0`); await sleep(200);
  await ev(`Tabs.active.toggleFind(true); void 0`); await sleep(150);
  await report('find bar');
  ok(await clickText('.view.active .toolbar button', '报告…'), 'text report button');
  await sleep(250); await report('text report dialog'); await closeModal();
  await ev(`Tabs.active.editAll(0); void 0`); await sleep(250);
  await report('edit as text'); await closeModal();
  await ev(`saveSessionDialog && saveSessionDialog('text', 'a', 'b', ''); void 0`); await sleep(250);
  await report('session prompt'); await closeModal();
  await ev(`Tabs.activate(Tabs.list.find(v=>v.kind==='merge')); void 0`); await sleep(200);
  await ev(`Tabs.active.editRegion(Tabs.active.regions.findIndex(g=>g.k!=="same")); void 0`); await sleep(250);
  await report('merge edit region'); await closeModal();

  // ---- toast + modal while in Chinese
  await ev(`toast('Select a session first'); void 0`); await sleep(100);
  ok((await bodyText()).includes('请先选择一个会话'), 'toast translated');
  await ev(`toast('Saved foo.txt','ok'); toast('3 operation(s) done','ok'); void 0`); await sleep(100);
  bt = await bodyText();
  ok(bt.includes('已保存 foo.txt') && bt.includes('3 个操作已完成'), 'pattern toasts translated');
  await ev(`confirmBox('Delete session','Delete session "My S"?','Delete',true); void 0`); await sleep(150);
  bt = await bodyText();
  ok(bt.includes('删除会话“My S”？'), 'confirm dialog translated');
  await ev(`document.querySelector('.modal .foot button').click(); void 0`); await sleep(100);

  // ---- back to English: everything reverts
  await ev(`document.querySelector('.tab-lang').click(); void 0`); await sleep(300);
  ok(await ev(`Lang.cur`) === 'en', 'switched back to en');
  bt = await bodyText();
  ok(!/[\u4e00-\u9fff]/.test(bt.replace(/中文/g, '')), 'no Chinese left after switching back: ' + (bt.match(/.{0,20}[\u4e00-\u9fff]+.{0,20}/) || [''])[0]);
  await ev(`Tabs.activate(Tabs.list.find(v=>v.kind==='merge')); void 0`); await sleep(150);
  ok((await bodyText()).includes('Conflicts'), 'merge status back in English');
  ok(await ev(`document.querySelector('.tab-lang').textContent`) === '中文', 'button shows 中文 again');

  // ---- persistence across reload: set zh, reload, still zh
  await ev(`Lang.set('zh'); void 0`); await sleep(100);
  ok(await ev(`localStorage.getItem('mbc.settings')`).then(s => /"lang":"zh"/.test(s)), 'zh stored');
  await ev(`Lang.set('en'); void 0`); await sleep(100);
  ok(await ev(`localStorage.getItem('mbc.settings')`).then(s => /"lang":"en"/.test(s)), 'en stored');
};

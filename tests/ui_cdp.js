// Minimal CDP driver: node ui_cdp.js  (needs mbc.exe + Chrome/Edge)
const { spawn } = require('child_process');
const fs = require('fs'), os = require('os'), path = require('path');
const MBC = path.resolve(__dirname, '..', process.platform === 'win32' ? 'mbc.exe' : 'mbc');
const CHROME = [process.env.CHROME, 'C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(p => p && fs.existsSync(p));
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'mbcui-'));
const w = (p, s) => { fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, s); };
const A = path.join(tmp, 'a'), B = path.join(tmp, 'b');
w(A + '/f.txt', 'one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten\neleven\ntwelve\n');
w(B + '/f.txt', 'one\n2\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten\neleven\ntwelve\nthirteen\n');
w(A + '/sub/s.txt', 'same\n'); w(B + '/sub/s.txt', 'same\n'); w(A + '/only.txt', 'lonely\n');
fs.writeFileSync(A + '/bin.dat', Buffer.from([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17])); fs.writeFileSync(B + '/bin.dat', Buffer.from([0, 1, 2, 3, 99, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18]));
w(tmp + '/base.txt', 'a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\n');
w(tmp + '/left.txt', 'a\nB\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\n');
w(tmp + '/right.txt', 'a\nb2\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nM\nn\n');
const sleep = ms => new Promise(r => setTimeout(r, ms));
const procs = [];
function cleanup() { for (const p of procs) try { p.kill(); } catch (e) {} try { fs.rmSync(tmp, { recursive: true, force: true }); } catch (e) {} }
let fails = 0, checks = 0;
const ok = (c, m) => { checks++; if (!c) { fails++; console.log('FAIL:', m); } };

(async () => {
  const port = 18800 + Math.floor(Math.random() * 100);
  const srv = spawn(MBC, ['gui', '--no-open', '--port', String(port), '--sessions', path.join(tmp, 'sess.xml')]);
  procs.push(srv);
  let url = '';
  srv.stdout.on('data', d => { const m = /http:\S+/.exec(String(d)); if (m) url = m[0]; });
  for (let i = 0; i < 50 && !url; i++) await sleep(100);
  if (!url) throw new Error('server did not start');
  const dbg = 9300 + Math.floor(Math.random() * 100);
  const ch = spawn(CHROME, ['--headless=new', '--remote-debugging-port=' + dbg, '--user-data-dir=' + path.join(tmp, 'prof'), '--window-size=1400,900', '--lang=en-US', '--accept-lang=en-US', '--no-first-run', '--disable-gpu', 'about:blank'], { stdio: 'ignore' });
  procs.push(ch);
  let tgt;
  for (let i = 0; i < 80 && !tgt; i++) { await sleep(150); try { const l = await (await fetch(`http://127.0.0.1:${dbg}/json`)).json(); tgt = l.find(t => t.type === 'page'); } catch (e) {} }
  if (!tgt) throw new Error('no chrome target');
  const ws = new WebSocket(tgt.webSocketDebuggerUrl);
  await new Promise(r => ws.onopen = r);
  let id = 0; const pend = new Map(); const errors = [];
  ws.onmessage = ev => {
    const m = JSON.parse(ev.data);
    if (m.id && pend.has(m.id)) { pend.get(m.id)(m); pend.delete(m.id); }
    else if (m.method === 'Runtime.exceptionThrown') errors.push(m.params.exceptionDetails.exception ? m.params.exceptionDetails.exception.description : m.params.exceptionDetails.text);
    else if (m.method === 'Runtime.consoleAPICalled' && m.params.type === 'error') errors.push('console.error: ' + m.params.args.map(a => a.value || a.description).join(' '));
  };
  const send = (method, params) => new Promise(r => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  const ev = async expr => { const r = await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true }); if (r.result.exceptionDetails) { errors.push('eval: ' + (r.result.exceptionDetails.exception || {}).description + ' in ' + expr.slice(0, 80)); return undefined; } return r.result.result.value; };
  const shot = async name => { const r = await send('Page.captureScreenshot', { format: 'png' }); fs.writeFileSync(path.join(__dirname, 'shots', name + '.png'), Buffer.from(r.result.data, 'base64')); };
  const waitFor = async (expr, ms = 5000) => { const t = Date.now(); while (Date.now() - t < ms) { if (await ev(expr)) return true; await sleep(80); } return false; };
  fs.mkdirSync(path.join(__dirname, 'shots'), { recursive: true });
  await send('Runtime.enable'); await send('Page.enable');
  const ctx = { send, ev, shot, waitFor, sleep, ok, errors, A, B, tmp, url };
  await send('Page.navigate', { url });
  ok(await waitFor('window.__mbcReady === true'), 'app ready');
  for (const f of ['./ui_scenarios.js', './ui_scenarios2.js', './ui_scenarios3.js', './ui_scenarios4.js', './ui_scenarios5.js', './ui_scenarios6.js']) {
    try { await require(f)(ctx); } catch (e) { fails++; console.log('SCENARIO EXCEPTION', f, e.stack); }
  }
  for (const e of errors) { fails++; console.log('JS ERROR:', e); }
  console.log(`ui checks: ${checks}, failed: ${fails}`);
  cleanup();
  process.exit(fails ? 1 : 0);
})().catch(e => { console.log('FATAL', e); cleanup(); process.exit(2); });

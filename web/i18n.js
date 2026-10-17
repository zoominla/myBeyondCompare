'use strict';
// ---------------------------------------------------------------- i18n (English <-> Chinese)
// The UI is written in English.  Chinese is applied by walking the DOM (text nodes, title/placeholder
// attributes) and keeping a MutationObserver while the language is Chinese, so no view needs to know about it.
// Switching back walks the DOM with the reverse tables.  User data (file contents, names) lives in the
// SKIP selector and is never touched.
const I18N_ZH = {
  // ---- generic
  'OK': '确定', 'Cancel': '取消', 'Apply': '应用', 'Close': '关闭', 'Delete': '删除', 'Reload': '重新加载', 'Discard': '放弃',
  'Exit': '退出', 'Save': '保存', 'Select': '选择', 'Go': '跳转', 'Open': '打开', 'Rename': '重命名', 'Compare': '比较',
  'Left': '左侧', 'Right': '右侧', 'Base': '基准', 'Output': '输出', 'Size': '大小', 'Modified': '修改时间', 'Offset': '偏移',
  'Home': '主页', 'Drives': '驱动器', 'Mode': '模式', 'Format': '格式', 'Filters': '过滤器', 'Encoding': '编码', 'Alignment': '对齐方式',
  'All': '全部', 'Same': '相同', 'Different': '不同', 'Similar': '相似', 'Differences': '差异', 'Important': '重要', 'Changed': '已更改',
  'Regions': '区域', 'Conflicts': '冲突', 'Files': '文件', 'Folders': '文件夹', 'Text': '文本', 'Hex': '十六进制', 'Shown': '已显示',
  'Selected': '已选', 'Errors': '错误', 'Current': '当前', 'First': '首个', 'Last': '末个', 'Orphans': '孤立项', 'Show': '显示',
  'Auto': '自动', 'Auto detect': '自动检测', 'Merge': '合并', 'Base:': '基准:',
  'Language': '语言', 'folder': '文件夹路径', 'file': '文件的路径',
  '⬆ Up': '⬆ 上级', 'Browse…': '浏览…', 'Select folder': '选择文件夹', 'Select file': '选择文件', 'Select this folder': '选择此文件夹',
  // ---- app chrome / tabs
  'Folder Compare': '文件夹比较', 'Text Compare': '文本比较', 'Hex Compare': '十六进制比较', 'Text Merge': '文本合并',
  'Stop the mbc gui server': '停止 mbc gui 服务', '⏻ Exit': '⏻ 退出',
  'Save session': '保存会话', 'Session name': '会话名称', 'Unsaved changes': '有未保存的更改',
  'Close this comparison and discard unsaved edits?': '关闭此比较并放弃未保存的修改？',
  'mbc gui has stopped': 'mbc gui 已停止', 'You can close this window.': '可以关闭此窗口了。',
  'Drag to resize the panes (double-click: equal widths)': '拖动调整窗格宽度（双击：恢复等宽）',
  'Drag to resize the panes (double-click: 50/50)': '拖动调整窗格宽度（双击：50/50）',
  // ---- home
  'Compare two folders recursively': '递归比较两个文件夹', 'Side-by-side text diff with editing': '并排文本差异，可直接编辑',
  'Byte-level comparison': '字节级比较', 'Three-way merge': '三方合并', 'Folders': '文件夹',
  'Enter both paths': '请输入两个路径', 'Browse for a folder…': '浏览文件夹…', 'Quick compare': '快速比较',
  'filter sessions…': '筛选会话…', 'Sessions': '会话', 'Rename…': '重命名…', 'Delete…': '删除…',
  'mbc - My Beyond Compare': 'mbc - My Beyond Compare',
  'No sessions match.': '没有匹配的会话。', 'No sessions yet. Every comparison is recorded here automatically under History; click "Session" to save one with a name.': '暂无会话。每次比较都会自动记录到“History”文件夹；点击“会话”可按名称另存。',
  'Select a session first': '请先选择一个会话', 'Rename session': '重命名会话', 'New name': '新名称',
  'Delete session': '删除会话',
  // ---- folder compare
  'Swap sides': '交换左右', '⟳ Refresh': '⟳ 刷新', '▶ Copy to right': '▶ 复制到右侧', '◀ Copy to left': '◀ 复制到左侧',
  'Copy selected items from left to right': '将所选项目从左侧复制到右侧', 'Copy selected items from right to left': '将所选项目从右侧复制到左侧',
  '🗑 Delete…': '🗑 删除…', 'Delete selected items': '删除所选项目', '⇅ Sync…': '⇅ 同步…', 'Synchronize folders': '同步文件夹',
  'Select differences': '选中差异项', 'Expand all': '全部展开', 'Collapse all': '全部折叠', 'Next diff ↓': '下一处差异 ↓', 'Prev diff ↑': '上一处差异 ↑',
  'Show ▾': '显示 ▾', 'Options ▾': '选项 ▾', 'Report…': '报告…', '💾 Session': '💾 会话',
  'Enter two folders above and press Compare.': '在上方输入两个文件夹后点击“比较”。',
  'Both folders are required': '必须指定两个文件夹', 'Comparing…': '正在比较…',
  'Left only': '仅左侧', 'Right only': '仅右侧',
  'Nothing to show - the folders contain no files.': '没有可显示的内容 - 文件夹中没有文件。',
  'No items match the current "Show" filter.': '没有项目符合当前的“显示”筛选条件。',
  'No differences': '没有差异', 'Compare as text': '以文本方式比较', 'Compare as hex': '以十六进制方式比较',
  'Compare folders in a new tab': '在新标签页中比较文件夹', 'Copy to right  ▶': '复制到右侧  ▶', 'Copy to left  ◀': '复制到左侧  ◀',
  'Delete left…': '删除左侧…', 'Delete right…': '删除右侧…', 'Delete both…': '删除两侧…', 'Expand': '展开', 'Collapse': '折叠',
  'Select differences in view': '选中视图中的差异项', 'Copy path': '复制路径', 'Nothing to do': '无需执行任何操作', 'Execute': '执行',
  'Select one or more items first': '请先选择一个或多个项目',
  'Copy to right': '复制到右侧', 'Copy to left': '复制到左侧', 'Copy': '复制', 'Delete from left': '从左侧删除', 'Delete from right': '从右侧删除',
  'Delete from both sides': '从两侧删除',
  'Update right (copy new/changed files left → right)': '更新右侧（将新增/已更改文件从左复制到右）',
  'Update left (copy right → left)': '更新左侧（从右复制到左）',
  'Update both sides (newer wins)': '双向更新（以较新者为准）',
  'Mirror to right (right becomes a copy of left, deletes extras)': '镜像到右侧（右侧变为左侧的副本，并删除多余项）',
  'Mirror to left (left becomes a copy of right, deletes extras)': '镜像到左侧（左侧变为右侧的副本，并删除多余项）',
  'Create empty folders': '创建空文件夹', 'Preview…': '预览…', 'Synchronize': '同步', 'Compare first': '请先进行比较',
  'Folder report': '文件夹报告', 'Differences only': '仅差异', 'Save report': '保存报告',
  'Identical': '相同', 'Different - left is newer': '不同 - 左侧较新', 'Different - right is newer': '不同 - 右侧较新',
  'Different - other': '不同 - 其他', 'Similar (rules-based)': '相似（基于规则）', 'Left orphans': '左侧孤立项', 'Right orphans': '右侧孤立项',
  'File/folder mismatch': '文件/文件夹类型不匹配', 'Hide empty folders': '隐藏空文件夹',
  'Compare files by': '文件比较方式', 'Modified time': '修改时间', 'CRC': 'CRC', 'Binary content': '二进制内容',
  'Rules-based (text, uses ignore settings of Text Compare)': '基于规则（文本，使用文本比较的忽略设置）',
  'Include subfolders': '包含子文件夹', 'Time tolerance (s)': '时间容差（秒）', 'Ignore daylight-saving (1 hour) differences': '忽略夏令时（1 小时）差异',
  'Ignore case of names': '忽略名称大小写', 'Apply & compare': '应用并比较',
  'content': '内容不同', 'size': '大小不同', 'time': '时间不同',
  // ---- text compare
  'ANSI / raw': 'ANSI / 原始字节', 'Change encoding': '更改编码',
  'This reloads the file and discards unsaved edits on this side. Continue?': '这将重新加载文件并放弃此侧未保存的修改。是否继续？',
  'Line endings (changing converts the whole file)': '行尾符（更改会转换整个文件）', 'Unix (LF)': 'Unix (LF)', 'DOS (CRLF)': 'DOS (CRLF)', 'Mac (CR)': 'Mac (CR)',
  '↶ Undo': '↶ 撤销', '↷ Redo': '↷ 重做', '▲ Prev diff': '▲ 上一处差异', '▼ Next diff': '▼ 下一处差异',
  'Copy ▶': '复制 ▶', '◀ Copy': '◀ 复制',
  'Copy the current difference from left to right (Alt+Right)': '将当前差异从左侧复制到右侧 (Alt+Right)',
  'Copy the current difference from right to left (Alt+Left)': '将当前差异从右侧复制到左侧 (Alt+Left)',
  '💾 Save left': '💾 保存左侧', '💾 Save right': '💾 保存右侧', 'Diffs only': '仅显示差异', 'Rules ▾': '规则 ▾', '🔍 Find': '🔍 查找',
  'Choose two files and press Compare.': '选择两个文件后点击“比较”。',
  'Edit the whole file as text': '以文本方式编辑整个文件', 'Save this side': '保存此侧',
  'Find text…': '查找文本…', 'Match case': '区分大小写',
  'Discard unsaved edits and reload from disk?': '放弃未保存的修改并从磁盘重新加载？',
  'Both files are required': '必须指定两个文件', 'Binary file detected - opening hex compare': '检测到二进制文件 - 正在打开十六进制比较',
  '(unsaved text)': '（未保存的文本）', '[mixed EOL]': '[混合行尾]',
  'Copy to right': '复制到右侧', 'Select a difference first': '请先选择一处差异', 'Save as': '另存为', 'File path': '文件路径',
  'Nothing to save on this side': '此侧没有需要保存的内容', 'At the first difference': '已在第一处差异', 'At the last difference': '已在最后一处差异',
  'Files are identical': '文件完全相同', 'Files differ': '文件不同', 'No differences (apart from line endings)': '没有差异（行尾符除外）',
  'No important differences': '没有重要差异', 'Same lines': '相同行', 'Left line': '左侧行', 'Right line': '右侧行',
  'Double-click a line to edit · Del removes the selected line · Ins inserts a line': '双击行可编辑 · Del 删除所选行 · Ins 插入一行',
  'Compare whitespace': '比较空白字符', 'Ignore amount of whitespace': '忽略空白字符数量', 'Ignore leading whitespace': '忽略行首空白',
  'Ignore trailing whitespace': '忽略行尾空白', 'Ignore leading & trailing': '忽略行首和行尾空白', 'Ignore all whitespace': '忽略所有空白',
  'Smart': '智能', 'Patience': 'Patience 算法', 'Complete (minimal)': '完整（最小差异）', 'Fast': '快速', 'Sorted lines': '排序后比较行', 'Positional (no alignment)': '按位置（不对齐）',
  'regular expressions - text matching any of them is ignored (one per line)': '正则表达式 - 匹配任一表达式的文本将被忽略（每行一个）',
  'Text compare rules': '文本比较规则', 'Ignore case': '忽略大小写', 'Ignore blank lines': '忽略空行', 'Ignore line-ending differences': '忽略行尾符差异',
  'Ignore text matching': '忽略匹配的文本',
  'Save your edits first - reports are made from the files on disk': '请先保存修改 - 报告基于磁盘上的文件生成', 'Text report': '文本报告',
  'Edit left file as text': '以文本方式编辑左侧文件', 'Edit right file as text': '以文本方式编辑右侧文件',
  // ---- hex
  'offset (0x… or decimal)': '偏移（0x… 或十进制）', 'Left size': '左侧大小', 'Right size': '右侧大小', 'Different bytes': '不同字节数',
  'Difference ranges': '差异区间数', 'Invalid offset': '偏移无效',
  // ---- merge
  '▲ Prev change': '▲ 上一处更改', '▼ Next change': '▼ 下一处更改', '⚠ Next conflict': '⚠ 下一个冲突',
  'Take left': '采用左侧', 'Use the left version for the selected region': '所选区域采用左侧版本', 'Take base': '采用基准', 'Take right': '采用右侧',
  'Left+Right': '左+右', 'Right+Left': '右+左', 'Edit…': '编辑…', 'Reset': '重置', 'Take all left': '全部采用左侧', 'Take all right': '全部采用右侧',
  '💾 Save output': '💾 保存输出',
  'Choose left, base (common ancestor) and right files, then press Merge.': '选择左侧、基准（共同祖先）和右侧文件后点击“合并”。',
  'Close without saving the merge output?': '不保存合并结果就关闭？', 'Discard your choices and merge again?': '放弃当前选择并重新合并？', 'Merge again': '重新合并',
  'Left, base and right files are required': '必须指定左侧、基准和右侧文件',
  'auto: left changed': '自动：仅左侧更改', 'auto: right changed': '自动：仅右侧更改', 'auto: same change': '自动：两侧相同更改',
  'left + right': '左 + 右', 'right + left': '右 + 左', ' (deleted)': '（已删除）', '(deleted)': '（已删除）',
  'Select a region first': '请先选择一个区域', 'That region is identical in all files': '该区域在所有文件中都相同',
  'No unresolved conflicts': '没有未解决的冲突', 'Edit merge result for this region': '编辑此区域的合并结果',
  'No earlier changes': '没有更早的更改', 'All conflicts resolved': '所有冲突均已解决', 'No conflicts': '没有冲突', 'Auto-merged': '自动合并',
  'Selected region': '已选区域', 'Click a region, then use the toolbar to choose its result': '点击一个区域，然后使用工具栏选择其结果',
  'Unresolved conflicts': '未解决的冲突', 'Save with markers': '带标记保存', 'Save merge result': '保存合并结果', 'Output file path': '输出文件路径',
  'conflicts': '冲突项', 'changes': '更改项', 'left': '左侧版本', 'right': '右侧版本', 'base': '基准版本',
  // ---- misc
  'Exit anyway?': '仍要退出吗？',
};

// patterns: [english template, chinese template] - {0}, {1} are placeholders (translated recursively)
const I18N_PAT = [
  ['📂 History ({0})', '📂 历史记录 ({0})'],
  ['Saved {0} (with conflict markers)', '已保存 {0}（含冲突标记）'],
  ['Saved {0}', '已保存 {0}'],
  ['Session "{0}" saved', '会话“{0}”已保存'],
  ['{0} operation(s) failed:\n{1}', '{0} 个操作失败：\n{1}'],
  ['{0} operation(s) done', '{0} 个操作已完成'],
  ['"{0}" exists on one side only - nothing to compare', '“{0}”仅存在于一侧 - 没有可比较的内容'],
  ['Synchronize: {0}', '同步：{0}'],
  ['Only the selected items ({0})', '仅所选项目（{0}）'],
  ['differs: {0}', '差异：{0}'],
  ['{0} match(es)', '{0} 处匹配'],
  ['⋯  {0} identical line(s) hidden  ⋯', '⋯  已隐藏 {0} 行相同内容  ⋯'],
  ['Edit {0} file as text', '以文本方式编辑{0}文件'],
  ['There are unsaved changes in {0} tab(s). Exit anyway?', '有 {0} 个标签页存在未保存的修改。仍要退出吗？'],
  ['Delete session "{0}"?', '删除会话“{0}”？'],
  ['({0} in {1})', '（共 {0} 个，位于 {1}）'],
  ['server not reachable: {0}', '无法连接服务器：{0}'],
  ['navigation limited to the first {0} ranges', '导航仅限于前 {0} 个区间'],
  ['{0} unresolved conflict(s)', '{0} 个未解决的冲突'],
  ['{0} conflict(s) are still unresolved. Save anyway with <<<<<<< ======= >>>>>>> conflict markers?', '仍有 {0} 个冲突未解决。是否带 <<<<<<< ======= >>>>>>> 冲突标记保存？'],
  ['{0} conflict(s) resolved with {1}', '已用{1}解决 {0} 个冲突'],
  ['No more {0}', '没有更多{0}'],
  ['Merge: {0}', '合并：{0}'],
  ['⚠ conflict: {0} left line(s) / {1} right line(s) - choose a resolution', '⚠ 冲突：左侧 {0} 行 / 右侧 {1} 行 - 请选择处理方式'],
  ['{0}   ({1} to copy)', '{0}   （需复制 {1}）'],
];

const I18N_SKIP = '.tx, .mgreg pre, .mgo pre, .hexrow:not(.hexhead), .dirrow .nm, .dirrow .num, textarea, .oplist, .sessrow .nm, .sessrow .p, .picker .it .nm, .ttl, [data-notr]';

const Lang = {
  cur: 'en',
  _maps: null,
  build() {
    const mk = (src) => {
      const exact = new Map(), pats = [];
      for (const [k, v] of Object.entries(src.dict)) { const a = src.rev ? v : k, b = src.rev ? k : v; if (!exact.has(a)) exact.set(a, b); }
      for (const [en, zh] of src.pats) {
        const from = src.rev ? zh : en, to = src.rev ? en : zh;
        const esc = from.replace(/[.*+?^$|\\()[\]]/g, '\\$&');
        const re = new RegExp('^' + esc.replace(/\{(\d)\}/g, '([\\s\\S]*?)') + '$');
        const order = [...from.matchAll(/\{(\d)\}/g)].map(m => +m[1]);
        pats.push({ re, to, order });
      }
      return { exact, pats };
    };
    this._maps = { zh: mk({ dict: I18N_ZH, pats: I18N_PAT, rev: false }), en: mk({ dict: I18N_ZH, pats: I18N_PAT, rev: true }) };
  },
  // translate one trimmed string into language `to` ('zh' or 'en'); returns null when nothing applies
  tr(s, to, depth) {
    if (!this._maps) this.build();
    const m = this._maps[to];
    const hit = m.exact.get(s);
    if (hit !== undefined) return hit;
    if (s.length > 400 || !/[A-Za-z\u4e00-\u9fff]/.test(s)) return null;
    for (const p of m.pats) {
      const r = p.re.exec(s);
      if (!r) continue;
      const args = [];
      p.order.forEach((n, i) => { args[n] = r[i + 1]; });
      return p.to.replace(/\{(\d)\}/g, (_, n) => {
        const a = args[+n];
        if (a === undefined) return '';
        if (depth > 2) return a;
        const t = this.tr(a, to, (depth || 0) + 1);
        return t === null ? a : t;
      });
    }
    return null;
  },
  t(s) { return this.cur === 'zh' ? (this.tr(s, 'zh', 0) ?? s) : s; },
  fixText(n, to) {
    const v = n.nodeValue;
    if (!v || !/[A-Za-z\u4e00-\u9fff]/.test(v)) return;
    const p = n.parentNode;
    if (!p || p.nodeType !== 1) return;
    if (p.tagName === 'SCRIPT' || p.tagName === 'STYLE') return;
    if (p.closest && p.closest(I18N_SKIP) && !p.closest('[data-tr]')) return;
    const m = /^(\s*)([\s\S]*?)(\s*)$/.exec(v);
    const out = this.tr(m[2], to, 0);
    if (out !== null && out !== m[2]) n.nodeValue = m[1] + out + m[3];
  },
  fixAttrs(el, to) {
    for (const a of ['title', 'placeholder', 'aria-label']) {
      const v = el.getAttribute && el.getAttribute(a);
      if (!v || (el.closest && el.closest('.tx, .hexrow:not(.hexhead), .sessrow, .picker .it'))) continue;
      const out = this.tr(v, to, 0);
      if (out !== null && out !== v) el.setAttribute(a, out);
    }
  },
  walk(root, to) {
    if (root.nodeType === 3) { this.fixText(root, to); return; }
    if (root.nodeType !== 1) return;
    this.fixAttrs(root, to);
    const tw = document.createTreeWalker(root, NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT);
    let n;
    while ((n = tw.nextNode())) {
      if (n.nodeType === 3) this.fixText(n, to); else this.fixAttrs(n, to);
    }
  },
  apply(to) {
    this._busy = true;
    try { this.walk(document.body, to); } finally { this._busy = false; }
    if (this.obs) this.obs.takeRecords();
  },
  observe() {
    if (this.obs) return;
    this.obs = new MutationObserver(recs => {
      if (this._busy || this.cur !== 'zh') return;
      this._busy = true;
      try {
        for (const r of recs) {
          if (r.type === 'childList') r.addedNodes.forEach(n => this.walk(n, 'zh'));
          else if (r.type === 'characterData') this.fixText(r.target, 'zh');
          else if (r.type === 'attributes') this.fixAttrs(r.target, 'zh');
        }
      } finally { this._busy = false; }
      this.obs.takeRecords();
    });
    this.obs.observe(document.body, { childList: true, subtree: true, characterData: true, attributes: true, attributeFilter: ['title', 'placeholder'] });
  },
  set(l, persist) {
    if (l !== 'zh') l = 'en';
    const prev = this.cur;
    this.cur = l;
    document.documentElement.lang = l === 'zh' ? 'zh-CN' : 'en';
    if (persist !== false) Settings.set('lang', l);
    if (l === 'zh') { this.apply('zh'); this.observe(); }
    else if (prev === 'zh') { this.apply('en'); }
    if (typeof Tabs !== 'undefined' && Tabs.render) Tabs.render();
    if (this.onChange) this.onChange(l);
  },
  toggle() { this.set(this.cur === 'zh' ? 'en' : 'zh'); },
  init() {
    let l = Settings.get('lang', null);
    if (!l) l = /^zh/i.test(navigator.language || '') ? 'zh' : 'en';
    if (l === 'zh') this.set('zh', false);
    else document.documentElement.lang = 'en';
  },
};

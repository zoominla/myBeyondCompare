#include "script.h"

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

#include "hexcmp.h"
#include "report.h"
#include "textfile.h"

namespace bc {

bool emitOutput(const std::string& path, const std::string& data, std::ostream& out, std::string* err) {
  if (path.empty() || path == "-" || path == "clipboard" || path == "printer") {
    out << data;
    out.flush();
    return true;
  }
  fs::path p = toPath(path);
  std::error_code ec;
  if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
  return writeFile(path, data, err);
}

std::vector<std::string> scriptTokens(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  bool inQ = false, have = false;
  for (char c : line) {
    if (c == '"') { inQ = !inQ; have = true; continue; }
    if (!inQ && (c == ' ' || c == '\t' || c == '\r')) {
      if (have) out.push_back(cur);
      cur.clear();
      have = false;
      continue;
    }
    cur += c;
    have = true;
  }
  if (have) out.push_back(cur);
  return out;
}

namespace {

struct Script {
  ScriptEnv& env;
  std::ostream& out;
  std::ostream& err;
  enum class Mode { None, Dir, Text, Hex } mode = Mode::None;
  std::string left, right;
  bool dirty = false;
  DirResult dr;
  int logLevel = 0;
  std::string logPath;
  int lineNo = 0;
  std::string errMsg;

  explicit Script(ScriptEnv& e) : env(e), out(e.out ? *e.out : std::cout), err(e.err ? *e.err : std::cerr) {}

  bool fail(const std::string& m) {
    errMsg = "script line " + std::to_string(lineNo) + ": " + m;
    err << errMsg << "\n";
    return false;
  }

  std::string subst(const std::string& line) {
    std::string o;
    for (size_t i = 0; i < line.size(); i++) {
      if (line[i] == '%' && i + 1 < line.size()) {
        if (isdigit((unsigned char)line[i + 1])) {
          size_t n = line[i + 1] - '0';
          if (n >= 1 && n <= env.args.size()) o += env.args[n - 1];
          i++;
          continue;
        }
        if (line[i + 1] == '%') { o += '%'; i++; continue; }
      }
      o += line[i];
    }
    return o;
  }

  void logLine(const std::string& s, bool verboseOnly = false) {
    if (logLevel == 0 || (verboseOnly && logLevel < 2)) return;
    if (logPath.empty()) { out << s << "\n"; return; }
    std::ofstream f(toPath(logPath), std::ios::app | std::ios::binary);
    f << s << "\n";
  }

  bool ensureDir() {
    if (mode != Mode::Dir) return fail("this command needs a folder compare (use 'load' with two folders)");
    if (dirty) {
      std::string e;
      if (!compareDirs(left, right, env.dir, dr, &e)) return fail(e);
      if (!e.empty()) err << "warning: " << e;
      dirty = false;
    }
    return true;
  }

  // ---- commands ----------------------------------------------------------------
  bool cmdLoad(const std::vector<std::string>& t) {
    std::vector<std::string> a(t.begin() + 1, t.end());
    std::string l, r;
    if (!a.empty() && !a[0].empty() && a[0][0] == '@') {
      std::string name = a[0].substr(1);
      for (size_t i = 1; i < a.size(); i++) name += " " + a[i];
      if (!env.sessions) return fail("no session store available");
      const Session* s = env.sessions->find(name);
      if (!s) return fail("session not found: " + name);
      l = nativeSessionPath(s->left);
      r = nativeSessionPath(s->right);
    } else {
      if (a.size() < 1) return fail("load needs a left (and right) path");
      l = a[0];
      r = a.size() > 1 ? a[1] : "";
      if (a.size() == 1) {  // single folder: compare with itself is pointless; require two
        return fail("load needs two paths");
      }
    }
    std::error_code ec;
    bool ld = fs::is_directory(toPath(l), ec), rd = fs::is_directory(toPath(r), ec);
    bool lf = fs::is_regular_file(toPath(l), ec), rf = fs::is_regular_file(toPath(r), ec);
    left = l;
    right = r;
    if (ld && rd) { mode = Mode::Dir; dirty = true; return true; }
    if (lf && rf) {
      std::string a1, b1;
      std::string e;
      if (!readFile(l, a1, &e) || !readFile(r, b1, &e)) return fail(e);
      mode = (looksBinary(a1) || looksBinary(b1)) ? Mode::Hex : Mode::Text;
      return true;
    }
    return fail("cannot compare '" + l + "' with '" + r + "' (both must be existing files or both folders)");
  }

  bool cmdFilter(const std::vector<std::string>& t) {
    std::string spec;
    for (size_t i = 1; i < t.size(); i++) spec += (i > 1 ? ";" : "") + t[i];
    if (spec == "*" || spec == "*.*") spec.clear();
    env.dir.filter.parse(spec);
    dirty = true;
    return true;
  }

  bool cmdCriteria(const std::vector<std::string>& t) {
    std::string s;
    for (size_t i = 1; i < t.size(); i++) {
      std::string w = toLower(t[i]);
      if (w == "attributes" || w == "version" || w == "comments" || w == "attribute") continue;
      s += w + ",";
    }
    DirCompareOptions o = env.dir;
    if (!parseCriteria(s, o)) return fail("unknown criteria: " + s);
    env.dir = o;
    dirty = true;
    return true;
  }

  static bool isTrue(const std::string& v) {
    std::string l = toLower(v);
    return l == "true" || l == "yes" || l == "on" || l == "1";
  }

  bool cmdOption(const std::vector<std::string>& t) {
    for (size_t i = 1; i < t.size(); i++) {
      size_t c = t[i].find(':');
      std::string k = toLower(t[i].substr(0, c)), v = c == std::string::npos ? "" : t[i].substr(c + 1);
      if (k == "ignore-case") env.diff.ignoreCase = isTrue(v);
      else if (k == "ignore-whitespace" || k == "ignore-ws") {
        std::string lv = toLower(v);
        env.diff.ignoreWsAll = lv == "all" || isTrue(v);
        env.diff.ignoreWsAmount = lv == "amount";
        env.diff.ignoreWsLeading = lv == "leading";
        env.diff.ignoreWsTrailing = lv == "trailing";
      } else if (k == "ignore-eol") env.diff.ignoreEol = isTrue(v);
      else if (k == "ignore-blank-lines") env.diff.ignoreBlankLines = isTrue(v);
      else if (k == "ignore-regex") env.diff.ignoreRegexes.push_back(v);
      else if (k == "align") { if (!parseAlign(v, env.diff.align)) return fail("bad align: " + v); }
      else if (k == "tolerance") { env.dir.timeTolerance = atoll(v.c_str()); dirty = true; }
      else if (k == "ignore-dst") { env.dir.ignoreDst = isTrue(v); dirty = true; }
      else if (k == "ignore-case-names") { env.dir.icaseNames = isTrue(v); dirty = true; }
      else if (k == "dry-run") env.dryRun = isTrue(v);
      else if (k == "recursive") { env.dir.recursive = isTrue(v); dirty = true; }
      // confirm:, stop-on-error:, etc. are accepted and ignored (non interactive)
    }
    return true;
  }

  bool cmdLog(const std::vector<std::string>& t) {
    logLevel = 1;
    logPath.clear();
    for (size_t i = 1; i < t.size(); i++) {
      std::string w = toLower(t[i]);
      if (w == "none") logLevel = 0;
      else if (w == "normal") logLevel = 1;
      else if (w == "verbose") logLevel = 2;
      else if (w == "append" || w == "replace") {}
      else logPath = t[i];
    }
    if (!logPath.empty() && logLevel > 0) {
      std::ofstream f(toPath(logPath), std::ios::app | std::ios::binary);
      f << "# mbc log\n";
    }
    return true;
  }

  static bool selTokenMatch(const std::string& tok0, const DirEntry& e) {
    std::string tok = toLower(tok0);
    if (tok.find('*') != std::string::npos || tok.find('?') != std::string::npos) {
      std::string p = replaceAll(tok0, "\\", "/");
      return wildMatch(p, p.find('/') != std::string::npos ? e.rel : e.name, true);
    }
    bool side[2] = {false, false};  // left, right
    bool newer = false, older = false, orphan = false, diff = false, same = false, all = false, files = false, folders = false;
    for (auto& p : splitAny(tok, ".")) {
      if (p == "left") side[0] = true;
      else if (p == "right") side[1] = true;
      else if (p == "newer") newer = true;
      else if (p == "older") older = true;
      else if (p == "orphan" || p == "orphans") orphan = true;
      else if (p == "diff" || p == "differences" || p == "different" || p == "mismatches") diff = true;
      else if (p == "same" || p == "matches" || p == "equal") same = true;
      else if (p == "all") all = true;
      else if (p == "files") files = true;
      else if (p == "folders") folders = true;
      else if (p == "similar") diff = true;
    }
    if (files && e.isDir()) return false;
    if (folders && !e.isDir()) return false;
    bool anySide = side[0] || side[1];
    if (orphan) {
      if (side[0] && !side[1]) return e.status == DStatus::LeftOnly;
      if (side[1] && !side[0]) return e.status == DStatus::RightOnly;
      return e.status == DStatus::LeftOnly || e.status == DStatus::RightOnly;
    }
    if (newer || older) {
      if (e.isDir()) return false;
      bool ln = e.newer == Newer::Left, rn = e.newer == Newer::Right;
      if (side[0] && !side[1]) return newer ? ln : rn;
      if (side[1] && !side[0]) return newer ? rn : ln;
      return ln || rn;
    }
    if (diff) return e.status != DStatus::Same;
    if (same) return e.status == DStatus::Same;
    if (anySide) return (side[0] && e.hasL) || (side[1] && e.hasR);
    return all || files || folders;
  }

  bool cmdSelect(const std::vector<std::string>& t) {
    if (!ensureDir()) return false;
    for (auto& e : dr.entries) e.selected = false;
    for (size_t i = 1; i < t.size(); i++) {
      if (toLower(t[i]) == "none") { for (auto& e : dr.entries) e.selected = false; continue; }
      for (auto& e : dr.entries)
        if (selTokenMatch(t[i], e)) e.selected = true;
    }
    return true;
  }

  bool runOpList(const std::vector<Op>& ops) {
    int bad = runOps(ops, env.dryRun, [&](const std::string& l) { logLine(l); });
    if (!env.dryRun) dirty = true;
    if (bad) return fail(std::to_string(bad) + " operation(s) failed");
    return true;
  }

  bool cmdCopyTo(const std::vector<std::string>& t) {
    if (!ensureDir()) return false;
    if (t.size() < 2) return fail("copyto needs left, right or a path");
    std::string w = toLower(t[1]);
    if (w == "left" || w == "right") return runOpList(planCopySelected(dr, w == "right"));
    std::vector<Op> ops;
    for (auto& e : dr.entries) {
      if (!e.selected) continue;
      Op op;
      op.type = OpType::CopyLR;
      op.rel = e.rel;
      op.isDir = e.isDir();
      op.src = e.hasL ? joinPath(dr.left, e.relL) : joinPath(dr.right, e.relR);
      op.dst = joinPath(t[1], e.rel);
      if (e.isDir()) { op.type = OpType::MkdirR; op.src.clear(); }
      ops.push_back(op);
    }
    return runOpList(ops);
  }

  bool cmdMoveTo(const std::vector<std::string>& t) { return cmdCopyTo(t); }

  bool cmdDelete(const std::vector<std::string>& t) {
    if (!ensureDir()) return false;
    std::string w = t.size() > 1 ? toLower(t[1]) : "both";
    if (w != "left" && w != "right" && w != "both") return fail("delete needs left, right or both");
    return runOpList(planDeleteSelected(dr, w != "right", w != "left"));
  }

  bool cmdSync(const std::vector<std::string>& t) {
    if (!ensureDir()) return false;
    bool createEmpty = false;
    SyncMode mode = SyncMode::UpdateLR;
    bool have = false;
    for (size_t i = 1; i < t.size(); i++) {
      std::string w = toLower(t[i]);
      if (w == "create-empty") createEmpty = true;
      else if (w == "create-empty:false") createEmpty = false;
      else if (!parseSyncMode(w, mode)) return fail("bad sync mode: " + t[i]);
      else have = true;
    }
    if (!have) return fail("sync needs a mode such as update:left->right");
    return runOpList(planSync(dr, mode, createEmpty, false));
  }

  static ShowFilter showFromOptions(const std::string& opts) {
    ShowFilter f;
    bool any = false;
    bool a = false;
    ShowFilter acc;
    acc.same = acc.diff = acc.similar = acc.left = acc.right = acc.newerLeft = acc.newerRight = acc.sameTimeDiff = acc.mismatch = false;
    for (auto& o0 : splitAny(opts, ",")) {
      std::string o = toLower(o0);
      if (!startsWith(o, "display-")) continue;
      any = true;
      if (o == "display-all") a = true;
      else if (o == "display-mismatches") { acc.diff = acc.similar = acc.left = acc.right = acc.newerLeft = acc.newerRight = acc.sameTimeDiff = acc.mismatch = true; }
      else if (o == "display-matches") acc.same = true;
      else if (o == "display-left-orphans") acc.left = true;
      else if (o == "display-right-orphans") acc.right = true;
      else if (o == "display-orphans") acc.left = acc.right = true;
      else if (o == "display-left-newer") acc.newerLeft = true;
      else if (o == "display-right-newer") acc.newerRight = true;
      else if (o == "display-differences") { acc.sameTimeDiff = acc.newerLeft = acc.newerRight = true; }
    }
    if (!any || a) return f;
    return acc;
  }

  static std::map<std::string, std::string> kvArgs(const std::vector<std::string>& t) {
    std::map<std::string, std::string> m;
    for (size_t i = 1; i < t.size(); i++) {
      size_t c = t[i].find(':');
      if (c == std::string::npos) continue;
      m[toLower(t[i].substr(0, c))] = t[i].substr(c + 1);
    }
    return m;
  }

  bool cmdFolderReport(const std::vector<std::string>& t) {
    if (!ensureDir()) return false;
    auto kv = kvArgs(t);
    FolderReportOptions fo;
    std::string layout = toLower(kv.count("layout") ? kv["layout"] : "summary");
    std::string oo = toLower(kv.count("output-options") ? kv["output-options"] : "");
    std::string style = "tree";
    if (layout == "xml") style = "xml";
    else if (layout == "statistics" || layout == "stats") style = "summary";
    else if (layout == "csv") style = "csv";
    else if (layout == "list") style = "list";
    else if (layout == "html") style = "html";
    if (oo.find("html") != std::string::npos && style == "tree") style = "html";
    fo.style = style;
    fo.show = showFromOptions(kv.count("options") ? kv["options"] : "");
    std::string opt = toLower(kv.count("options") ? kv["options"] : "");
    if (opt.find("hide-empty") != std::string::npos) fo.show.hideEmptyDirs = true;
    std::string rep = folderReport(dr, fo);
    std::string e;
    if (!emitOutput(kv.count("output-to") ? kv["output-to"] : "", rep, out, &e)) return fail(e);
    return true;
  }

  static std::string textStyleFor(const std::string& layout, const std::string& oo) {
    std::string l = toLower(layout);
    std::string s = "sidebyside";
    if (l.find("unified") != std::string::npos) s = "unified";
    else if (l.find("context") != std::string::npos) s = "context";
    else if (l.find("normal") != std::string::npos) s = "normal";
    else if (l.find("patch") != std::string::npos) s = "unified";
    else if (l.find("summary") != std::string::npos || l.find("statistics") != std::string::npos) s = "summary";
    else if (l == "html") s = "html";
    if (oo.find("html") != std::string::npos && s == "sidebyside") s = "html";
    return s;
  }

  bool reportPair(const std::string& l, const std::string& r, const TextReportOptions& base, std::string& outText) {
    std::string a, b, e;
    if (!readFile(l, a, &e) || !readFile(r, b, &e)) return fail(e);
    TextReportOptions o = base;
    o.leftTitle = o.leftTitle.empty() ? l : o.leftTitle;
    o.rightTitle = o.rightTitle.empty() ? r : o.rightTitle;
    if (looksBinary(a) || looksBinary(b)) {
      auto h = hexCompare(a, b);
      outText += "Binary files " + l + " and " + r + (h.equal ? " are identical\n" : " differ\n");
      return true;
    }
    TextCompare tc;
    if (!compareTextFiles(l, r, env.diff, tc, &e, env.lenc, env.renc)) return fail(e);
    outText += textReport(tc, o);
    return true;
  }

  bool cmdTextReport(const std::vector<std::string>& t) {
    if (mode == Mode::None) return fail("nothing loaded");
    auto kv = kvArgs(t);
    TextReportOptions o;
    std::string oo = toLower(kv.count("output-options") ? kv["output-options"] : "");
    o.style = textStyleFor(kv.count("layout") ? kv["layout"] : "side-by-side", oo);
    std::string opt = toLower(kv.count("options") ? kv["options"] : "");
    o.onlyDiffs = opt.find("display-mismatches") != std::string::npos;
    if (kv.count("title")) { o.leftTitle = kv["title"]; }
    std::string text;
    if (mode == Mode::Dir) {
      if (!ensureDir()) return false;
      int n = 0;
      for (auto& e : dr.entries) {
        if (!e.selected || e.isDir() || !e.hasL || !e.hasR) continue;
        TextReportOptions oo2 = o;
        oo2.leftTitle = joinPath(dr.left, e.relL);
        oo2.rightTitle = joinPath(dr.right, e.relR);
        if (n++) text += "\n";
        if (!reportPair(oo2.leftTitle, oo2.rightTitle, oo2, text)) return false;
      }
      if (n == 0) return fail("no selected file pairs to report on (use 'select' first)");
    } else {
      if (!reportPair(left, right, o, text)) return false;
    }
    std::string e;
    if (!emitOutput(kv.count("output-to") ? kv["output-to"] : "", text, out, &e)) return fail(e);
    return true;
  }

  bool cmdFileReport(const std::vector<std::string>& t) {
    return mode == Mode::Dir ? cmdTextReport(t) : cmdTextReport(t);
  }

  bool dispatch(const std::vector<std::string>& t) {
    std::string c = toLower(t[0]);
    if (c == "load") return cmdLoad(t);
    if (c == "filter" || c == "file-filter") return cmdFilter(t);
    if (c == "criteria") return cmdCriteria(t);
    if (c == "option") return cmdOption(t);
    if (c == "log") return cmdLog(t);
    if (c == "select") return cmdSelect(t);
    if (c == "copyto") return cmdCopyTo(t);
    if (c == "moveto") return cmdMoveTo(t);
    if (c == "delete") return cmdDelete(t);
    if (c == "sync") return cmdSync(t);
    if (c == "folder-report") return cmdFolderReport(t);
    if (c == "text-report") return cmdTextReport(t);
    if (c == "file-report") return cmdFileReport(t);
    if (c == "expand" || c == "collapse" || c == "compare" || c == "view" || c == "colors") {
      if (c == "compare") dirty = true;
      return true;
    }
    if (c == "echo") {
      std::string s;
      for (size_t i = 1; i < t.size(); i++) s += (i > 1 ? " " : "") + t[i];
      out << s << "\n";
      return true;
    }
    return fail("unknown command '" + t[0] + "'");
  }
};

}  // namespace

int runScript(const std::string& text, ScriptEnv& env) {
  Script s(env);
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    s.lineNo++;
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    auto toks = scriptTokens(s.subst(t));
    if (toks.empty()) continue;
    if (!s.dispatch(toks)) return EXIT_ERROR;
  }
  return 0;
}

}  // namespace bc

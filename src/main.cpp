// mbc - Beyond Compare style command line compare tool (clean-room C++ reimplementation)
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#include "common.h"
#include "dircmp.h"
#include "diff.h"
#include "gui.h"
#include "hexcmp.h"
#include "merge.h"
#include "report.h"
#include "script.h"
#include "session.h"
#include "textfile.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <shellapi.h>
#endif

using namespace bc;

namespace {

const char* kVersion = "mbc 1.0 (clean-room reimplementation of Beyond Compare style compare features)";

const char* kHelp = R"(mbc - text / folder / hex / 3-way merge compare tool, BComp compatible exit codes

USAGE
  mbc [command] [options] <args>

COMMANDS
  compare L R          auto detect: folder/folder, text/text, binary/binary   (default)
  diff    L R          text compare, prints a report (default style: unified)
  dir     L R          folder compare, prints a report (default style: tree)
  hex     L R          binary (hex) compare
  merge   L R BASE     3-way merge. Output: -o FILE or /mergeoutput=FILE
  sync    L R          folder synchronisation (--mode update:lr|mirror:lr|update:rl|mirror:rl|both)
  script  FILE [args]  run a BC style script (%1..%9 are arguments); also: mbc @FILE
  session list|show NAME|run NAME|save NAME L R|delete NAME     (BCSessions.xml)
  help, version

EXIT CODES (as BComp.com)
  0 success  1 binary same  2 rules-based same  11 binary differences  12 similar
  13 rules-based differences  14 conflicts  100 error  101 conflicts, not saved

TEXT RULES
  -i, --ignore-case            -w, --ignore-all-space      -b, --ignore-space-change
  --ignore-leading-space       --ignore-trailing-space     --no-ignore-eol
  -B, --ignore-blank-lines     --ignore-regex RE (repeatable)
  --align smart|complete|fast|patience|sort|none
  --encoding E | --left-encoding E | --right-encoding E     (utf8,utf8bom,utf16le,utf16be,ansi)
  /iu  ignore unimportant differences

REPORTS
  --style sidebyside|unified|context|normal|html|summary     (text)
  --report-style tree|list|csv|html|xml|summary              (folders)
  -u, -c, -n               unified / context / normal diff
  -U N, --context N        context lines          --only-diffs  side-by-side/html: differences only
  -o, --output FILE        write the report to FILE         -q, --quiet  no report, exit code only
  /qc                      quick compare: binary identical or not (exit 1 / 11)

FOLDERS
  --criteria size,time,crc,binary,rules   what makes two files different (default binary)
  --filter "*.c;*.h;-*.o;-.git/"   include / exclude (trailing / = folder)    --no-recursive  --depth N
  --tolerance SECS  --ignore-dst  --ignore-name-case  --follow-symlinks  --no-orphan-children
  --show all|diff|same|left|right|orphans|newer-left|newer-right|similar     --prune  hide empty folders
  sync: --mode MODE  --dry-run  --no-empty-folders

MERGE
  --resolve left|right|both  auto-resolve conflicts      --force  write conflict markers (<<<<<<< ... >>>>>>>)

GUI
  mbc gui [LEFT [RIGHT]]  start the local web GUI (--port N, --no-open, --view dir|text|hex|merge)

OTHER
  --sessions FILE     session store (default: ./BCSessions.xml in the current directory, created if missing; env MBC_SESSIONS overrides)
  BC switches accepted and ignored: /silent /closescript /solo /favorleft /favorright /expandall /automerge ...
)";

struct Cli {
  std::string cmd;
  std::vector<std::string> pos;
  DiffOptions diff;
  DirCompareOptions dir;
  ShowFilter show;
  TextReportOptions text;
  FolderReportOptions folder;
  bool styleSet = false, folderStyleSet = false;
  Enc lenc = Enc::Auto, renc = Enc::Auto;
  std::string output;
  bool quiet = false, qc = false, force = false, dryRun = false, emptyFolders = true;
  Resolve resolve = Resolve::None;
  std::string mode = "update:lr";
  std::string sessionsPath;
  std::string fv;  // forced viewer from /fv=
  std::string title1, title2;
  std::string reviewMerge;
  int port = 0;
  bool noOpen = false;
  std::string view;
};

[[noreturn]] void usageError(const std::string& m) {
  std::cerr << "mbc: " << m << "\n(try 'mbc help')\n";
  std::exit(EXIT_ERROR);
}

bool isKnownSlashSwitch(const std::string& a) {
  static const char* names[] = {"qc",          "iu",          "silent",    "closescript", "solo",        "force",
                                "mergeoutput", "reviewconflicts", "favorleft", "favorright", "expandall",   "automerge",
                                "fv",          "title1",      "title2",    "title3",      "title4",      "filters",
                                "leftreadonly", "rightreadonly", "ro",     "lro",         "rro",         "nobackups",
                                "readonly",    "z",           "ignoreunimportant", "fileviewer", "ma", "sa",
                                "bc3", "nosplash", "keyfile", "viewer", "lefttitle", "righttitle", "centertitle",
                                "outputtitle", "savetarget", "defaultencoding"};
  std::string n = a.substr(1);
  size_t e = n.find_first_of("=:");
  if (e != std::string::npos) n = n.substr(0, e);
  n = toLower(n);
  for (auto s : names)
    if (n == s) return true;
  return false;
}

Cli parseArgs(const std::vector<std::string>& args) {
  Cli c;
  c.text.style = "unified";
  c.show.hideEmptyDirs = false;
  size_t i = 0;
  const std::set<std::string> cmds = {"compare", "diff", "dir", "hex", "merge", "sync", "script", "session", "gui", "help", "version"};
  if (!args.empty() && cmds.count(args[0])) { c.cmd = args[0]; i = 1; }
  bool dashdash = false;
  auto val = [&](const std::string& opt, std::string inlineVal, bool hasInline) -> std::string {
    if (hasInline) return inlineVal;
    if (i + 1 >= args.size()) usageError("option " + opt + " needs a value");
    return args[++i];
  };
  for (; i < args.size(); i++) {
    std::string a = args[i];
    if (dashdash || a == "-" || a.empty()) { c.pos.push_back(a); continue; }
    if (a == "--") { dashdash = true; continue; }
    if (a[0] == '@') { c.pos.push_back(a); continue; }
    if (a[0] == '/' && a.size() > 1 && isKnownSlashSwitch(a)) {
      std::string n = a.substr(1);
      size_t e = n.find_first_of("=:");
      std::string key = toLower(e == std::string::npos ? n : n.substr(0, e));
      std::string v = e == std::string::npos ? "" : n.substr(e + 1);
      if (key == "qc") c.qc = true;
      else if (key == "iu" || key == "ignoreunimportant") c.diff.ignoreUnimportant = true;
      else if (key == "force") c.force = true;
      else if (key == "mergeoutput" || key == "savetarget") c.output = v;
      else if (key == "fv" || key == "fileviewer" || key == "viewer") c.fv = toLower(v);
      else if (key == "title1" || key == "lefttitle") c.title1 = v;
      else if (key == "title2" || key == "righttitle") c.title2 = v;
      else if (key == "filters") { c.dir.filter.parse(v); }
      else if (key == "silent") c.quiet = true;
      continue;  // others: accepted, ignored
    }
    if (a.size() > 2 && a[0] == '-' && a[1] == '-') {
      std::string name = a.substr(2), v;
      bool hasV = false;
      size_t e = name.find('=');
      if (e != std::string::npos) { v = name.substr(e + 1); name = name.substr(0, e); hasV = true; }
      auto V = [&]() { return val(a, v, hasV); };
      if (name == "ignore-case") c.diff.ignoreCase = true;
      else if (name == "ignore-all-space") c.diff.ignoreWsAll = true;
      else if (name == "ignore-space-change") c.diff.ignoreWsAmount = true;
      else if (name == "ignore-leading-space") c.diff.ignoreWsLeading = true;
      else if (name == "ignore-trailing-space") c.diff.ignoreWsTrailing = true;
      else if (name == "ignore-eol") c.diff.ignoreEol = true;
      else if (name == "no-ignore-eol") c.diff.ignoreEol = false;
      else if (name == "ignore-blank-lines") c.diff.ignoreBlankLines = true;
      else if (name == "ignore-regex") c.diff.ignoreRegexes.push_back(V());
      else if (name == "ignore-unimportant") c.diff.ignoreUnimportant = true;
      else if (name == "align") { if (!parseAlign(V(), c.diff.align)) usageError("bad --align value"); }
      else if (name == "encoding") { Enc e2; if (!parseEnc(V(), e2)) usageError("bad encoding"); c.lenc = c.renc = e2; }
      else if (name == "left-encoding") { if (!parseEnc(V(), c.lenc)) usageError("bad encoding"); }
      else if (name == "right-encoding") { if (!parseEnc(V(), c.renc)) usageError("bad encoding"); }
      else if (name == "style") { c.text.style = toLower(V()); c.styleSet = true; if (!validTextStyle(c.text.style)) usageError("bad --style"); }
      else if (name == "report-style") { c.folder.style = toLower(V()); c.folderStyleSet = true; if (!validFolderStyle(c.folder.style)) usageError("bad --report-style"); }
      else if (name == "context") c.text.context = atoi(V().c_str());
      else if (name == "only-diffs") c.text.onlyDiffs = true;
      else if (name == "width") c.text.width = atoi(V().c_str());
      else if (name == "no-line-numbers") c.text.lineNumbers = false;
      else if (name == "output") c.output = V();
      else if (name == "quiet") c.quiet = true;
      else if (name == "criteria") { if (!parseCriteria(V(), c.dir)) usageError("bad --criteria"); }
      else if (name == "rules") { parseCriteria("rules", c.dir); }
      else if (name == "filter") c.dir.filter.parse(V());
      else if (name == "no-recursive") c.dir.recursive = false;
      else if (name == "depth") c.dir.maxDepth = atoi(V().c_str());
      else if (name == "tolerance") c.dir.timeTolerance = atoll(V().c_str());
      else if (name == "ignore-dst") c.dir.ignoreDst = true;
      else if (name == "ignore-name-case") c.dir.icaseNames = true;
      else if (name == "follow-symlinks") c.dir.followSymlinks = true;
      else if (name == "no-orphan-children") c.dir.orphanChildren = false;
      else if (name == "show") { bool h = c.show.hideEmptyDirs; if (!c.show.parse(V())) usageError("bad --show"); c.show.hideEmptyDirs = h; }
      else if (name == "prune") c.show.hideEmptyDirs = true;
      else if (name == "no-summary") c.folder.summary = false;
      else if (name == "full-paths") c.folder.fullPaths = true;
      else if (name == "mode") c.mode = V();
      else if (name == "dry-run") c.dryRun = true;
      else if (name == "no-empty-folders") c.emptyFolders = false;
      else if (name == "resolve") {
        std::string r = toLower(V());
        if (r == "left") c.resolve = Resolve::Left;
        else if (r == "right") c.resolve = Resolve::Right;
        else if (r == "both") c.resolve = Resolve::Both;
        else usageError("bad --resolve value");
      } else if (name == "force") c.force = true;
      else if (name == "sessions") c.sessionsPath = V();
      else if (name == "port") c.port = std::atoi(V().c_str());
      else if (name == "no-open") c.noOpen = true;
      else if (name == "view") c.view = toLower(V());
      else if (name == "title1") c.title1 = V();
      else if (name == "title2") c.title2 = V();
      else if (name == "help") c.cmd = "help";
      else if (name == "version") c.cmd = "version";
      else usageError("unknown option --" + name);
      continue;
    }
    if (a.size() >= 2 && a[0] == '-') {
      std::string o = a.substr(1);
      if (o == "i") c.diff.ignoreCase = true;
      else if (o == "w") c.diff.ignoreWsAll = true;
      else if (o == "b") c.diff.ignoreWsAmount = true;
      else if (o == "B") c.diff.ignoreBlankLines = true;
      else if (o == "u") { c.text.style = "unified"; c.styleSet = true; }
      else if (o == "c") { c.text.style = "context"; c.styleSet = true; }
      else if (o == "n") { c.text.style = "normal"; c.styleSet = true; }
      else if (o == "q") c.quiet = true;
      else if (o == "r") {}
      else if (o == "U") { if (i + 1 >= args.size()) usageError("-U needs a value"); c.text.context = atoi(args[++i].c_str()); c.text.style = "unified"; c.styleSet = true; }
      else if (o == "o") { if (i + 1 >= args.size()) usageError("-o needs a value"); c.output = args[++i]; }
      else if (o == "v") c.cmd = "version";
      else if (o == "h") c.cmd = "help";
      else usageError("unknown option " + a);
      continue;
    }
    c.pos.push_back(a);
  }
  return c;
}

// ---- helpers ------------------------------------------------------------------
bool isDir(const std::string& p) { std::error_code ec; return fs::is_directory(toPath(p), ec); }
bool isFile(const std::string& p) { std::error_code ec; return fs::is_regular_file(toPath(p), ec); }

int emit(const Cli& c, const std::string& data) {
  std::string err;
  if (!emitOutput(c.output, data, std::cout, &err)) {
    std::cerr << "mbc: cannot write output: " << err << "\n";
    return EXIT_ERROR;
  }
  return 0;
}

std::string defaultSessionsPath(const Cli& c, const std::string& argv0) {
  if (!c.sessionsPath.empty()) return c.sessionsPath;
  if (const char* e = std::getenv("MBC_SESSIONS")) return e;
  // Default: BCSessions.xml in the current working directory of the process (created on demand).
  (void)argv0;
  std::error_code ec;
  fs::path cwd = fs::current_path(ec);
  if (ec) return "BCSessions.xml";
  return fromPath(cwd / "BCSessions.xml");
}

// ---- commands -----------------------------------------------------------------
int doHex(const Cli& c, const std::string& l, const std::string& r) {
  std::string a, b, err;
  if (!readFile(l, a, &err) || !readFile(r, b, &err)) { std::cerr << "mbc: " << err << "\n"; return EXIT_ERROR; }
  HexResult h = hexCompare(a, b);
  if (!c.quiet && !c.qc) {
    HexDumpOptions o;
    o.contextRows = c.text.context;  // rows of context around differences (use --context -1 for everything)
    if (h.equal) {
      std::ostringstream os;
      os << "Files are identical (" << a.size() << " bytes)\n";
      if (int rc = emit(c, os.str())) return rc;
    } else {
      std::ostringstream os;
      os << "Left : " << l << " (" << h.sizeL << " bytes)\nRight: " << r << " (" << h.sizeR << " bytes)\n";
      os << h.diffBytes << " differing byte(s) in " << h.ranges.size() << " range(s)\n";
      if (h.ranges.size() <= 20) {
        for (auto& g : h.ranges) os << "  offset 0x" << std::hex << g.offset << std::dec << " length " << g.length << "\n";
      }
      os << hexDump(a, b, o);
      if (int rc = emit(c, os.str())) return rc;
    }
  }
  return h.equal ? EXIT_BINARY_SAME : EXIT_BINARY_DIFF;
}

int doText(Cli& c, const std::string& l, const std::string& r, bool defaultPrint) {
  std::string a, b, err;
  if (!readFile(l, a, &err) || !readFile(r, b, &err)) { std::cerr << "mbc: " << err << "\n"; return EXIT_ERROR; }
  bool eq = a == b;
  if (c.qc) return eq ? EXIT_BINARY_SAME : EXIT_BINARY_DIFF;
  if (c.fv == "hex compare" || c.fv == "hex") return doHex(c, l, r);
  bool bin = (looksBinary(a) || looksBinary(b)) && c.lenc == Enc::Auto && c.renc == Enc::Auto && c.fv != "text compare";
  if (bin) return doHex(c, l, r);
  TextCompare tc;
  if (!compareTextFiles(l, r, c.diff, tc, &err, c.lenc, c.renc)) { std::cerr << "mbc: " << err << "\n"; return EXIT_ERROR; }
  int code = tc.bytesEqual ? EXIT_BINARY_SAME : tc.diff.importantSame() ? EXIT_RULES_SAME : EXIT_RULES_DIFF;
  if (!c.quiet && (defaultPrint || !c.output.empty())) {
    c.text.leftTitle = c.title1.empty() ? l : c.title1;
    c.text.rightTitle = c.title2.empty() ? r : c.title2;
    std::string rep;
    if (code == EXIT_BINARY_SAME && !c.styleSet) rep = "Files are identical.\n";
    else rep = textReport(tc, c.text);
    if (c.text.style == "unified" || c.text.style == "context" || c.text.style == "normal") {
      if (rep.empty() && !c.output.empty()) rep = "";
    }
    if (int rc = emit(c, rep)) return rc;
  }
  return code;
}

int doDir(Cli& c, const std::string& l, const std::string& r, bool print) {
  DirResult res;
  std::string err;
  if (c.qc) { c.dir.cmpSize = c.dir.cmpTime = false; c.dir.cmpRules = false; c.dir.cmpBinary = true; }
  if (!compareDirs(l, r, c.dir, res, &err)) { std::cerr << "mbc: " << err << "\n"; return EXIT_ERROR; }
  if (!err.empty()) std::cerr << "mbc: warning: " << err;
  bool identical = true, similarOnly = true;
  for (auto& e : res.entries) {
    if (e.status == DStatus::Same) continue;
    identical = false;
    if (e.status != DStatus::Similar && !(e.isDir() && e.status == DStatus::Similar)) similarOnly = false;
  }
  if (res.summary.errors) return EXIT_ERROR;
  if (print && !c.quiet && !c.qc) {
    c.folder.show = c.show;
    if (!c.folderStyleSet) c.folder.style = "tree";
    if (int rc = emit(c, folderReport(res, c.folder))) return rc;
  }
  if (identical) return c.dir.cmpRules ? EXIT_RULES_SAME : EXIT_BINARY_SAME;
  if (similarOnly) return EXIT_RULES_SAME;
  return c.dir.cmpRules ? EXIT_RULES_DIFF : EXIT_BINARY_DIFF;
}

int doSync(Cli& c, const std::string& l, const std::string& r) {
  SyncMode mode;
  if (!parseSyncMode(c.mode, mode)) { std::cerr << "mbc: bad --mode '" << c.mode << "'\n"; return EXIT_ERROR; }
  DirResult res;
  std::string err;
  if (!compareDirs(l, r, c.dir, res, &err)) { std::cerr << "mbc: " << err << "\n"; return EXIT_ERROR; }
  auto ops = planSync(res, mode, c.emptyFolders, false);
  int bad = runOps(ops, c.dryRun, [&](const std::string& s) { if (!c.quiet) std::cout << s << "\n"; });
  if (!c.quiet) std::cout << ops.size() << " operation(s)" << (c.dryRun ? " planned (dry run)" : " executed") << (bad ? ", " + std::to_string(bad) + " failed" : "") << "\n";
  return bad ? EXIT_ERROR : 0;
}

int doMerge(Cli& c) {
  if (c.pos.size() < 3) usageError("merge needs LEFT RIGHT BASE");
  const std::string &l = c.pos[0], &r = c.pos[1], &b = c.pos[2];
  if (c.pos.size() > 3 && c.output.empty()) c.output = c.pos[3];
  TextFile L, R, B;
  std::string err;
  if (!loadTextFile(l, L, &err, c.lenc) || !loadTextFile(r, R, &err, c.renc) || !loadTextFile(b, B, &err)) {
    std::cerr << "mbc: " << err << "\n";
    return EXIT_ERROR;
  }
  MergeOptions mo;
  mo.diff = c.diff;
  mo.resolve = c.resolve;
  mo.markConflicts = c.force;
  mo.leftLabel = l;
  mo.rightLabel = r;
  mo.baseLabel = b;
  MergeResult m = merge3(B, L, R, mo);
  std::string data = serializeText(m.output);
  bool conflicts = m.conflicts > 0;
  if (!conflicts || c.force) {
    if (c.output.empty()) { std::cout << data; }
    else if (!writeFile(c.output, data, &err)) { std::cerr << "mbc: " << err << "\n"; return EXIT_ERROR; }
  }
  if (!c.quiet)
    std::cerr << "merge: " << m.autoMerged << " auto-merged, " << m.resolved << " resolved, " << m.conflicts << " conflict(s)"
              << (conflicts && !c.force ? " - output not saved (use --force to write conflict markers or --resolve)" : "") << "\n";
  if (conflicts) return c.force ? EXIT_CONFLICTS : EXIT_CONFLICTS_UNSAVED;
  return 0;
}

int doScript(Cli& c, const std::string& argv0) {
  if (c.pos.empty()) usageError("script needs a file");
  std::string path = c.pos[0];
  if (path[0] == '@') path = path.substr(1);
  std::string text, err;
  if (path == "-") { std::ostringstream ss; ss << std::cin.rdbuf(); text = ss.str(); }
  else if (!readFile(path, text, &err)) { std::cerr << "mbc: " << err << "\n"; return EXIT_ERROR; }
  SessionStore ss;
  std::string sp = defaultSessionsPath(c, argv0), e2;
  bool have = ss.load(sp, &e2);
  ScriptEnv env;
  env.args.assign(c.pos.begin() + 1, c.pos.end());
  env.sessions = have ? &ss : nullptr;
  env.diff = c.diff;
  env.dir = c.dir;
  env.lenc = c.lenc;
  env.renc = c.renc;
  env.dryRun = c.dryRun;
  return runScript(text, env);
}

int doSession(Cli& c, const std::string& argv0) {
  if (c.pos.empty()) usageError("session needs a sub command: list|show|run|save|delete");
  std::string sub = c.pos[0];
  std::string sp = defaultSessionsPath(c, argv0), err;
  SessionStore ss;
  std::error_code fec;
  bool exists = fs::exists(toPath(sp), fec);
  // a missing sessions file is just an empty list (it is created by 'save')
  bool loaded = !exists || ss.load(sp, &err);
  if (!loaded && sub != "save") { std::cerr << "mbc: cannot read sessions '" << sp << "': " << err << "\n"; return EXIT_ERROR; }
  if (sub == "list") {
    for (auto& s : ss.items)
      std::cout << s.type << "\t" << (s.folder.empty() ? "" : s.folder + "/") << s.name << "\t" << s.left << "\t" << s.right << "\n";
    return 0;
  }
  if (c.pos.size() < 2) usageError("session " + sub + " needs a name");
  const std::string& name = c.pos[1];
  if (sub == "show" || sub == "run") {
    const Session* s = ss.find(name);
    if (!s) { std::cerr << "mbc: session not found: " << name << "\n"; return EXIT_ERROR; }
    if (sub == "show") {
      std::cout << "name : " << s->name << "\ntype : " << s->type << "\nleft : " << s->left << "\nright: " << s->right << "\nmodified: " << s->lastModified << "\n";
      for (auto& e : s->extra) std::cout << e.first << " : " << e.second << "\n";
      return 0;
    }
    std::string l = nativeSessionPath(s->left), r = nativeSessionPath(s->right);
    if (s->isDir() || (isDir(l) && isDir(r))) return doDir(c, l, r, true);
    return doText(c, l, r, true);
  }
  if (sub == "save") {
    if (c.pos.size() < 4) usageError("session save NAME LEFT RIGHT");
    Session s;
    s.name = name;
    s.left = c.pos[2];
    s.right = c.pos[3];
    s.type = isDir(s.left) ? "TDirCompareSession" : "TTextCompareSession";
    s.lastModified = formatTime((int64_t)time(nullptr));
    ss.put(s);
    if (!ss.save(sp, &err)) { std::cerr << "mbc: " << err << "\n"; return EXIT_ERROR; }
    return 0;
  }
  if (sub == "delete") {
    if (!ss.remove(name)) { std::cerr << "mbc: session not found: " << name << "\n"; return EXIT_ERROR; }
    if (!ss.save(sp, &err)) { std::cerr << "mbc: " << err << "\n"; return EXIT_ERROR; }
    return 0;
  }
  usageError("unknown session sub command " + sub);
}

// True when the program was started by double-clicking it in Explorer: Windows then creates a fresh
// console that only this process is attached to.  Started from cmd / PowerShell / a script the console
// is shared with the parent, so the count is > 1.
static bool startedFromExplorer() {
#ifdef _WIN32
  DWORD pid = 0;
  return GetConsoleProcessList(&pid, 1) == 1;
#else
  return false;
#endif
}

int realMain(const std::vector<std::string>& args) {
  Cli c = parseArgs(std::vector<std::string>(args.begin() + (args.empty() ? 0 : 1), args.end()));
  std::string argv0 = args.empty() ? "" : args[0];
  // No arguments + double-click: open the GUI instead of printing the help and closing the window.
  if (c.cmd.empty() && c.pos.empty() && startedFromExplorer()) c.cmd = "gui";
  if (c.cmd == "help" || (c.cmd.empty() && c.pos.empty())) { std::cout << kHelp; return 0; }
  if (c.cmd == "version") { std::cout << kVersion << "\n"; return 0; }
  if (c.cmd == "gui") {
    GuiOptions g;
    g.port = c.port;
    g.openBrowser = !c.noOpen && !std::getenv("MBC_NO_OPEN");
    g.sessionsPath = defaultSessionsPath(c, argv0);
    if (c.pos.size() > 0) g.startLeft = c.pos[0];
    if (c.pos.size() > 1) g.startRight = c.pos[1];
    if (c.pos.size() > 2) { g.startBase = c.pos[1]; g.startRight = c.pos[2]; g.startLeft = c.pos[0]; }
    g.startView = c.view;
    if (g.startView.empty() && c.pos.size() > 2) g.startView = "merge";
    return runGui(g);
  }
  if (c.cmd == "merge") return doMerge(c);
  if (c.cmd == "script") return doScript(c, argv0);
  if (c.cmd.empty() && !c.pos.empty() && c.pos[0][0] == '@') {
    std::string p = c.pos[0].substr(1);
    if (isFile(p)) { c.cmd = "script"; return doScript(c, argv0); }
    Cli c2 = c;
    c2.pos = {"run", p};
    return doSession(c2, argv0);
  }
  if (c.cmd == "session") return doSession(c, argv0);

  bool three = c.cmd.empty() && (c.pos.size() >= 3 || (!c.output.empty() && c.pos.size() >= 3));
  if (three) {
    c.cmd = "merge";
    return doMerge(c);
  }
  if (c.pos.size() < 2) usageError("two paths are required");
  const std::string &l = c.pos[0], &r = c.pos[1];
  std::error_code ec;
  if (!fs::exists(toPath(l), ec)) { std::cerr << "mbc: no such file or folder: " << l << "\n"; return EXIT_ERROR; }
  if (!fs::exists(toPath(r), ec)) { std::cerr << "mbc: no such file or folder: " << r << "\n"; return EXIT_ERROR; }
  bool ld = isDir(l), rd = isDir(r);
  if (c.cmd == "sync") {
    if (!ld || !rd) { std::cerr << "mbc: sync needs two folders\n"; return EXIT_ERROR; }
    return doSync(c, l, r);
  }
  if (ld != rd) { std::cerr << "mbc: cannot compare a folder with a file\n"; return EXIT_ERROR; }
  if (c.cmd == "hex") return doHex(c, l, r);
  if (ld) {
    if (c.cmd == "diff") { std::cerr << "mbc: 'diff' needs two files (use 'dir' for folders)\n"; return EXIT_ERROR; }
    return doDir(c, l, r, true);
  }
  if (c.cmd == "dir") { std::cerr << "mbc: 'dir' needs two folders\n"; return EXIT_ERROR; }
  if (c.cmd == "diff") return doText(c, l, r, true);
  // auto: BComp opens a viewer; on the console we print a report unless quiet.
  if (!c.styleSet) c.text.style = "unified";
  return doText(c, l, r, true);
}

}  // namespace

#ifdef _WIN32
static std::string wideToUtf8(const wchar_t* w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(n > 0 ? n - 1 : 0, '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
  return s;
}
#endif

int main(int argc, char** argv) {
  std::vector<std::string> args;
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
  int wc = 0;
  wchar_t** wv = CommandLineToArgvW(GetCommandLineW(), &wc);
  for (int i = 0; i < wc; i++) args.push_back(wideToUtf8(wv[i]));
  (void)argc; (void)argv;
#else
  for (int i = 0; i < argc; i++) args.push_back(argv[i]);
#endif
  try {
    return realMain(args);
  } catch (const std::exception& e) {
    std::cerr << "mbc: error: " << e.what() << "\n";
    return EXIT_ERROR;
  }
}

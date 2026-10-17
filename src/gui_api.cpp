// JSON API of the local web GUI (implemented on top of the compare engine).
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

#include "common.h"
#include "diff.h"
#include "dircmp.h"
#include "gui_api.h"
#include "hexcmp.h"
#include "json.h"
#include "merge.h"
#include "report.h"
#include "session.h"
#include "textfile.h"

namespace bc {

namespace {

Json okJson() {
  Json j = Json::object();
  j["ok"] = true;
  return j;
}
Json errJson(const std::string& m) {
  Json j = Json::object();
  j["ok"] = false;
  j["error"] = m;
  return j;
}

// ---------------------------------------------------------------- option parsing
DiffOptions parseDiffOpts(const Json& j) {
  DiffOptions o;
  o.ignoreCase = j.at("ignoreCase").boolean(false);
  std::string ws = toLower(j.at("ws").str("none"));
  if (ws == "all") o.ignoreWsAll = true;
  else if (ws == "amount") o.ignoreWsAmount = true;
  else if (ws == "leading") o.ignoreWsLeading = true;
  else if (ws == "trailing") o.ignoreWsTrailing = true;
  else if (ws == "leadtrail" || ws == "both") o.ignoreWsLeading = o.ignoreWsTrailing = true;
  o.ignoreEol = j.at("ignoreEol").boolean(true);
  o.ignoreBlankLines = j.at("ignoreBlankLines").boolean(false);
  o.ignoreUnimportant = j.at("ignoreUnimportant").boolean(false);
  const Json& rx = j.at("regexes");
  for (auto& r : rx.a)
    if (!r.str().empty()) o.ignoreRegexes.push_back(r.str());
  Align al;
  if (parseAlign(j.at("align").str("smart"), al)) o.align = al;
  return o;
}

bool parseDirOpts(const Json& j, DirCompareOptions& o, std::string& err) {
  o.recursive = j.at("recursive").boolean(true);
  o.maxDepth = (int)j.at("depth").num(-1);
  std::string crit = j.at("criteria").str("binary");
  if (!parseCriteria(crit, o)) {
    err = "unknown comparison criteria: " + crit;
    return false;
  }
  o.timeTolerance = j.at("tolerance").i64(0);
  o.ignoreDst = j.at("ignoreDst").boolean(false);
  o.icaseNames = j.at("icaseNames").boolean(false);
  o.orphanChildren = j.at("orphanChildren").boolean(true);
  std::string f = j.at("filter").str("");
  if (!trim(f).empty() && trim(f) != "*") o.filter.parse(f);
  if (o.cmpRules) o.rules = parseDiffOpts(j.at("rules"));
  return true;
}

bool encFromName(const std::string& n, Enc& e) {
  std::string l = toLower(n);
  if (l.empty() || l == "auto") { e = Enc::Auto; return true; }
  return parseEnc(n, e);
}

// ---------------------------------------------------------------- text sides
struct Side {
  TextFile tf;
  std::string path, raw;
  bool haveRaw = false;
};

// returns false on error; binary=true when the file looks binary (and no encoding was forced)
bool loadSide(const Json& j, Side& s, std::string& err, bool& binary, bool forceText) {
  s.path = j.at("path").str();
  Enc enc = Enc::Auto;
  if (!encFromName(j.at("enc").str("auto"), enc)) {
    err = "unknown encoding: " + j.at("enc").str();
    return false;
  }
  if (j.at("lines").t == Json::Arr) {
    const Json& L = j.at("lines");
    std::string eols = j.at("eols").str();
    s.tf.enc = enc == Enc::Auto ? Enc::UTF8 : enc;
    Eol def = Eol::LF;
    const std::string en = j.at("eol").str("LF");
    if (en == "CRLF") def = Eol::CRLF;
    else if (en == "CR") def = Eol::CR;
    s.tf.dominantEol = def;
    for (size_t i = 0; i < L.a.size(); i++) {
      Line ln;
      ln.text = L.a[i].str();
      char c = i < eols.size() ? eols[i] : (i + 1 == L.a.size() ? 'n' : 'L');
      ln.eol = c == 'n' ? Eol::None : c == 'C' ? Eol::CRLF : c == 'R' ? Eol::CR : Eol::LF;
      s.tf.lines.push_back(std::move(ln));
    }
    return true;
  }
  if (s.path.empty()) {
    s.tf.enc = enc == Enc::Auto ? Enc::UTF8 : enc;
    return true;
  }
  if (!readFile(s.path, s.raw, &err)) return false;
  s.haveRaw = true;
  if (!forceText && enc == Enc::Auto && looksBinary(s.raw)) {
    binary = true;
    return true;
  }
  s.tf = parseText(s.raw, enc);
  return true;
}

char eolChar(Eol e) { return e == Eol::None ? 'n' : e == Eol::CRLF ? 'C' : e == Eol::CR ? 'R' : 'L'; }

Json sideJson(const Side& s) {
  Json j = Json::object();
  j["path"] = s.path;
  switch (s.tf.enc) {
    case Enc::UTF8BOM: j["enc"] = "utf-8-bom"; break;
    case Enc::UTF16LE: j["enc"] = "utf-16le"; break;
    case Enc::UTF16BE: j["enc"] = "utf-16be"; break;
    case Enc::ANSI: j["enc"] = "ansi"; break;
    default: j["enc"] = "utf-8"; break;
  }
  j["eol"] = s.tf.dominantEol == Eol::CRLF ? "CRLF" : s.tf.dominantEol == Eol::CR ? "CR" : "LF";
  j["mixedEol"] = s.tf.mixedEol;
  j["count"] = s.tf.lines.size();
  std::string eols;
  Json& L = j["lines"];
  L = Json::array();
  L.a.reserve(s.tf.lines.size());
  for (auto& l : s.tf.lines) {
    L.a.emplace_back(l.text);
    eols += eolChar(l.eol);
  }
  j["eols"] = eols;
  return j;
}

Json apiText(const Json& rq) {
  DiffOptions opt = parseDiffOpts(rq.at("opts"));
  Side L, R;
  std::string err;
  bool lb = false, rb = false;
  bool force = rq.at("forceText").boolean(false);
  if (!loadSide(rq.at("left"), L, err, lb, force)) return errJson(err);
  if (!loadSide(rq.at("right"), R, err, rb, force)) return errJson(err);
  if (lb || rb) {
    Json j = okJson();
    j["binary"] = true;
    return j;
  }
  DiffResult d = diffLines(L.tf.lines, R.tf.lines, opt);
  Json j = okJson();
  j["binary"] = false;
  j["left"] = sideJson(L);
  j["right"] = sideJson(R);
  j["bytesEqual"] = L.haveRaw && R.haveRaw && L.raw == R.raw;
  Json& rows = j["rows"];
  rows = Json::array();
  rows.a.reserve(d.rows.size() * 3);
  for (auto& r : d.rows) {
    rows.a.emplace_back(r.l);
    rows.a.emplace_back(r.r);
    rows.a.emplace_back((int)r.kind + (r.minor ? 8 : 0));
  }
  // inline (character level) differences, positions are JavaScript string indexes
  Json& inl = j["inline"];
  inl = Json::object();
  size_t budget = 20000;
  for (size_t i = 0; i < d.rows.size() && budget; i++) {
    auto& r = d.rows[i];
    if (r.l < 0 || r.r < 0 || r.kind == RowKind::Same) continue;
    const std::string& a = L.tf.lines[r.l].text;
    const std::string& b = R.tf.lines[r.r].text;
    if (a.size() > 4000 || b.size() > 4000) continue;
    InlineDiff id = inlineDiff(a, b, opt);
    if (id.left.empty() && id.right.empty()) continue;
    budget--;
    Json e = Json::array();
    Json la = Json::array(), ra = Json::array();
    for (auto& s : id.left) { la.push((int64_t)utf16Units(a, s.begin)); la.push((int64_t)utf16Units(a, s.end)); }
    for (auto& s : id.right) { ra.push((int64_t)utf16Units(b, s.begin)); ra.push((int64_t)utf16Units(b, s.end)); }
    e.push(std::move(la));
    e.push(std::move(ra));
    inl[std::to_string(i)] = std::move(e);
  }
  Json& sec = j["sections"];
  sec = Json::array();
  for (auto& s : d.sections) {
    Json e = Json::array();
    e.push((int64_t)s.begin);
    e.push((int64_t)s.end);
    e.push(s.important);
    sec.push(std::move(e));
  }
  Json& st = j["stats"];
  st = Json::object();
  st["same"] = d.nSame;
  st["unimportant"] = d.nUnimportant;
  st["different"] = d.nDifferent;
  st["leftOnly"] = d.nLeftOnly;
  st["rightOnly"] = d.nRightOnly;
  st["importantSections"] = d.importantSections();
  st["identical"] = d.identical();
  st["importantSame"] = d.importantSame();
  return j;
}

// UTF-8 text coming from the browser -> bytes written for the file's encoding
std::string toFileBytes(const TextFile& tf, Enc enc) {
  if (enc != Enc::ANSI) return serializeText(tf, enc);
  // ANSI text is held as raw bytes; the browser got them as Latin-1 code points -> map back
  TextFile t2 = tf;
  for (auto& l : t2.lines) {
    std::string out;
    bool ok = true;
    for (size_t i = 0; i < l.text.size();) {
      unsigned char c = l.text[i];
      if (c < 0x80) { out += (char)c; i++; }
      else if ((c & 0xE0) == 0xC0 && i + 1 < l.text.size() && (l.text[i + 1] & 0xC0) == 0x80) {
        unsigned cp = ((c & 0x1F) << 6) | (l.text[i + 1] & 0x3F);
        if (cp > 0xFF) { ok = false; break; }
        out += (char)cp;
        i += 2;
      } else { ok = false; break; }
    }
    if (ok) l.text = out;
  }
  return serializeText(t2, enc);
}

Json apiSave(const Json& rq) {
  std::string path = rq.at("path").str();
  if (path.empty()) return errJson("no target path");
  Enc enc = Enc::UTF8;
  if (!encFromName(rq.at("enc").str("utf-8"), enc) || enc == Enc::Auto) enc = Enc::UTF8;
  TextFile tf;
  tf.enc = enc;
  std::string eols = rq.at("eols").str();
  const Json& L = rq.at("lines");
  Eol dom = Eol::LF;
  bool domSet = false;
  for (size_t i = 0; i < L.a.size(); i++) {
    Line ln;
    ln.text = L.a[i].str();
    char c = i < eols.size() ? eols[i] : (i + 1 == L.a.size() ? 'n' : 'L');
    ln.eol = c == 'n' ? Eol::None : c == 'C' ? Eol::CRLF : c == 'R' ? Eol::CR : Eol::LF;
    if (!domSet && ln.eol != Eol::None) { dom = ln.eol; domSet = true; }
    tf.lines.push_back(std::move(ln));
  }
  tf.dominantEol = dom;
  std::string bytes = toFileBytes(tf, enc);
  std::string err;
  if (!writeFile(path, bytes, &err)) return errJson(err);
  Json j = okJson();
  j["bytes"] = bytes.size();
  j["mtime"] = fileMTime(toPath(path));
  return j;
}

// ---------------------------------------------------------------- hex
Json apiHexOpen(const Json& rq) {
  std::string a, b, err;
  if (!readFile(rq.at("left").str(), a, &err) || !readFile(rq.at("right").str(), b, &err)) return errJson(err);
  HexResult h = hexCompare(a, b);
  Json j = okJson();
  j["sizeL"] = h.sizeL;
  j["sizeR"] = h.sizeR;
  j["equal"] = h.equal;
  j["diffBytes"] = h.diffBytes;
  j["nRanges"] = h.ranges.size();
  Json& r = j["ranges"];
  r = Json::array();
  size_t cap = 200000;
  for (size_t i = 0; i < h.ranges.size() && i < cap; i++) {
    r.a.emplace_back(h.ranges[i].offset);
    r.a.emplace_back(h.ranges[i].length);
  }
  j["truncated"] = h.ranges.size() > cap;
  return j;
}

Json apiHexWindow(const Json& rq) {
  uint64_t off = (uint64_t)rq.at("offset").num(0);
  uint64_t len = (uint64_t)rq.at("length").num(4096);
  if (len > (1u << 20)) len = 1u << 20;
  Json j = okJson();
  static const char* hx = "0123456789abcdef";
  for (const char* side : {"left", "right"}) {
    std::string path = rq.at(side).str();
    std::string hex;
    std::ifstream f(toPath(path), std::ios::binary);
    if (f) {
      f.seekg((std::streamoff)off);
      std::string buf(len, '\0');
      f.read(&buf[0], (std::streamsize)len);
      buf.resize((size_t)f.gcount());
      hex.reserve(buf.size() * 2);
      for (unsigned char c : buf) { hex += hx[c >> 4]; hex += hx[c & 15]; }
    }
    j[side] = hex;
  }
  return j;
}

// ---------------------------------------------------------------- merge
Json apiMerge(const Json& rq) {
  DiffOptions dopt = parseDiffOpts(rq.at("opts"));
  Side L, R, B;
  std::string err;
  bool b1 = false, b2 = false, b3 = false;
  if (!loadSide(rq.at("left"), L, err, b1, false)) return errJson(err);
  if (!loadSide(rq.at("right"), R, err, b2, false)) return errJson(err);
  if (!loadSide(rq.at("base"), B, err, b3, false)) return errJson(err);
  if (b1 || b2 || b3) return errJson("binary files cannot be merged as text");
  MergeOptions mo;
  mo.diff = dopt;
  MergeResult mr = merge3(B.tf, L.tf, R.tf, mo);
  Json j = okJson();
  j["left"] = sideJson(L);
  j["right"] = sideJson(R);
  j["base"] = sideJson(B);
  j["left"].o.erase(std::remove_if(j["left"].o.begin(), j["left"].o.end(), [](auto& p) { return p.first == "lines" || p.first == "eols"; }), j["left"].o.end());
  j["right"].o.erase(std::remove_if(j["right"].o.begin(), j["right"].o.end(), [](auto& p) { return p.first == "lines" || p.first == "eols"; }), j["right"].o.end());
  j["base"].o.erase(std::remove_if(j["base"].o.begin(), j["base"].o.end(), [](auto& p) { return p.first == "lines" || p.first == "eols"; }), j["base"].o.end());
  j["finalNl"] = L.tf.lines.empty() ? true : L.tf.lines.back().eol != Eol::None;
  j["conflicts"] = mr.conflicts;
  j["autoMerged"] = mr.autoMerged;
  Json& regs = j["regions"];
  regs = Json::array();
  auto lines = [](const std::vector<Line>& v) {
    Json a = Json::array();
    a.a.reserve(v.size());
    for (auto& l : v) a.a.emplace_back(l.text);
    return a;
  };
  static const char* kn[] = {"same", "left", "right", "both", "conflict"};
  for (auto& g : mr.regions) {
    Json e = Json::object();
    e["k"] = kn[g.kind];
    e["o"] = lines(g.out);
    if (g.kind != MergeRegion::Same) {
      e["b"] = lines(g.base);
      e["l"] = lines(g.left);
      e["r"] = lines(g.right);
    }
    regs.push(std::move(e));
  }
  return j;
}

// ---------------------------------------------------------------- folders
Json entryJson(const DirEntry& e) {
  Json j = Json::object();
  j["rel"] = e.rel;
  j["name"] = e.name;
  if (!e.relL.empty() && e.relL != e.rel) j["relL"] = e.relL;
  if (!e.relR.empty() && e.relR != e.rel) j["relR"] = e.relR;
  j["depth"] = e.depth;
  j["hasL"] = e.hasL;
  j["hasR"] = e.hasR;
  j["dirL"] = e.dirL;
  j["dirR"] = e.dirR;
  j["sizeL"] = e.sizeL;
  j["sizeR"] = e.sizeR;
  j["mtimeL"] = e.mtimeL;
  j["mtimeR"] = e.mtimeR;
  j["status"] = statusName(e.status);
  j["newer"] = e.newer == Newer::Left ? "left" : e.newer == Newer::Right ? "right" : "";
  if (!e.reason.empty()) j["reason"] = e.reason;
  if (!e.error.empty()) j["error"] = e.error;
  return j;
}

Json summaryJson(const DirSummary& s) {
  Json j = Json::object();
  j["files"] = s.files;
  j["folders"] = s.folders;
  j["same"] = s.same;
  j["different"] = s.different;
  j["similar"] = s.similar;
  j["leftOnly"] = s.leftOnly;
  j["rightOnly"] = s.rightOnly;
  j["errors"] = s.errors;
  j["mismatch"] = s.mismatch;
  j["newerLeft"] = s.newerLeft;
  j["newerRight"] = s.newerRight;
  return j;
}

bool compareFromReq(const Json& rq, DirResult& res, std::string& err) {
  DirCompareOptions o;
  if (!parseDirOpts(rq.at("opts"), o, err)) return false;
  std::string l = rq.at("left").str(), r = rq.at("right").str();
  if (l.empty() || r.empty()) { err = "both folders are required"; return false; }
  std::string w;
  if (!compareDirs(l, r, o, res, &w)) { err = w; return false; }
  return true;
}

Json apiDir(const Json& rq) {
  DirResult res;
  std::string err;
  if (!compareFromReq(rq, res, err)) return errJson(err);
  Json j = okJson();
  j["left"] = res.left;
  j["right"] = res.right;
  Json& en = j["entries"];
  en = Json::array();
  en.a.reserve(res.entries.size());
  for (auto& e : res.entries) en.a.push_back(entryJson(e));
  j["summary"] = summaryJson(res.summary);
  j["identical"] = res.identical();
  return j;
}

Json opsJson(const std::vector<Op>& ops) {
  Json a = Json::array();
  for (auto& o : ops) {
    Json e = Json::object();
    e["type"] = opName(o.type);
    e["rel"] = o.rel;
    e["src"] = o.src;
    e["dst"] = o.dst;
    e["isDir"] = o.isDir;
    e["bytes"] = o.bytes;
    a.push(std::move(e));
  }
  return a;
}

Json execOps(const std::vector<Op>& ops, bool dry) {
  Json j = okJson();
  std::vector<std::string> log;
  int failed = runOps(ops, dry, [&](const std::string& s) { log.push_back(s); });
  j["ops"] = opsJson(ops);
  j["dryRun"] = dry;
  j["failed"] = failed;
  Json& lg = j["log"];
  lg = Json::array();
  for (auto& s : log) lg.push(s);
  if (failed) j["error"] = std::to_string(failed) + " operation(s) failed";
  return j;
}

Json apiDirOp(const Json& rq) {
  DirResult res;
  std::string err;
  if (!compareFromReq(rq, res, err)) return errJson(err);
  std::set<std::string> rels;
  for (auto& r : rq.at("rels").a) rels.insert(r.str());
  for (size_t i = 0; i < res.entries.size(); i++) {
    if (!rels.count(res.entries[i].rel)) continue;
    size_t end = res.entries[i].isDir() ? std::max(res.entries[i].subtreeEnd, i + 1) : i + 1;
    for (size_t k = i; k < end && k < res.entries.size(); k++) res.entries[k].selected = true;
  }
  std::string op = rq.at("op").str();
  std::vector<Op> ops;
  if (op == "copyLR") ops = planCopySelected(res, true);
  else if (op == "copyRL") ops = planCopySelected(res, false);
  else if (op == "deleteL") ops = planDeleteSelected(res, true, false);
  else if (op == "deleteR") ops = planDeleteSelected(res, false, true);
  else if (op == "deleteBoth") ops = planDeleteSelected(res, true, true);
  else return errJson("unknown operation: " + op);
  return execOps(ops, rq.at("dryRun").boolean(true));
}

Json apiSync(const Json& rq) {
  DirResult res;
  std::string err;
  if (!compareFromReq(rq, res, err)) return errJson(err);
  SyncMode m;
  if (!parseSyncMode(rq.at("mode").str("update:lr"), m)) return errJson("unknown sync mode: " + rq.at("mode").str());
  bool onlySel = rq.at("rels").t == Json::Arr && rq.at("rels").size() > 0;
  if (onlySel) {
    std::set<std::string> rels;
    for (auto& r : rq.at("rels").a) rels.insert(r.str());
    for (size_t i = 0; i < res.entries.size(); i++) {
      if (!rels.count(res.entries[i].rel)) continue;
      size_t end = res.entries[i].isDir() ? std::max(res.entries[i].subtreeEnd, i + 1) : i + 1;
      for (size_t k = i; k < end && k < res.entries.size(); k++) res.entries[k].selected = true;
    }
  }
  auto ops = planSync(res, m, rq.at("createEmpty").boolean(true), onlySel);
  return execOps(ops, rq.at("dryRun").boolean(true));
}

Json apiDirReport(const Json& rq) {
  DirResult res;
  std::string err;
  if (!compareFromReq(rq, res, err)) return errJson(err);
  FolderReportOptions fo;
  fo.style = rq.at("style").str("html");
  if (!validFolderStyle(fo.style)) return errJson("unknown report style: " + fo.style);
  fo.show.parse(rq.at("show").str("all"));
  Json j = okJson();
  j["content"] = folderReport(res, fo);
  const std::string& s = fo.style;
  j["ext"] = s == "html" ? "html" : s == "csv" ? "csv" : s == "xml" ? "xml" : "txt";
  return j;
}

Json apiTextReport(const Json& rq) {
  DiffOptions opt = parseDiffOpts(rq.at("opts"));
  TextCompare tc;
  std::string err;
  std::string l = rq.at("left").str(), r = rq.at("right").str();
  if (!compareTextFiles(l, r, opt, tc, &err)) return errJson(err);
  TextReportOptions to;
  to.style = rq.at("style").str("html");
  if (!validTextStyle(to.style)) return errJson("unknown report style: " + to.style);
  to.leftTitle = l;
  to.rightTitle = r;
  Json j = okJson();
  j["content"] = textReport(tc, to);
  const std::string& s = to.style;
  j["ext"] = s == "html" ? "html" : (s == "unified" || s == "context" || s == "normal") ? "patch" : "txt";
  return j;
}

// ---------------------------------------------------------------- file browser
Json apiBrowse(const Json& rq) {
  std::string p = rq.at("path").str();
  Json j = okJson();
  Json& roots = j["roots"];
  roots = Json::array();
  std::error_code ec;
#ifdef _WIN32
  j["sep"] = "\\";
  for (char c = 'A'; c <= 'Z'; c++) {
    std::string d = std::string(1, c) + ":\\";
    if (fs::exists(toPath(d), ec)) roots.push(d);
  }
#else
  j["sep"] = "/";
  roots.push("/");
#endif
  const char* home = std::getenv("USERPROFILE");
  if (!home) home = std::getenv("HOME");
  if (home) j["home"] = std::string(home);
  Json& ents = j["entries"];
  ents = Json::array();
  if (trim(p).empty()) {
    j["path"] = "";
    j["parent"] = "";
    return j;
  }
  fs::path fp = toPath(p);
  if (!fs::exists(fp, ec)) return errJson("path does not exist: " + p);
  if (!fs::is_directory(fp, ec)) {
    fp = fp.parent_path();
    if (fp.empty()) return errJson("not a folder: " + p);
  }
  fs::path abs = fs::absolute(fp, ec).lexically_normal();
  std::string shown = fromPath(abs);
#ifdef _WIN32
  std::replace(shown.begin(), shown.end(), '/', '\\');
#endif
  while (shown.size() > 1 && (shown.back() == '/' || shown.back() == '\\') && !(shown.size() == 3 && shown[1] == ':')) shown.pop_back();
  j["path"] = shown;
  fs::path parent = abs.parent_path();
  j["parent"] = (parent == abs || parent.empty()) ? std::string() : fromPath(parent);
  struct Ent { std::string name; bool dir; uint64_t size; int64_t mtime; };
  std::vector<Ent> v;
  fs::directory_iterator it(abs, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    Ent e;
    e.name = fromPath(it->path().filename());
    e.dir = it->is_directory(e2);
    e.size = (!e.dir && !e2) ? (uint64_t)it->file_size(e2) : 0;
    e.mtime = fileMTime(it->path());
    v.push_back(std::move(e));
    if (v.size() >= 20000) break;
  }
  std::sort(v.begin(), v.end(), [](const Ent& a, const Ent& b) {
    if (a.dir != b.dir) return a.dir;
    return toLower(a.name) < toLower(b.name);
  });
  for (auto& e : v) {
    Json x = Json::object();
    x["name"] = e.name;
    x["dir"] = e.dir;
    x["size"] = e.size;
    x["mtime"] = e.mtime;
    ents.push(std::move(x));
  }
  return j;
}

Json apiProbe(const Json& rq) {
  std::string l = rq.at("left").str(), r = rq.at("right").str();
  std::error_code ec;
  bool ld = fs::is_directory(toPath(l), ec), rd = fs::is_directory(toPath(r), ec);
  Json j = okJson();
  if (!fs::exists(toPath(l), ec)) return errJson("not found: " + l);
  if (!r.empty() && !fs::exists(toPath(r), ec)) return errJson("not found: " + r);
  if (ld != (r.empty() ? ld : rd)) return errJson("cannot compare a file with a folder");
  if (ld) { j["kind"] = "dir"; return j; }
  std::string a, b, err;
  if (!readFile(l, a, &err)) return errJson(err);
  bool bin = looksBinary(a);
  if (!r.empty()) {
    if (!readFile(r, b, &err)) return errJson(err);
    bin = bin || looksBinary(b);
  }
  j["kind"] = bin ? "hex" : "text";
  return j;
}

Json apiMkdir(const Json& rq) {
  std::error_code ec;
  fs::create_directories(toPath(rq.at("path").str()), ec);
  if (ec) return errJson(ec.message());
  return okJson();
}

// ---------------------------------------------------------------- sessions
std::string kindOf(const Session& s) {
  if (s.type == "TDirCompareSession") return "dir";
  if (s.type == "TTextCompareSession") return "text";
  if (s.type == "THexCompareSession") return "hex";
  if (s.type.find("Merge") != std::string::npos) return "merge";
  return "other";
}
std::string typeOf(const std::string& kind) {
  if (kind == "dir") return "TDirCompareSession";
  if (kind == "text") return "TTextCompareSession";
  if (kind == "hex") return "THexCompareSession";
  if (kind == "merge") return "TTextMergeSession";
  return "";
}

Json sessionJson(const Session& s) {
  Json j = Json::object();
  j["name"] = s.name;
  j["type"] = s.type;
  j["kind"] = kindOf(s);
  j["folder"] = s.folder;
  j["left"] = nativeSessionPath(s.left);
  j["right"] = nativeSessionPath(s.right);
  j["center"] = nativeSessionPath(s.center);
  j["lastModified"] = s.lastModified;
  return j;
}

Json apiSessions(const std::string& action, const Json& rq, const std::string& path) {
  SessionStore st;
  std::string err;
  {
    std::error_code ec;
    if (fs::exists(toPath(path), ec) && !st.load(path, &err)) return errJson("cannot read sessions: " + err);
  }
  if (action == "list") {
    Json j = okJson();
    j["path"] = path;
    Json& a = j["items"];
    a = Json::array();
    for (auto& s : st.items) a.push(sessionJson(s));
    return j;
  }
  if (action == "save") {
    Session s;
    s.name = trim(rq.at("name").str());
    if (s.name.empty()) return errJson("session name is required");
    s.type = typeOf(rq.at("kind").str());
    if (s.type.empty()) return errJson("unknown session kind");
    s.folder = rq.at("folder").str();
    s.left = rq.at("left").str();
    s.right = rq.at("right").str();
    s.center = rq.at("center").str();
    s.lastModified = formatTime((int64_t)std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    for (auto& e : st.items)
      if (e.name == s.name && e.folder == s.folder) { s.extra = e.extra; break; }
    st.put(s);
  } else if (action == "record") {
    // Automatic history: every comparison the user starts is remembered in the "History" folder.
    const std::string kHist = "History";
    const size_t kMaxHist = 100;
    Session s;
    s.type = typeOf(rq.at("kind").str());
    if (s.type.empty()) return errJson("unknown session kind");
    s.left = rq.at("left").str();
    s.right = rq.at("right").str();
    s.center = rq.at("center").str();
    if (s.left.empty() || s.right.empty()) return errJson("both sides are required");
    s.folder = kHist;
    auto same = [&](const Session& e) { return e.type == s.type && e.left == s.left && e.right == s.right && e.center == s.center; };
    Json j = okJson();
    j["changed"] = false;
    // already saved by the user under a name -> nothing to record
    for (auto& e : st.items)
      if (e.folder != kHist && same(e)) return j;
    // already the newest history entry -> nothing to do (refreshes do not rewrite the file)
    for (auto& e : st.items)
      if (e.folder == kHist) { if (same(e)) return j; break; }
    size_t firstHist = st.items.size();
    for (size_t i = 0; i < st.items.size(); i++)
      if (st.items[i].folder == kHist) { firstHist = i; break; }
    for (size_t i = 0; i < st.items.size(); i++)
      if (st.items[i].folder == kHist && same(st.items[i])) {
        s.name = st.items[i].name;
        s.extra = st.items[i].extra;
        st.items.erase(st.items.begin() + (long)i);
        if (i < firstHist) firstHist--;
        break;
      }
    if (s.name.empty()) {
      auto base = [](const std::string& p) {
        std::string q = p;
        while (q.size() > 1 && (q.back() == '/' || q.back() == '\\')) q.pop_back();
        size_t k = q.find_last_of("/\\");
        return k == std::string::npos ? q : q.substr(k + 1);
      };
      std::string nm = base(s.left) + " ↔ " + base(s.right);
      if (s.type == "THexCompareSession") nm += " (hex)";
      else if (s.type == "TTextMergeSession") nm = "Merge: " + base(s.left) + " ↔ " + base(s.right);
      std::string cand = nm;
      auto taken = [&](const std::string& n) { for (auto& e : st.items) if (e.name == n) return true; return false; };
      for (int n = 2; taken(cand); n++) cand = nm + " (" + std::to_string(n) + ")";
      s.name = cand;
    }
    s.lastModified = formatTime((int64_t)std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    st.items.insert(st.items.begin() + (long)std::min(firstHist, st.items.size()), s);
    // keep only the newest kMaxHist history entries
    size_t cnt = 0;
    for (size_t i = 0; i < st.items.size();)
      if (st.items[i].folder == kHist && ++cnt > kMaxHist) st.items.erase(st.items.begin() + (long)i);
      else i++;
    if (!st.save(path, &err)) return errJson("cannot write sessions: " + err);
    j["changed"] = true;
    return j;
  } else if (action == "delete") {
    if (!st.remove(rq.at("name").str())) return errJson("no such session: " + rq.at("name").str());
  } else if (action == "rename") {
    const Session* old = st.find(rq.at("name").str());
    if (!old) return errJson("no such session: " + rq.at("name").str());
    Session c = *old;
    std::string nn = trim(rq.at("newName").str());
    if (nn.empty()) return errJson("new name is required");
    st.remove(old->name);
    c.name = nn;
    st.put(c);
  } else return errJson("unknown sessions action: " + action);
  if (!st.save(path, &err)) return errJson("cannot write sessions: " + err);
  return okJson();
}

}  // namespace

Json guiApi(const std::string& name, const Json& rq, const GuiContext& ctx) {
  try {
    if (name == "info") {
      Json j = okJson();
      j["version"] = "1.0";
      j["sessionsPath"] = ctx.sessionsPath;
#ifdef _WIN32
      j["platform"] = "windows";
#else
      j["platform"] = "posix";
#endif
      std::error_code ec;
      j["cwd"] = fromPath(fs::current_path(ec));
      j["start"] = ctx.start;
      return j;
    }
    if (name == "text") return apiText(rq);
    if (name == "save") return apiSave(rq);
    if (name == "hex/open") return apiHexOpen(rq);
    if (name == "hex/window") return apiHexWindow(rq);
    if (name == "merge") return apiMerge(rq);
    if (name == "dir") return apiDir(rq);
    if (name == "dir/op") return apiDirOp(rq);
    if (name == "dir/sync") return apiSync(rq);
    if (name == "dir/report") return apiDirReport(rq);
    if (name == "text/report") return apiTextReport(rq);
    if (name == "browse") return apiBrowse(rq);
    if (name == "probe") return apiProbe(rq);
    if (name == "mkdir") return apiMkdir(rq);
    if (name.rfind("sessions/", 0) == 0) return apiSessions(name.substr(9), rq, ctx.sessionsPath);
    return errJson("unknown API: " + name);
  } catch (const std::exception& ex) {
    return errJson(std::string("error: ") + ex.what());
  }
}

}  // namespace bc

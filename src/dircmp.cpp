#include "dircmp.h"

#include <algorithm>
#include <cstdlib>
#include <map>

#include "textfile.h"

namespace bc {

// ---------------------------------------------------------------------------------
// Name filter
// ---------------------------------------------------------------------------------
void NameFilter::parse(const std::string& s) {
  fileInc.clear(); fileExc.clear(); dirInc.clear(); dirExc.clear();
  spec = s;
  bool afterBar = false;
  std::string cur;
  auto flush = [&]() {
    std::string t = trim(cur);
    cur.clear();
    if (t.empty()) return;
    bool exc = afterBar;
    if (t[0] == '-') { exc = true; t = trim(t.substr(1)); }
    else if (t[0] == '+') { exc = false; t = trim(t.substr(1)); }
    bool isDir = false;
    if (!t.empty() && (t.back() == '\\' || t.back() == '/')) { isDir = true; t.pop_back(); }
    for (auto& c : t) if (c == '\\') c = '/';
    if (t.empty()) return;
    (isDir ? (exc ? dirExc : dirInc) : (exc ? fileExc : fileInc)).push_back(t);
  };
  for (char c : s) {
    if (c == ';') flush();
    else if (c == '|') { flush(); afterBar = true; }
    else cur += c;
  }
  flush();
}
static bool anyMatch(const std::vector<std::string>& pats, const std::string& name, const std::string& rel, bool icase) {
  for (auto& p : pats) {
    if (p.find('/') != std::string::npos) {
      if (wildMatch(p, rel, icase)) return true;
    } else if (wildMatch(p, name, icase))
      return true;
  }
  return false;
}
bool NameFilter::acceptFile(const std::string& name, const std::string& rel, bool icase) const {
  if (anyMatch(fileExc, name, rel, icase)) return false;
  if (!fileInc.empty() && !anyMatch(fileInc, name, rel, icase)) return false;
  return true;
}
bool NameFilter::acceptDir(const std::string& name, const std::string& rel, bool icase) const {
  return !anyMatch(dirExc, name, rel, icase);
}

const char* statusName(DStatus s) {
  switch (s) {
    case DStatus::Same: return "same";
    case DStatus::Different: return "different";
    case DStatus::Similar: return "similar";
    case DStatus::LeftOnly: return "left-only";
    case DStatus::RightOnly: return "right-only";
    case DStatus::TypeMismatch: return "type-mismatch";
    case DStatus::Error: return "error";
  }
  return "?";
}

bool parseCriteria(const std::string& s, DirCompareOptions& o) {
  DirCompareOptions t = o;
  t.cmpSize = t.cmpTime = t.cmpCrc = t.cmpBinary = t.cmpRules = false;
  for (auto& t0 : splitAny(s, ", +;")) {
    std::string w = toLower(t0);
    if (w == "size") t.cmpSize = true;
    else if (w == "time" || w == "timestamp" || w == "date" || w == "modified") t.cmpTime = true;
    else if (w == "crc") t.cmpCrc = true;
    else if (w == "binary" || w == "content" || w == "contents") t.cmpBinary = true;
    else if (w == "rules" || w == "rules-based" || w == "text") t.cmpRules = true;
    else if (w == "quick") { t.cmpSize = true; t.cmpTime = true; }
    else return false;
  }
  o = t;
  return true;
}

// ---------------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------------
namespace {
struct Node {
  std::string name;
  bool isDir = false;
  uint64_t size = 0;
  int64_t mtime = 0;
};

bool listDir(const std::string& path, std::vector<Node>& out, std::string* err, bool follow) {
  std::error_code ec;
  fs::directory_iterator it(toPath(path), fs::directory_options::skip_permission_denied, ec);
  if (ec) {
    if (err) *err = "cannot read folder '" + path + "': " + ec.message();
    return false;
  }
  for (; it != fs::directory_iterator(); it.increment(ec)) {
    if (ec) break;
    Node n;
    n.name = fromPath(it->path().filename());
    std::error_code e2;
    auto st = it->symlink_status(e2);
    bool link = !e2 && fs::is_symlink(st);
    if (link && follow) st = it->status(e2);
    n.isDir = !e2 && fs::is_directory(st) && !(link && !follow);
    if (!n.isDir && !e2 && !fs::is_regular_file(st) && !link) continue;  // skip sockets, devices...
    if (!n.isDir) {
      auto sz = it->file_size(e2);
      n.size = e2 ? 0 : (uint64_t)sz;
    }
    n.mtime = fileMTime(it->path());
    out.push_back(std::move(n));
  }
  return true;
}

struct Scanner {
  const DirCompareOptions& opt;
  DirResult& res;
  std::string errs;

  bool timeEq(int64_t a, int64_t b) const {
    int64_t d = a > b ? a - b : b - a;
    if (d <= opt.timeTolerance) return true;
    if (opt.ignoreDst && std::llabs(d - 3600) <= opt.timeTolerance) return true;
    return false;
  }

  void compareFiles(DirEntry& e, const std::string& lp, const std::string& rp) {
    e.status = DStatus::Same;
    bool tdiff = !timeEq(e.mtimeL, e.mtimeR);
    if (tdiff) e.newer = e.mtimeL > e.mtimeR ? Newer::Left : Newer::Right;
    if (opt.cmpSize && e.sizeL != e.sizeR) { e.status = DStatus::Different; e.reason = "size"; }
    else if (opt.cmpTime && tdiff) { e.status = DStatus::Different; e.reason = "time"; }
    else if (opt.cmpRules || opt.cmpBinary || opt.cmpCrc) {
      std::string err;
      if (opt.cmpRules) {
        std::string a, b;
        if (!readFile(lp, a, &err) || !readFile(rp, b, &err)) { e.status = DStatus::Error; e.error = err; return; }
        if (a == b) e.status = DStatus::Same;
        else if (looksBinary(a) || looksBinary(b)) { e.status = DStatus::Different; e.reason = "content"; }
        else {
          auto L = parseText(a), R = parseText(b);
          auto d = diffLines(L.lines, R.lines, opt.rules);
          if (d.importantSame()) { e.status = DStatus::Similar; e.reason = "rules"; }
          else { e.status = DStatus::Different; e.reason = "content"; }
        }
      } else if (opt.cmpBinary) {
        if (e.sizeL != e.sizeR) { e.status = DStatus::Different; e.reason = "content"; }
        else {
          bool eq = filesBinaryEqual(lp, rp, &err);
          if (!err.empty()) { e.status = DStatus::Error; e.error = err; return; }
          if (!eq) { e.status = DStatus::Different; e.reason = "content"; }
        }
      } else {
        uint32_t c1, c2;
        if (!fileCrc32(lp, c1, &err) || !fileCrc32(rp, c2, &err)) { e.status = DStatus::Error; e.error = err; return; }
        if (c1 != c2 || e.sizeL != e.sizeR) { e.status = DStatus::Different; e.reason = "crc"; }
      }
    }
    if (e.status == DStatus::Same) e.newer = Newer::None;
  }

  static bool less(const std::pair<bool, std::string>& a, const std::pair<bool, std::string>& b) {
    if (a.first != b.first) return a.first > b.first;  // folders first
    std::string la = toLower(a.second), lb = toLower(b.second);
    if (la != lb) return la < lb;
    return a.second < b.second;
  }

  void walk(const std::string& relL, const std::string& relR, const std::string* L, const std::string* R, int depth,
            bool inDirInc) {
    std::vector<Node> nl, nr;
    std::string err;
    if (L && !listDir(*L, nl, &err, opt.followSymlinks)) errs += err + "\n";
    if (R && !listDir(*R, nr, &err, opt.followSymlinks)) errs += err + "\n";
    struct Pair { int l = -1, r = -1; };
    std::map<std::string, Pair> m;
    auto key = [&](const std::string& n) { return opt.icaseNames ? toLower(n) : n; };
    for (size_t i = 0; i < nl.size(); i++) m[key(nl[i].name)].l = (int)i;
    for (size_t i = 0; i < nr.size(); i++) m[key(nr[i].name)].r = (int)i;
    std::vector<std::pair<std::pair<bool, std::string>, Pair>> items;
    for (auto& kv : m) {
      bool isDir = (kv.second.l >= 0 && nl[kv.second.l].isDir) || (kv.second.r >= 0 && nr[kv.second.r].isDir);
      std::string disp = kv.second.l >= 0 ? nl[kv.second.l].name : nr[kv.second.r].name;
      items.push_back({{isDir, disp}, kv.second});
    }
    std::sort(items.begin(), items.end(), [](auto& a, auto& b) { return less(a.first, b.first); });
    for (auto& it : items) {
      const Node* a = it.second.l >= 0 ? &nl[it.second.l] : nullptr;
      const Node* b = it.second.r >= 0 ? &nr[it.second.r] : nullptr;
      DirEntry e;
      e.name = it.first.second;
      e.depth = depth;
      e.relL = a ? (relL.empty() ? a->name : relL + "/" + a->name) : "";
      e.relR = b ? (relR.empty() ? b->name : relR + "/" + b->name) : "";
      e.rel = a ? e.relL : e.relR;
      e.hasL = a != nullptr;
      e.hasR = b != nullptr;
      e.dirL = a && a->isDir;
      e.dirR = b && b->isDir;
      if (a) { e.sizeL = a->size; e.mtimeL = a->mtime; }
      if (b) { e.sizeR = b->size; e.mtimeR = b->mtime; }
      bool anyDir = e.dirL || e.dirR;
      if (anyDir) {
        if (!opt.filter.acceptDir(e.name, e.rel, opt.icaseNames)) continue;
      } else {
        if (!inDirInc || !opt.filter.acceptFile(e.name, e.rel, opt.icaseNames)) continue;
      }
      std::string lp = a ? joinPath(*L, a->name) : "", rp = b ? joinPath(*R, b->name) : "";
      if (!e.hasR) e.status = DStatus::LeftOnly;
      else if (!e.hasL) e.status = DStatus::RightOnly;
      else if (e.dirL != e.dirR) e.status = DStatus::TypeMismatch;
      else if (!anyDir) compareFiles(e, lp, rp);
      size_t idx = res.entries.size();
      res.entries.push_back(e);
      bool recurse = anyDir && e.dirL == e.hasL && e.dirR == e.hasR && opt.recursive &&
                     (opt.maxDepth < 0 || depth + 1 < opt.maxDepth) && ((e.hasL && e.hasR) || opt.orphanChildren);
      if (recurse) {
        bool sub = inDirInc || (!opt.filter.dirInc.empty() && anyMatch(opt.filter.dirInc, e.name, e.rel, opt.icaseNames));
        walk(e.relL, e.relR, e.hasL ? &lp : nullptr, e.hasR ? &rp : nullptr, depth + 1, sub);
      }
      res.entries[idx].subtreeEnd = res.entries.size();
    }
  }
};
}  // namespace

bool DirResult::identical() const {
  for (auto& e : entries)
    if (e.status != DStatus::Same) return false;
  return true;
}

bool compareDirs(const std::string& left, const std::string& right, const DirCompareOptions& opt, DirResult& out,
                 std::string* err) {
  std::error_code ec;
  if (!fs::is_directory(toPath(left), ec)) { if (err) *err = "not a folder: " + left; return false; }
  if (!fs::is_directory(toPath(right), ec)) { if (err) *err = "not a folder: " + right; return false; }
  out = DirResult();
  out.left = left;
  out.right = right;
  Scanner sc{opt, out, {}};
  std::string l = left, r = right;
  sc.walk("", "", &l, &r, 0, opt.filter.dirInc.empty());
  auto& es = out.entries;
  // aggregate folder status from the files below
  for (size_t i = es.size(); i-- > 0;) {
    auto& e = es[i];
    if (!e.isDir()) continue;
    if (e.status == DStatus::TypeMismatch || e.status == DStatus::LeftOnly || e.status == DStatus::RightOnly) continue;
    DStatus st = DStatus::Same;
    for (size_t k = i + 1; k < e.subtreeEnd; k++) {
      DStatus s = es[k].status;
      if (es[k].isDir()) {
        if (s == DStatus::LeftOnly || s == DStatus::RightOnly || s == DStatus::TypeMismatch) { st = DStatus::Different; break; }
        continue;
      }
      if (s == DStatus::Same) continue;
      if (s == DStatus::Similar) { st = DStatus::Similar; continue; }
      st = DStatus::Different;
      break;
    }
    e.status = st;
  }
  auto& s = out.summary;
  for (auto& e : es) {
    if (e.isDir()) {
      s.folders++;
      switch (e.status) {
        case DStatus::Same: s.foldersSame++; break;
        case DStatus::LeftOnly: s.foldersLeftOnly++; break;
        case DStatus::RightOnly: s.foldersRightOnly++; break;
        default: s.foldersDifferent++;
      }
      continue;
    }
    s.files++;
    switch (e.status) {
      case DStatus::Same: s.same++; break;
      case DStatus::Different: s.different++; break;
      case DStatus::Similar: s.similar++; break;
      case DStatus::LeftOnly: s.leftOnly++; break;
      case DStatus::RightOnly: s.rightOnly++; break;
      case DStatus::TypeMismatch: s.mismatch++; break;
      case DStatus::Error: s.errors++; break;
    }
    if (e.status == DStatus::Different || e.status == DStatus::Similar) {
      if (e.newer == Newer::Left) s.newerLeft++;
      else if (e.newer == Newer::Right) s.newerRight++;
    }
  }
  if (!sc.errs.empty() && err) *err = sc.errs;
  return true;
}

// ---------------------------------------------------------------------------------
// Show filter
// ---------------------------------------------------------------------------------
bool ShowFilter::parse(const std::string& spec) {
  bool hide = hideEmptyDirs;
  auto setAll = [&](bool v) { same = diff = similar = left = right = newerLeft = newerRight = sameTimeDiff = mismatch = v; };
  setAll(false);
  bool any = false;
  for (auto& t0 : splitAny(spec, ", ;+")) {
    std::string t = toLower(t0);
    any = true;
    if (t == "all") setAll(true);
    else if (t == "diff" || t == "differences" || t == "different") { setAll(true); same = false; }
    else if (t == "same" || t == "equal" || t == "identical") same = true;
    else if (t == "similar") similar = true;
    else if (t == "left" || t == "left-only" || t == "left-orphans") left = true;
    else if (t == "right" || t == "right-only" || t == "right-orphans") right = true;
    else if (t == "orphans") left = right = true;
    else if (t == "newer-left" || t == "left-newer") newerLeft = true;
    else if (t == "newer-right" || t == "right-newer") newerRight = true;
    else if (t == "newer") newerLeft = newerRight = true;
    else if (t == "mismatch") mismatch = true;
    else { setAll(true); hideEmptyDirs = hide; return false; }
  }
  if (!any) setAll(true);
  hideEmptyDirs = hide;
  return true;
}
bool ShowFilter::matches(const DirEntry& e) const {
  switch (e.status) {
    case DStatus::Same: return same;
    case DStatus::Similar: return similar;
    case DStatus::LeftOnly: return left;
    case DStatus::RightOnly: return right;
    case DStatus::TypeMismatch: return mismatch;
    case DStatus::Error: return true;
    case DStatus::Different:
      if (e.newer == Newer::Left) return newerLeft;
      if (e.newer == Newer::Right) return newerRight;
      return sameTimeDiff;
  }
  return true;
}
std::vector<size_t> visibleEntries(const DirResult& r, const ShowFilter& f) {
  const auto& es = r.entries;
  std::vector<char> vis(es.size(), 0);
  for (size_t i = 0; i < es.size(); i++)
    if (!es[i].isDir() && f.matches(es[i])) vis[i] = 1;
  for (size_t i = es.size(); i-- > 0;) {
    if (!es[i].isDir()) continue;
    bool any = false;
    for (size_t k = i + 1; k < es[i].subtreeEnd && !any; k++)
      if (vis[k] && !es[k].isDir()) any = true;
    vis[i] = any || (!f.hideEmptyDirs && f.matches(es[i]));
  }
  std::vector<size_t> out;
  for (size_t i = 0; i < es.size(); i++)
    if (vis[i]) out.push_back(i);
  return out;
}

// ---------------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------------
const char* opName(OpType t) {
  switch (t) {
    case OpType::CopyLR: return "copy L->R";
    case OpType::CopyRL: return "copy R->L";
    case OpType::DeleteL: return "delete L";
    case OpType::DeleteR: return "delete R";
    case OpType::MkdirL: return "mkdir L";
    case OpType::MkdirR: return "mkdir R";
  }
  return "?";
}
bool parseSyncMode(const std::string& s0, SyncMode& out) {
  std::string s = toLower(s0);
  s = replaceAll(s, "left->right", "lr");
  s = replaceAll(s, "right->left", "rl");
  s = replaceAll(s, "->", "");
  if (s == "update:lr" || s == "update-lr" || s == "updatelr" || s == "update") out = SyncMode::UpdateLR;
  else if (s == "mirror:lr" || s == "mirror-lr" || s == "mirrorlr" || s == "mirror") out = SyncMode::MirrorLR;
  else if (s == "update:rl" || s == "update-rl" || s == "updaterl") out = SyncMode::UpdateRL;
  else if (s == "mirror:rl" || s == "mirror-rl" || s == "mirrorrl") out = SyncMode::MirrorRL;
  else if (s == "update:both" || s == "both" || s == "update-both" || s == "updateboth") out = SyncMode::UpdateBoth;
  else return false;
  return true;
}

static Op mkOp(const DirResult& r, OpType t, const DirEntry& e) {
  Op op;
  op.type = t;
  op.rel = e.rel;
  op.isDir = e.isDir();
  switch (t) {
    case OpType::CopyLR: op.src = joinPath(r.left, e.relL); op.dst = joinPath(r.right, e.hasR ? e.relR : e.relL); op.bytes = e.sizeL; break;
    case OpType::CopyRL: op.src = joinPath(r.right, e.relR); op.dst = joinPath(r.left, e.hasL ? e.relL : e.relR); op.bytes = e.sizeR; break;
    case OpType::DeleteL: op.src = joinPath(r.left, e.relL); break;
    case OpType::DeleteR: op.src = joinPath(r.right, e.relR); break;
    case OpType::MkdirL: op.dst = joinPath(r.left, e.relR); break;
    case OpType::MkdirR: op.dst = joinPath(r.right, e.relL); break;
  }
  return op;
}

std::vector<Op> planSync(const DirResult& r, SyncMode mode, bool createEmpty, bool onlySelected) {
  std::vector<Op> ops, dirDeletes;
  bool toR = mode == SyncMode::UpdateLR || mode == SyncMode::MirrorLR || mode == SyncMode::UpdateBoth;
  bool toL = mode == SyncMode::UpdateRL || mode == SyncMode::MirrorRL || mode == SyncMode::UpdateBoth;
  bool mirror = mode == SyncMode::MirrorLR || mode == SyncMode::MirrorRL;
  bool delL = mode == SyncMode::MirrorRL, delR = mode == SyncMode::MirrorLR;
  size_t skipTo = 0;
  for (size_t i = 0; i < r.entries.size(); i++) {
    const auto& e = r.entries[i];
    if (i < skipTo) continue;
    if (onlySelected && !e.selected) continue;
    if (e.status == DStatus::Error) continue;
    if (e.isDir()) {
      if (e.status == DStatus::LeftOnly) {
        if (toR) { if (createEmpty) ops.push_back(mkOp(r, OpType::MkdirR, e)); }
        else if (delL) { dirDeletes.push_back(mkOp(r, OpType::DeleteL, e)); skipTo = e.subtreeEnd; }
      } else if (e.status == DStatus::RightOnly) {
        if (toL) { if (createEmpty) ops.push_back(mkOp(r, OpType::MkdirL, e)); }
        else if (delR) { dirDeletes.push_back(mkOp(r, OpType::DeleteR, e)); skipTo = e.subtreeEnd; }
      } else if (e.status == DStatus::TypeMismatch && mirror) {
        skipTo = e.subtreeEnd;
        if (delR) { ops.push_back(mkOp(r, OpType::DeleteR, e)); ops.push_back(mkOp(r, e.dirL ? OpType::MkdirR : OpType::CopyLR, e)); }
        else if (delL) { ops.push_back(mkOp(r, OpType::DeleteL, e)); ops.push_back(mkOp(r, e.dirR ? OpType::MkdirL : OpType::CopyRL, e)); }
      }
      continue;
    }
    switch (e.status) {
      case DStatus::LeftOnly:
        if (toR) ops.push_back(mkOp(r, OpType::CopyLR, e));
        else if (delL) ops.push_back(mkOp(r, OpType::DeleteL, e));
        break;
      case DStatus::RightOnly:
        if (toL) ops.push_back(mkOp(r, OpType::CopyRL, e));
        else if (delR) ops.push_back(mkOp(r, OpType::DeleteR, e));
        break;
      case DStatus::Different:
      case DStatus::Similar:
        if (mode == SyncMode::MirrorLR) ops.push_back(mkOp(r, OpType::CopyLR, e));
        else if (mode == SyncMode::MirrorRL) ops.push_back(mkOp(r, OpType::CopyRL, e));
        else if (e.status == DStatus::Different) {
          if (e.newer == Newer::Left && toR) ops.push_back(mkOp(r, OpType::CopyLR, e));
          else if (e.newer == Newer::Right && toL) ops.push_back(mkOp(r, OpType::CopyRL, e));
        }
        break;
      case DStatus::TypeMismatch:
        if (delR) { ops.push_back(mkOp(r, OpType::DeleteR, e)); ops.push_back(mkOp(r, OpType::CopyLR, e)); }
        else if (delL) { ops.push_back(mkOp(r, OpType::DeleteL, e)); ops.push_back(mkOp(r, OpType::CopyRL, e)); }
        break;
      default: break;
    }
  }
  for (auto it = dirDeletes.rbegin(); it != dirDeletes.rend(); ++it) ops.push_back(*it);
  return ops;
}

std::vector<Op> planCopySelected(const DirResult& r, bool toRight) {
  std::vector<Op> ops;
  size_t skipTo = 0;
  for (size_t i = 0; i < r.entries.size(); i++) {
    const auto& e = r.entries[i];
    if (i < skipTo || !e.selected) continue;
    bool hasSrc = toRight ? e.hasL : e.hasR, hasDst = toRight ? e.hasR : e.hasL;
    bool srcDir = toRight ? e.dirL : e.dirR;
    if (!hasSrc) continue;
    if (srcDir) {
      if (!hasDst) {  // whole orphan folder: copy recursively
        ops.push_back(mkOp(r, toRight ? OpType::CopyLR : OpType::CopyRL, e));
        skipTo = e.subtreeEnd;
      }
    } else
      ops.push_back(mkOp(r, toRight ? OpType::CopyLR : OpType::CopyRL, e));
  }
  return ops;
}

std::vector<Op> planDeleteSelected(const DirResult& r, bool L, bool R) {
  std::vector<Op> ops, dirs;
  for (auto& e : r.entries) {
    if (!e.selected) continue;
    if (L && e.hasL) (e.dirL ? dirs : ops).push_back(mkOp(r, OpType::DeleteL, e));
    if (R && e.hasR) (e.dirR ? dirs : ops).push_back(mkOp(r, OpType::DeleteR, e));
  }
  for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) ops.push_back(*it);
  return ops;
}

bool copyPathPreserve(const std::string& src, const std::string& dst, std::string* err) {
  std::error_code ec;
  auto s = toPath(src), d = toPath(dst);
  if (fs::is_directory(s, ec)) {
    fs::create_directories(d, ec);
    if (ec) { if (err) *err = ec.message(); return false; }
    std::vector<fs::path> kids;
    for (auto& it : fs::directory_iterator(s, ec)) kids.push_back(it.path());
    for (auto& k : kids)
      if (!copyPathPreserve(fromPath(k), fromPath(d / k.filename()), err)) return false;
    setFileMTime(d, fileMTime(s));
    return true;
  }
  if (d.has_parent_path()) fs::create_directories(d.parent_path(), ec);
  ec.clear();
  if (fs::exists(d, ec)) fs::permissions(d, fs::perms::owner_write, fs::perm_options::add, ec);
  ec.clear();
  fs::copy_file(s, d, fs::copy_options::overwrite_existing, ec);
  if (ec) { if (err) *err = "copy '" + src + "' -> '" + dst + "': " + ec.message(); return false; }
  setFileMTime(d, fileMTime(s));
  return true;
}

int runOps(const std::vector<Op>& ops, bool dryRun, const std::function<void(const std::string&)>& log) {
  int fail = 0;
  for (auto& op : ops) {
    std::string line = std::string(opName(op.type)) + " " + op.rel + (op.isDir ? "/" : "");
    std::string err;
    bool ok = true;
    if (!dryRun) {
      std::error_code ec;
      switch (op.type) {
        case OpType::CopyLR:
        case OpType::CopyRL:
          if (fs::exists(toPath(op.dst), ec) && fs::is_directory(toPath(op.dst), ec) != fs::is_directory(toPath(op.src), ec))
            fs::remove_all(toPath(op.dst), ec);  // replacing a file by a folder or vice versa
          ok = copyPathPreserve(op.src, op.dst, &err);
          break;
        case OpType::DeleteL:
        case OpType::DeleteR:
          fs::permissions(toPath(op.src), fs::perms::owner_write, fs::perm_options::add, ec);
          ec.clear();
          fs::remove_all(toPath(op.src), ec);
          ok = !ec;
          if (!ok) err = ec.message();
          break;
        case OpType::MkdirL:
        case OpType::MkdirR:
          fs::create_directories(toPath(op.dst), ec);
          ok = !ec;
          if (!ok) err = ec.message();
          break;
      }
    }
    if (!ok) { fail++; line += "  FAILED: " + err; }
    else if (dryRun) line += "  (dry run)";
    if (log) log(line);
  }
  return fail;
}

}  // namespace bc

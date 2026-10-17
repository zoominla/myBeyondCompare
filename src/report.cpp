#include "report.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace bc {

bool validTextStyle(const std::string& s0) {
  std::string s = toLower(s0);
  return s == "sidebyside" || s == "side-by-side" || s == "unified" || s == "context" || s == "normal" || s == "html" ||
         s == "summary" || s == "line-by-line";
}

namespace {
const char* rowMarker(const DiffRow& r) {
  if (r.minor && r.kind != RowKind::Same) return r.kind == RowKind::LeftOnly ? "<" : ">";
  switch (r.kind) {
    case RowKind::Same: return " ";
    case RowKind::Unimportant: return "~";
    case RowKind::Different: return "|";
    case RowKind::LeftOnly: return "<";
    case RowKind::RightOnly: return ">";
  }
  return " ";
}
std::string expandTabs(const std::string& s, int ts = 4) {
  std::string o;
  size_t col = 0;
  for (size_t i = 0; i < s.size(); i++) {
    unsigned char c = s[i];
    if (c == '\t') {
      size_t n = ts - (col % ts);
      o.append(n, ' ');
      col += n;
    } else {
      o += (char)c;
      if ((c & 0xC0) != 0x80) col++;
    }
  }
  return o;
}
std::string padTo(const std::string& s, size_t w) {
  size_t n = utf8Len(s);
  return n >= w ? s : s + std::string(w - n, ' ');
}
std::string lpad(const std::string& s, size_t w) { return s.size() >= w ? s : std::string(w - s.size(), ' ') + s; }

// rows to display for onlyDiffs: indices of rows within `ctx` of a non-Same row
std::vector<char> rowMask(const DiffResult& d, bool onlyDiffs, int ctx) {
  std::vector<char> keep(d.rows.size(), onlyDiffs ? 0 : 1);
  if (!onlyDiffs) return keep;
  for (size_t i = 0; i < d.rows.size(); i++) {
    if (d.rows[i].kind == RowKind::Same) continue;
    size_t a = i >= (size_t)ctx ? i - ctx : 0, b = std::min(d.rows.size() - 1, i + (size_t)ctx);
    for (size_t k = a; k <= b; k++) keep[k] = 1;
  }
  return keep;
}

const char* fileEolNote(const TextFile& f) { return f.mixedEol ? "mixed" : eolName(f.dominantEol); }

std::string sideBySide(const TextCompare& tc, const TextReportOptions& o) {
  const auto& L = tc.left.lines;
  const auto& R = tc.right.lines;
  size_t w = 0;
  for (auto& r : tc.diff.rows) {
    if (r.l >= 0) w = std::max(w, utf8Len(expandTabs(L[r.l].text)));
    if (r.r >= 0) w = std::max(w, utf8Len(expandTabs(R[r.r].text)));
  }
  if (o.width > 0) w = o.width;
  else w = std::min<size_t>(std::max<size_t>(w, 10), 100);
  size_t nw = std::to_string(std::max(L.size(), R.size())).size();
  std::ostringstream os;
  os << "Left : " << o.leftTitle << "  [" << encName(tc.left.enc) << ", " << fileEolNote(tc.left) << "]\n";
  os << "Right: " << o.rightTitle << "  [" << encName(tc.right.enc) << ", " << fileEolNote(tc.right) << "]\n";
  auto keep = rowMask(tc.diff, o.onlyDiffs, o.context);
  bool gap = false;
  for (size_t i = 0; i < tc.diff.rows.size(); i++) {
    if (!keep[i]) { gap = true; continue; }
    if (gap) { os << "  ...\n"; gap = false; }
    const auto& r = tc.diff.rows[i];
    std::string ls = r.l >= 0 ? expandTabs(L[r.l].text) : "";
    std::string rs = r.r >= 0 ? expandTabs(R[r.r].text) : "";
    std::string ln = o.lineNumbers ? (r.l >= 0 ? lpad(std::to_string(r.l + 1), nw) : std::string(nw, ' ')) + " " : "";
    std::string rn = o.lineNumbers ? (r.r >= 0 ? lpad(std::to_string(r.r + 1), nw) : std::string(nw, ' ')) + " " : "";
    std::string line = ln + padTo(ls, w) + " " + rowMarker(r) + " " + rn + rs;
    while (!line.empty() && line.back() == ' ') line.pop_back();
    os << line << "\n";
  }
  if (tc.diff.rows.empty()) os << "(both files are empty)\n";
  return os.str();
}

// ---- patch style reports ------------------------------------------------------------
struct Block { size_t a, b; };  // rows [a,b)

std::vector<Block> changeBlocks(const DiffResult& d, bool unimportantAsChange) {
  std::vector<Block> out;
  auto changed = [&](const DiffRow& r) {
    if (r.kind == RowKind::Same) return false;
    if (r.kind == RowKind::Unimportant && !unimportantAsChange) return false;
    return true;
  };
  for (size_t i = 0; i < d.rows.size();) {
    if (!changed(d.rows[i])) { i++; continue; }
    size_t j = i;
    while (j < d.rows.size() && changed(d.rows[j])) j++;
    out.push_back({i, j});
    i = j;
  }
  return out;
}

struct Hunk { size_t a, b; };  // row range including context
std::vector<Hunk> makeHunks(const DiffResult& d, const std::vector<Block>& blocks, int ctx) {
  std::vector<Hunk> hs;
  for (auto& bl : blocks) {
    size_t a = bl.a >= (size_t)ctx ? bl.a - ctx : 0;
    size_t b = std::min(d.rows.size(), bl.b + ctx);
    if (!hs.empty() && a <= hs.back().b) hs.back().b = b;
    else hs.push_back({a, b});
  }
  return hs;
}

struct Counts { int l = 0, r = 0; };
Counts countBefore(const DiffResult& d, size_t row) {
  Counts c;
  for (size_t i = 0; i < row; i++) {
    if (d.rows[i].l >= 0) c.l++;
    if (d.rows[i].r >= 0) c.r++;
  }
  return c;
}

std::string rangeU(int start, int count) {
  if (count == 1) return std::to_string(start);
  return std::to_string(count == 0 ? start - 1 : start) + "," + std::to_string(count);
}

void putLine(std::ostringstream& os, char pfx, const Line& ln, bool isLast) {
  os << pfx << ln.text << "\n";
  if (ln.eol == Eol::None && isLast) os << "\\ No newline at end of file\n";
}

std::string unifiedReport(const TextCompare& tc, const TextReportOptions& o) {
  const auto& d = tc.diff;
  const auto& L = tc.left.lines;
  const auto& R = tc.right.lines;
  auto blocks = changeBlocks(d, o.showUnimportant);
  std::ostringstream os;
  if (blocks.empty()) return "";
  os << "--- " << o.leftTitle << "\n+++ " << o.rightTitle << "\n";
  for (auto& h : makeHunks(d, blocks, o.context)) {
    Counts before = countBefore(d, h.a);
    int lc = 0, rc = 0;
    for (size_t i = h.a; i < h.b; i++) {
      if (d.rows[i].l >= 0) lc++;
      if (d.rows[i].r >= 0) rc++;
    }
    os << "@@ -" << (lc == 0 ? std::to_string(before.l) + ",0" : rangeU(before.l + 1, lc)) << " +"
       << (rc == 0 ? std::to_string(before.r) + ",0" : rangeU(before.r + 1, rc)) << " @@\n";
    size_t i = h.a;
    while (i < h.b) {
      const auto& r = d.rows[i];
      bool isChange = r.kind != RowKind::Same && (r.kind != RowKind::Unimportant || o.showUnimportant);
      if (!isChange) {
        if (r.l >= 0) putLine(os, ' ', L[r.l], (size_t)r.l + 1 == L.size());
        i++;
        continue;
      }
      size_t j = i;
      while (j < h.b && d.rows[j].kind != RowKind::Same && (d.rows[j].kind != RowKind::Unimportant || o.showUnimportant)) j++;
      for (size_t k = i; k < j; k++)
        if (d.rows[k].l >= 0) putLine(os, '-', L[d.rows[k].l], (size_t)d.rows[k].l + 1 == L.size());
      for (size_t k = i; k < j; k++)
        if (d.rows[k].r >= 0) putLine(os, '+', R[d.rows[k].r], (size_t)d.rows[k].r + 1 == R.size());
      i = j;
    }
  }
  return os.str();
}

std::string normalReport(const TextCompare& tc, const TextReportOptions& o) {
  const auto& d = tc.diff;
  const auto& L = tc.left.lines;
  const auto& R = tc.right.lines;
  std::ostringstream os;
  for (auto& bl : changeBlocks(d, o.showUnimportant)) {
    Counts before = countBefore(d, bl.a);
    std::vector<int> ls, rs;
    for (size_t k = bl.a; k < bl.b; k++) {
      if (d.rows[k].l >= 0) ls.push_back(d.rows[k].l);
      if (d.rows[k].r >= 0) rs.push_back(d.rows[k].r);
    }
    auto rng = [](int start, int n) {
      return n == 1 ? std::to_string(start) : std::to_string(start) + "," + std::to_string(start + n - 1);
    };
    char op = ls.empty() ? 'a' : rs.empty() ? 'd' : 'c';
    std::string lr = ls.empty() ? std::to_string(before.l) : rng(before.l + 1, (int)ls.size());
    std::string rr = rs.empty() ? std::to_string(before.r) : rng(before.r + 1, (int)rs.size());
    os << lr << op << rr << "\n";
    for (int x : ls) { os << "< " << L[x].text << "\n"; if (L[x].eol == Eol::None && (size_t)x + 1 == L.size()) os << "\\ No newline at end of file\n"; }
    if (op == 'c') os << "---\n";
    for (int x : rs) { os << "> " << R[x].text << "\n"; if (R[x].eol == Eol::None && (size_t)x + 1 == R.size()) os << "\\ No newline at end of file\n"; }
  }
  return os.str();
}

std::string contextReport(const TextCompare& tc, const TextReportOptions& o) {
  const auto& d = tc.diff;
  const auto& L = tc.left.lines;
  const auto& R = tc.right.lines;
  auto blocks = changeBlocks(d, o.showUnimportant);
  if (blocks.empty()) return "";
  std::ostringstream os;
  os << "*** " << o.leftTitle << "\n--- " << o.rightTitle << "\n";
  auto rng = [](int start, int n) {
    if (n == 0) return std::to_string(start);
    if (n == 1) return std::to_string(start);
    return std::to_string(start) + "," + std::to_string(start + n - 1);
  };
  for (auto& h : makeHunks(d, blocks, o.context)) {
    Counts before = countBefore(d, h.a);
    int lc = 0, rc = 0;
    bool lchg = false, rchg = false;
    // classify changed blocks within the hunk
    struct Info { char lmark = ' ', rmark = ' '; };
    std::vector<Info> info(h.b - h.a);
    for (size_t i = h.a; i < h.b;) {
      const auto& r = d.rows[i];
      bool chg = r.kind != RowKind::Same && (r.kind != RowKind::Unimportant || o.showUnimportant);
      if (!chg) { i++; continue; }
      size_t j = i;
      bool hasL = false, hasR = false;
      while (j < h.b && d.rows[j].kind != RowKind::Same && (d.rows[j].kind != RowKind::Unimportant || o.showUnimportant)) {
        if (d.rows[j].l >= 0) hasL = true;
        if (d.rows[j].r >= 0) hasR = true;
        j++;
      }
      for (size_t k = i; k < j; k++) {
        char lm = hasR ? '!' : '-', rm = hasL ? '!' : '+';
        info[k - h.a].lmark = lm;
        info[k - h.a].rmark = rm;
        if (d.rows[k].l >= 0) lchg = true;
        if (d.rows[k].r >= 0) rchg = true;
      }
      i = j;
    }
    for (size_t i = h.a; i < h.b; i++) { if (d.rows[i].l >= 0) lc++; if (d.rows[i].r >= 0) rc++; }
    os << "***************\n*** " << (lc == 0 ? std::to_string(before.l) : rng(before.l + 1, lc)) << " ****\n";
    if (lchg)
      for (size_t i = h.a; i < h.b; i++) {
        if (d.rows[i].l < 0) continue;
        char m = info[i - h.a].lmark;
        os << m << ' ' << L[d.rows[i].l].text << "\n";
        if (L[d.rows[i].l].eol == Eol::None && (size_t)d.rows[i].l + 1 == L.size()) os << "\\ No newline at end of file\n";
      }
    os << "--- " << (rc == 0 ? std::to_string(before.r) : rng(before.r + 1, rc)) << " ----\n";
    if (rchg)
      for (size_t i = h.a; i < h.b; i++) {
        if (d.rows[i].r < 0) continue;
        char m = info[i - h.a].rmark;
        os << m << ' ' << R[d.rows[i].r].text << "\n";
        if (R[d.rows[i].r].eol == Eol::None && (size_t)d.rows[i].r + 1 == R.size()) os << "\\ No newline at end of file\n";
      }
  }
  return os.str();
}

std::string summaryReport(const TextCompare& tc, const TextReportOptions& o) {
  std::ostringstream os;
  const auto& d = tc.diff;
  os << "Text compare summary\n";
  os << "  Left : " << o.leftTitle << "  (" << tc.left.lines.size() << " lines, " << encName(tc.left.enc) << ", " << fileEolNote(tc.left) << ")\n";
  os << "  Right: " << o.rightTitle << "  (" << tc.right.lines.size() << " lines, " << encName(tc.right.enc) << ", " << fileEolNote(tc.right) << ")\n";
  os << "  Identical bytes   : " << (tc.bytesEqual ? "yes" : "no") << "\n";
  os << "  Same lines        : " << d.nSame << "\n";
  os << "  Unimportant lines : " << d.nUnimportant << "\n";
  os << "  Different lines   : " << d.nDifferent << "\n";
  os << "  Left-only lines   : " << d.nLeftOnly << "\n";
  os << "  Right-only lines  : " << d.nRightOnly << "\n";
  os << "  Difference sections: " << d.sections.size() << " (important: " << d.importantSections() << ")\n";
  os << "  Result: " << (tc.bytesEqual ? "files are identical" : d.importantSame() ? "no important differences (rules-based match)" : "files differ") << "\n";
  return os.str();
}

std::string htmlReport(const TextCompare& tc, const TextReportOptions& o) {
  const auto& L = tc.left.lines;
  const auto& R = tc.right.lines;
  std::ostringstream os;
  os << "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>Text Compare</title>\n<style>\n"
        "body{font-family:Consolas,monospace;font-size:13px}table{border-collapse:collapse;width:100%}\n"
        "td{padding:0 4px;white-space:pre-wrap;vertical-align:top;border:1px solid #ddd}\n"
        "td.n{color:#888;text-align:right;width:3em}.same{}.unim{background:#f0f0f0}.diff{background:#ffe9a8}\n"
        ".lo{background:#ffd0d0}.ro{background:#d0f0d0}.ch{background:#ff9a3c;font-weight:bold}\n"
        ".gap{text-align:center;color:#888;background:#fafafa}\n</style></head><body>\n";
  os << "<h3>Text Compare</h3>\n<p>Left: " << htmlEscape(o.leftTitle) << "<br>Right: " << htmlEscape(o.rightTitle) << "</p>\n";
  os << "<table>\n";
  auto keep = rowMask(tc.diff, o.onlyDiffs, o.context);
  bool gap = false;
  DiffOptions dopt;
  auto spans = [&](const std::string& s, const std::vector<Span>& sp) {
    std::string out;
    size_t pos = 0;
    for (auto& x : sp) {
      if (x.begin < pos || x.end > s.size()) continue;
      out += htmlEscape(s.substr(pos, x.begin - pos));
      out += "<span class=\"ch\">" + htmlEscape(s.substr(x.begin, x.end - x.begin)) + "</span>";
      pos = x.end;
    }
    return out + htmlEscape(s.substr(pos));
  };
  for (size_t i = 0; i < tc.diff.rows.size(); i++) {
    if (!keep[i]) { gap = true; continue; }
    if (gap) { os << "<tr><td class=\"gap\" colspan=\"4\">...</td></tr>\n"; gap = false; }
    const auto& r = tc.diff.rows[i];
    const char* cls = "same";
    switch (r.kind) {
      case RowKind::Same: cls = "same"; break;
      case RowKind::Unimportant: cls = "unim"; break;
      case RowKind::Different: cls = "diff"; break;
      case RowKind::LeftOnly: cls = "lo"; break;
      case RowKind::RightOnly: cls = "ro"; break;
    }
    std::string lt, rt;
    if (r.l >= 0 && r.r >= 0 && r.kind == RowKind::Different) {
      auto id = inlineDiff(L[r.l].text, R[r.r].text, dopt);
      lt = spans(L[r.l].text, id.left);
      rt = spans(R[r.r].text, id.right);
    } else {
      if (r.l >= 0) lt = htmlEscape(L[r.l].text);
      if (r.r >= 0) rt = htmlEscape(R[r.r].text);
    }
    os << "<tr class=\"" << cls << "\"><td class=\"n\">" << (r.l >= 0 ? std::to_string(r.l + 1) : "") << "</td><td>" << lt
       << "</td><td class=\"n\">" << (r.r >= 0 ? std::to_string(r.r + 1) : "") << "</td><td>" << rt << "</td></tr>\n";
  }
  os << "</table>\n</body></html>\n";
  return os.str();
}
}  // namespace

std::string textReport(const TextCompare& tc, const TextReportOptions& o) {
  std::string s = toLower(o.style);
  if (s == "unified") return unifiedReport(tc, o);
  if (s == "context") return contextReport(tc, o);
  if (s == "normal") return normalReport(tc, o);
  if (s == "html") return htmlReport(tc, o);
  if (s == "summary") return summaryReport(tc, o);
  return sideBySide(tc, o);
}

// ---------------------------------------------------------------------------------
// Folder reports
// ---------------------------------------------------------------------------------
bool validFolderStyle(const std::string& s0) {
  std::string s = toLower(s0);
  return s == "tree" || s == "list" || s == "csv" || s == "html" || s == "xml" || s == "summary";
}

static const char* statusSymbol(const DirEntry& e) {
  switch (e.status) {
    case DStatus::Same: return "==";
    case DStatus::Similar: return "~=";
    case DStatus::Different: return e.newer == Newer::Left ? "<!" : e.newer == Newer::Right ? "!>" : "!=";
    case DStatus::LeftOnly: return "<-";
    case DStatus::RightOnly: return "->";
    case DStatus::TypeMismatch: return "!?";
    case DStatus::Error: return "ER";
  }
  return "??";
}

std::string folderSummaryText(const DirResult& r) {
  const auto& s = r.summary;
  std::ostringstream os;
  os << "Left : " << r.left << "\nRight: " << r.right << "\n";
  os << "Files: " << s.files << "  same: " << s.same << "  different: " << s.different << " (newer left: " << s.newerLeft
     << ", newer right: " << s.newerRight << ")  similar: " << s.similar << "  left-only: " << s.leftOnly
     << "  right-only: " << s.rightOnly;
  if (s.mismatch) os << "  type-mismatch: " << s.mismatch;
  if (s.errors) os << "  errors: " << s.errors;
  os << "\nFolders: " << s.folders << "  same: " << s.foldersSame << "  different: " << s.foldersDifferent
     << "  left-only: " << s.foldersLeftOnly << "  right-only: " << s.foldersRightOnly << "\n";
  return os.str();
}

std::string folderReport(const DirResult& r, const FolderReportOptions& o) {
  std::string style = toLower(o.style);
  auto vis = visibleEntries(r, o.show);
  std::ostringstream os;
  auto sizeStr = [](const DirEntry& e, bool left) -> std::string {
    if (left ? !e.hasL : !e.hasR) return "";
    if (left ? e.dirL : e.dirR) return "<DIR>";
    return std::to_string(left ? e.sizeL : e.sizeR);
  };
  auto timeStr = [](const DirEntry& e, bool left) -> std::string {
    if (left ? !e.hasL : !e.hasR) return "";
    return formatTime(left ? e.mtimeL : e.mtimeR);
  };
  if (style == "summary") return folderSummaryText(r);
  if (style == "list") {
    for (size_t i : vis) {
      const auto& e = r.entries[i];
      if (o.fullPaths) os << (e.hasL ? joinPath(r.left, e.relL) : joinPath(r.right, e.relR));
      else os << e.rel;
      os << (e.isDir() ? "/" : "") << "\n";
    }
    if (o.summary) os << "\n" << folderSummaryText(r);
    return os.str();
  }
  if (style == "csv") {
    os << "Status,Type,Path,LeftSize,LeftTime,RightSize,RightTime,Newer,Reason\n";
    for (size_t i : vis) {
      const auto& e = r.entries[i];
      os << statusName(e.status) << "," << (e.isDir() ? "folder" : "file") << "," << csvEscape(e.rel) << ","
         << (e.dirL ? "" : sizeStr(e, true)) << "," << timeStr(e, true) << "," << (e.dirR ? "" : sizeStr(e, false)) << ","
         << timeStr(e, false) << "," << (e.newer == Newer::Left ? "left" : e.newer == Newer::Right ? "right" : "") << ","
         << csvEscape(e.reason) << "\n";
    }
    return os.str();
  }
  if (style == "xml") {
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<folderreport left=\"" << xmlEscape(r.left) << "\" right=\"" << xmlEscape(r.right) << "\">\n";
    for (size_t i : vis) {
      const auto& e = r.entries[i];
      os << "\t<entry type=\"" << (e.isDir() ? "folder" : "file") << "\" status=\"" << statusName(e.status) << "\" path=\""
         << xmlEscape(e.rel) << "\"";
      if (e.hasL && !e.dirL) os << " leftsize=\"" << e.sizeL << "\"";
      if (e.hasR && !e.dirR) os << " rightsize=\"" << e.sizeR << "\"";
      if (e.hasL) os << " lefttime=\"" << formatTime(e.mtimeL) << "\"";
      if (e.hasR) os << " righttime=\"" << formatTime(e.mtimeR) << "\"";
      if (e.newer != Newer::None) os << " newer=\"" << (e.newer == Newer::Left ? "left" : "right") << "\"";
      if (!e.reason.empty()) os << " reason=\"" << xmlEscape(e.reason) << "\"";
      os << "/>\n";
    }
    const auto& s = r.summary;
    os << "\t<summary files=\"" << s.files << "\" same=\"" << s.same << "\" different=\"" << s.different << "\" similar=\""
       << s.similar << "\" leftonly=\"" << s.leftOnly << "\" rightonly=\"" << s.rightOnly << "\"/>\n</folderreport>\n";
    return os.str();
  }
  if (style == "html") {
    os << "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>Folder Compare</title><style>\n"
          "body{font-family:Segoe UI,Arial,sans-serif;font-size:13px}table{border-collapse:collapse}\n"
          "td,th{border:1px solid #ccc;padding:2px 8px}.same{color:#555}.different{background:#ffe9a8}.similar{background:#f0f0f0}\n"
          ".left-only{background:#ffd0d0}.right-only{background:#d0f0d0}.type-mismatch,.error{background:#f8b0ff}\n</style></head><body>\n";
    os << "<h3>Folder Compare</h3><p>Left: " << htmlEscape(r.left) << "<br>Right: " << htmlEscape(r.right) << "</p>\n<table>\n";
    os << "<tr><th>Name</th><th>Status</th><th>Left size</th><th>Left time</th><th>Right size</th><th>Right time</th></tr>\n";
    for (size_t i : vis) {
      const auto& e = r.entries[i];
      os << "<tr class=\"" << statusName(e.status) << "\"><td>" << std::string(e.depth * 4, ' ') << htmlEscape(e.name)
         << (e.isDir() ? "/" : "") << "</td><td>" << statusName(e.status)
         << (e.newer == Newer::Left ? " (left newer)" : e.newer == Newer::Right ? " (right newer)" : "") << "</td><td>"
         << sizeStr(e, true) << "</td><td>" << timeStr(e, true) << "</td><td>" << sizeStr(e, false) << "</td><td>"
         << timeStr(e, false) << "</td></tr>\n";
    }
    os << "</table>\n";
    if (o.summary) {
      std::string t = folderSummaryText(r);
      os << "<pre>" << htmlEscape(t) << "</pre>\n";
    }
    os << "</body></html>\n";
    return os.str();
  }
  // tree
  os << "Left : " << r.left << "\nRight: " << r.right << "\n";
  os << "Legend: == same, ~= similar, != different, <! left newer, !> right newer, <- left only, -> right only, !? type mismatch\n";
  os << "St  " << padTo("Left size", 12) << padTo("Left modified", 20) << padTo("Right size", 12) << padTo("Right modified", 20) << "Name\n";
  for (size_t i : vis) {
    const auto& e = r.entries[i];
    os << padTo(statusSymbol(e), 4) << padTo(sizeStr(e, true), 12) << padTo(timeStr(e, true), 20) << padTo(sizeStr(e, false), 12)
       << padTo(timeStr(e, false), 20) << std::string(e.depth * 2, ' ') << e.name << (e.isDir() ? "/" : "");
    if (!e.error.empty()) os << "  [" << e.error << "]";
    os << "\n";
  }
  if (o.summary) os << "\n" << folderSummaryText(r);
  return os.str();
}

}  // namespace bc

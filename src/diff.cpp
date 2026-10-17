#include "diff.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <numeric>
#include <unordered_map>

#include "common.h"

namespace bc {

bool parseAlign(const std::string& s, Align& out) {
  std::string n = toLower(s);
  if (n == "smart") out = Align::Smart;
  else if (n == "complete") out = Align::Complete;
  else if (n == "fast" || n == "quick") out = Align::Fast;
  else if (n == "patience") out = Align::Patience;
  else if (n == "sort") out = Align::Sort;
  else if (n == "none") out = Align::None;
  else return false;
  return true;
}

// ---------------------------------------------------------------------------------
// Normalizer
// ---------------------------------------------------------------------------------
Normalizer::Normalizer(const DiffOptions& o) : o_(o) {
  for (auto& r : o.ignoreRegexes) {
    try {
      re_.emplace_back(r, std::regex::ECMAScript);
    } catch (...) {
    }
  }
  plain_ = !(o.ignoreCase || o.ignoreWsAll || o.ignoreWsLeading || o.ignoreWsTrailing || o.ignoreWsAmount || !re_.empty());
}
static bool isWs(char c) { return c == ' ' || c == '\t' || c == '\v' || c == '\f'; }

std::string Normalizer::keyText(const std::string& in) const {
  if (plain_) return in;
  std::string s = in;
  for (auto& re : re_) s = std::regex_replace(s, re, "");
  if (o_.ignoreCase)
    for (auto& c : s)
      if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  if (o_.ignoreWsAll) {
    std::string t;
    for (char c : s)
      if (!isWs(c)) t += c;
    return t;
  }
  size_t a = 0, b = s.size();
  if (o_.ignoreWsLeading || o_.ignoreWsAmount)
    while (a < b && isWs(s[a])) a++;
  if (o_.ignoreWsTrailing || o_.ignoreWsAmount)
    while (b > a && isWs(s[b - 1])) b--;
  s = s.substr(a, b - a);
  if (o_.ignoreWsAmount) {
    std::string t;
    bool inWs = false;
    for (char c : s) {
      if (isWs(c)) { if (!inWs) t += ' '; inWs = true; }
      else { t += c; inWs = false; }
    }
    return t;
  }
  return s;
}
std::string Normalizer::key(const Line& l) const {
  std::string k = keyText(l.text);
  if (!o_.ignoreEol) {
    k += '\x01';
    k += eolStr(l.eol);
    k += (char)('0' + (int)l.eol);
  }
  return k;
}

// ---------------------------------------------------------------------------------
// Myers LCS (divide & conquer, linear space)
// ---------------------------------------------------------------------------------
namespace {
struct Myers {
  const int* A;
  const int* B;
  long maxCost;
  std::vector<std::pair<int, int>>* out;
  std::vector<int> v1, v2;

  // returns true and the split point if found
  bool bisect(const int* a, int n, const int* b, int m, int& sx, int& sy) {
    int maxD = (n + m + 1) / 2;
    int off = maxD, len = 2 * maxD + 1;
    v1.assign(len, -1);
    v2.assign(len, -1);
    v1[off + 1] = 0;
    v2[off + 1] = 0;
    int delta = n - m;
    bool front = (delta % 2) != 0;
    int k1s = 0, k1e = 0, k2s = 0, k2e = 0;
    for (int d = 0; d < maxD; d++) {
      if (maxCost > 0 && d > maxCost) return false;
      for (int k1 = -d + k1s; k1 <= d - k1e; k1 += 2) {
        int ko = off + k1;
        int x1;
        if (k1 == -d || (k1 != d && v1[ko - 1] < v1[ko + 1])) x1 = v1[ko + 1];
        else x1 = v1[ko - 1] + 1;
        int y1 = x1 - k1;
        while (x1 < n && y1 < m && a[x1] == b[y1]) { x1++; y1++; }
        v1[ko] = x1;
        if (x1 > n) k1e += 2;
        else if (y1 > m) k1s += 2;
        else if (front) {
          int k2o = off + delta - k1;
          if (k2o >= 0 && k2o < len && v2[k2o] != -1) {
            int x2 = n - v2[k2o];
            if (x1 >= x2) { sx = x1; sy = y1; return true; }
          }
        }
      }
      for (int k2 = -d + k2s; k2 <= d - k2e; k2 += 2) {
        int ko = off + k2;
        int x2;
        if (k2 == -d || (k2 != d && v2[ko - 1] < v2[ko + 1])) x2 = v2[ko + 1];
        else x2 = v2[ko - 1] + 1;
        int y2 = x2 - k2;
        while (x2 < n && y2 < m && a[n - x2 - 1] == b[m - y2 - 1]) { x2++; y2++; }
        v2[ko] = x2;
        if (x2 > n) k2e += 2;
        else if (y2 > m) k2s += 2;
        else if (!front) {
          int k1o = off + delta - k2;
          if (k1o >= 0 && k1o < len && v1[k1o] != -1) {
            int x1 = v1[k1o];
            int y1 = off + x1 - k1o;
            if (x1 >= n - x2) { sx = x1; sy = y1; return true; }
          }
        }
      }
    }
    return false;
  }

  void rec(const int* a, int n, const int* b, int m, int ao, int bo) {
    int p = 0;
    while (p < n && p < m && a[p] == b[p]) { out->push_back({ao + p, bo + p}); p++; }
    a += p; b += p; n -= p; m -= p; ao += p; bo += p;
    int s = 0;
    while (s < n && s < m && a[n - 1 - s] == b[m - 1 - s]) s++;
    int n2 = n - s, m2 = m - s;
    if (n2 > 0 && m2 > 0) {
      int x, y;
      if (bisect(a, n2, b, m2, x, y) && !((x == 0 && y == 0) || (x == n2 && y == m2))) {
        rec(a, x, b, y, ao, bo);
        rec(a + x, n2 - x, b + y, m2 - y, ao + x, bo + y);
      }
    }
    for (int k = 0; k < s; k++) out->push_back({ao + n2 + k, bo + m2 + k});
  }
};
}  // namespace

std::vector<std::pair<int, int>> lcsMatches(const std::vector<int>& a, const std::vector<int>& b, long maxCost) {
  std::vector<std::pair<int, int>> res;
  if (a.empty() || b.empty()) return res;
  // drop elements that occur on only one side: they cannot be part of an LCS
  std::unordered_map<int, std::pair<int, int>> cnt;
  for (int x : a) cnt[x].first++;
  for (int x : b) cnt[x].second++;
  std::vector<int> ra, rb, ia, ib;
  ra.reserve(a.size());
  rb.reserve(b.size());
  for (size_t i = 0; i < a.size(); i++)
    if (cnt[a[i]].second) { ra.push_back(a[i]); ia.push_back((int)i); }
  for (size_t i = 0; i < b.size(); i++)
    if (cnt[b[i]].first) { rb.push_back(b[i]); ib.push_back((int)i); }
  std::vector<std::pair<int, int>> m;
  Myers my{ra.data(), rb.data(), maxCost, &m, {}, {}};
  my.rec(ra.data(), (int)ra.size(), rb.data(), (int)rb.size(), 0, 0);
  res.reserve(m.size());
  for (auto& p : m) res.push_back({ia[p.first], ib[p.second]});
  return res;
}

// ---------------------------------------------------------------------------------
// Patience
// ---------------------------------------------------------------------------------
static void patienceRec(const std::vector<int>& a, const std::vector<int>& b, int alo, int ahi, int blo, int bhi,
                        std::vector<std::pair<int, int>>& out) {
  while (alo < ahi && blo < bhi && a[alo] == b[blo]) { out.push_back({alo, blo}); alo++; blo++; }
  std::vector<std::pair<int, int>> tail;
  while (alo < ahi && blo < bhi && a[ahi - 1] == b[bhi - 1]) { ahi--; bhi--; tail.push_back({ahi, bhi}); }
  auto flushTail = [&] { for (auto it = tail.rbegin(); it != tail.rend(); ++it) out.push_back(*it); };
  if (alo >= ahi || blo >= bhi) { flushTail(); return; }
  std::unordered_map<int, std::pair<int, int>> pos;  // id -> (count, index) in a ; count in b stored via second map
  std::unordered_map<int, int> ca, cb, ib;
  std::unordered_map<int, int> ia;
  for (int i = alo; i < ahi; i++) { ca[a[i]]++; ia[a[i]] = i; }
  for (int j = blo; j < bhi; j++) { cb[b[j]]++; ib[b[j]] = j; }
  std::vector<std::pair<int, int>> cand;  // (i,j) unique in both
  for (int i = alo; i < ahi; i++) {
    int id = a[i];
    if (ca[id] == 1 && cb.count(id) && cb[id] == 1) cand.push_back({i, ib[id]});
  }
  if (cand.empty()) {
    std::vector<int> sa(a.begin() + alo, a.begin() + ahi), sb(b.begin() + blo, b.begin() + bhi);
    for (auto& p : lcsMatches(sa, sb)) out.push_back({p.first + alo, p.second + blo});
    flushTail();
    return;
  }
  // LIS on j
  std::vector<int> tails, prev(cand.size(), -1), tailIdx;
  for (size_t k = 0; k < cand.size(); k++) {
    int j = cand[k].second;
    size_t pos2 = std::lower_bound(tails.begin(), tails.end(), j) - tails.begin();
    if (pos2 == tails.size()) { tails.push_back(j); tailIdx.push_back((int)k); }
    else { tails[pos2] = j; tailIdx[pos2] = (int)k; }
    prev[k] = pos2 ? tailIdx[pos2 - 1] : -1;
  }
  std::vector<int> chain;
  for (int k = tailIdx.back(); k >= 0; k = prev[k]) chain.push_back(k);
  std::reverse(chain.begin(), chain.end());
  int pa = alo, pb = blo;
  for (int k : chain) {
    patienceRec(a, b, pa, cand[k].first, pb, cand[k].second, out);
    out.push_back(cand[k]);
    pa = cand[k].first + 1;
    pb = cand[k].second + 1;
  }
  patienceRec(a, b, pa, ahi, pb, bhi, out);
  flushTail();
}
std::vector<std::pair<int, int>> patienceMatches(const std::vector<int>& a, const std::vector<int>& b) {
  std::vector<std::pair<int, int>> out;
  patienceRec(a, b, 0, (int)a.size(), 0, (int)b.size(), out);
  return out;
}

// ---------------------------------------------------------------------------------
// Line diff with row building
// ---------------------------------------------------------------------------------
size_t DiffResult::importantSections() const {
  size_t n = 0;
  for (auto& s : sections) n += s.important;
  return n;
}

namespace {
struct Builder {
  const std::vector<Line>& L;
  const std::vector<Line>& R;
  const DiffOptions& opt;
  std::vector<std::string> kl, kr;  // keys per original line
  std::vector<int> mapL, mapR;      // position -> original line index
  DiffResult res;

  Builder(const std::vector<Line>& l, const std::vector<Line>& r, const DiffOptions& o) : L(l), R(r), opt(o) {}

  RowKind pairKind(int li, int ri) const {
    if (L[li].text == R[ri].text && L[li].eol == R[ri].eol) return RowKind::Same;
    if (kl[li] == kr[ri]) return RowKind::Unimportant;
    return RowKind::Different;
  }
  void pushRow(int li, int ri, RowKind k, bool minor = false) {
    DiffRow row;
    row.l = li; row.r = ri; row.kind = k; row.minor = minor;
    res.rows.push_back(row);
  }
  void pair(int pa, int pb) { pushRow(mapL[pa], mapR[pb], pairKind(mapL[pa], mapR[pb])); }

  static double simil(const std::array<uint16_t, 256>& a, size_t la, const std::array<uint16_t, 256>& b, size_t lb) {
    if (la + lb == 0) return 1.0;
    size_t c = 0;
    for (int i = 0; i < 256; i++) c += std::min(a[i], b[i]);
    return 2.0 * (double)c / (double)(la + lb);
  }

  void positional(const std::vector<int>& A, const std::vector<int>& B) {
    size_t k = 0;
    for (; k < A.size() && k < B.size(); k++) pair(A[k], B[k]);
    for (size_t i = k; i < A.size(); i++) pushRow(mapL[A[i]], -1, RowKind::LeftOnly);
    for (size_t j = k; j < B.size(); j++) pushRow(-1, mapR[B[j]], RowKind::RightOnly);
  }

  void gap(int la, int lb, int ra, int rb) {
    std::vector<int> A, B;
    for (int i = la; i < lb; i++) {
      if (opt.ignoreBlankLines && kl[mapL[i]].empty()) pushRow(mapL[i], -1, RowKind::LeftOnly, true);
      else A.push_back(i);
    }
    for (int j = ra; j < rb; j++) {
      if (opt.ignoreBlankLines && kr[mapR[j]].empty()) pushRow(-1, mapR[j], RowKind::RightOnly, true);
      else B.push_back(j);
    }
    if (A.empty() || B.empty() || opt.align != Align::Smart) { positional(A, B); return; }
    size_t na = A.size(), nb = B.size();
    if (na * nb > 250000 || na > 600 || nb > 600) { positional(A, B); return; }
    std::vector<std::array<uint16_t, 256>> ha(na), hb(nb);
    for (size_t i = 0; i < na; i++) {
      ha[i].fill(0);
      for (unsigned char c : kl[mapL[A[i]]]) if (ha[i][c] < 65535) ha[i][c]++;
    }
    for (size_t j = 0; j < nb; j++) {
      hb[j].fill(0);
      for (unsigned char c : kr[mapR[B[j]]]) if (hb[j][c] < 65535) hb[j][c]++;
    }
    const double thr = 0.45;
    std::vector<double> dp((na + 1) * (nb + 1), 0.0);
    std::vector<double> sm(na * nb, 0.0);
    auto at = [&](size_t i, size_t j) -> double& { return dp[i * (nb + 1) + j]; };
    for (size_t i = 1; i <= na; i++)
      for (size_t j = 1; j <= nb; j++) {
        double s = simil(ha[i - 1], kl[mapL[A[i - 1]]].size(), hb[j - 1], kr[mapR[B[j - 1]]].size());
        sm[(i - 1) * nb + (j - 1)] = s;
        double best = std::max(at(i - 1, j), at(i, j - 1));
        if (s >= thr) best = std::max(best, at(i - 1, j - 1) + s);
        at(i, j) = best;
      }
    std::vector<std::pair<int, int>> anchors;
    for (size_t i = na, j = nb; i > 0 && j > 0;) {
      double s = sm[(i - 1) * nb + (j - 1)];
      if (s >= thr && at(i, j) == at(i - 1, j - 1) + s) { anchors.push_back({(int)i - 1, (int)j - 1}); i--; j--; }
      else if (at(i, j) == at(i - 1, j)) i--;
      else j--;
    }
    std::reverse(anchors.begin(), anchors.end());
    size_t pi = 0, pj = 0;
    for (auto& an : anchors) {
      std::vector<int> sa(A.begin() + pi, A.begin() + an.first), sb(B.begin() + pj, B.begin() + an.second);
      positional(sa, sb);
      pair(A[an.first], B[an.second]);
      pi = an.first + 1;
      pj = an.second + 1;
    }
    std::vector<int> sa(A.begin() + pi, A.end()), sb(B.begin() + pj, B.end());
    positional(sa, sb);
  }

  void finish() {
    auto& r = res;
    for (auto& row : r.rows) {
      if (row.minor) { r.nUnimportant++; continue; }
      switch (row.kind) {
        case RowKind::Same: r.nSame++; break;
        case RowKind::Unimportant: r.nUnimportant++; break;
        case RowKind::Different: r.nDifferent++; break;
        case RowKind::LeftOnly: r.nLeftOnly++; break;
        case RowKind::RightOnly: r.nRightOnly++; break;
      }
    }
    size_t i = 0;
    auto isDiff = [&](const DiffRow& w) {
      if (w.kind == RowKind::Same) return false;
      if (opt.ignoreUnimportant && (w.kind == RowKind::Unimportant || w.minor)) return false;
      return true;
    };
    while (i < r.rows.size()) {
      if (!isDiff(r.rows[i])) { i++; continue; }
      size_t j = i;
      bool imp = false;
      while (j < r.rows.size() && isDiff(r.rows[j])) { imp |= rowImportant(r.rows[j]); j++; }
      r.sections.push_back({i, j, imp});
      i = j;
    }
  }
};
}  // namespace

DiffResult diffLines(const std::vector<Line>& L, const std::vector<Line>& R, const DiffOptions& opt) {
  Builder b(L, R, opt);
  Normalizer nz(opt);
  b.kl.reserve(L.size());
  b.kr.reserve(R.size());
  std::unordered_map<std::string, int> ids;
  std::vector<int> idl(L.size()), idr(R.size());
  auto idOf = [&](const std::string& k) {
    auto it = ids.find(k);
    if (it != ids.end()) return it->second;
    int v = (int)ids.size();
    ids.emplace(k, v);
    return v;
  };
  for (size_t i = 0; i < L.size(); i++) { b.kl.push_back(nz.key(L[i])); idl[i] = idOf(b.kl.back()); }
  for (size_t i = 0; i < R.size(); i++) { b.kr.push_back(nz.key(R[i])); idr[i] = idOf(b.kr.back()); }

  b.mapL.resize(L.size());
  b.mapR.resize(R.size());
  std::iota(b.mapL.begin(), b.mapL.end(), 0);
  std::iota(b.mapR.begin(), b.mapR.end(), 0);
  if (opt.align == Align::Sort) {
    std::stable_sort(b.mapL.begin(), b.mapL.end(), [&](int x, int y) { return b.kl[x] < b.kl[y]; });
    std::stable_sort(b.mapR.begin(), b.mapR.end(), [&](int x, int y) { return b.kr[x] < b.kr[y]; });
  }
  int nl = (int)L.size(), nr = (int)R.size();

  if (opt.align == Align::None) {
    for (int k = 0; k < std::max(nl, nr); k++) {
      if (k < nl && k < nr) b.pair(k, k);
      else if (k < nl) b.pushRow(k, -1, RowKind::LeftOnly);
      else b.pushRow(-1, k, RowKind::RightOnly);
    }
    b.finish();
    return std::move(b.res);
  }

  // sequences of ids at positions (blank lines are excluded from matching when ignoring them)
  std::vector<int> sa, sb, pa, pb;
  for (int i = 0; i < nl; i++) {
    if (opt.ignoreBlankLines && b.kl[b.mapL[i]].empty()) continue;
    sa.push_back(idl[b.mapL[i]]); pa.push_back(i);
  }
  for (int j = 0; j < nr; j++) {
    if (opt.ignoreBlankLines && b.kr[b.mapR[j]].empty()) continue;
    sb.push_back(idr[b.mapR[j]]); pb.push_back(j);
  }
  std::vector<std::pair<int, int>> m;
  if (opt.align == Align::Patience) m = patienceMatches(sa, sb);
  else if (opt.align == Align::Fast) m = lcsMatches(sa, sb, 1500);
  else m = lcsMatches(sa, sb, 0);
  int ci = 0, cj = 0;
  for (auto& mm : m) {
    int i = pa[mm.first], j = pb[mm.second];
    b.gap(ci, i, cj, j);
    b.pair(i, j);
    ci = i + 1;
    cj = j + 1;
  }
  b.gap(ci, nl, cj, nr);
  b.finish();
  return std::move(b.res);
}

// ---------------------------------------------------------------------------------
// Inline (character level) diff
// ---------------------------------------------------------------------------------
InlineDiff inlineDiff(const std::string& a, const std::string& b, const DiffOptions& opt) {
  InlineDiff res;
  struct Tok { std::string s; size_t off, len; };
  auto toks = [&](const std::string& s) {
    std::vector<Tok> t;
    size_t off = 0;
    for (auto& ch : utf8Chars(s)) {
      bool skip = (opt.ignoreWsAll || opt.ignoreWsAmount) && ch.size() == 1 && isWs(ch[0]);
      if (!skip) {
        std::string k = ch;
        if (opt.ignoreCase && k.size() == 1 && k[0] >= 'A' && k[0] <= 'Z') k[0] = char(k[0] - 'A' + 'a');
        t.push_back({k, off, ch.size()});
      }
      off += ch.size();
    }
    return t;
  };
  auto ta = toks(a), tb = toks(b);
  std::unordered_map<std::string, int> ids;
  auto idv = [&](const std::vector<Tok>& t) {
    std::vector<int> v;
    for (auto& x : t) {
      auto it = ids.find(x.s);
      if (it == ids.end()) it = ids.emplace(x.s, (int)ids.size()).first;
      v.push_back(it->second);
    }
    return v;
  };
  auto ia = idv(ta), ib = idv(tb);
  auto m = lcsMatches(ia, ib, 3000);
  std::vector<char> ma(ta.size(), 0), mb(tb.size(), 0);
  for (auto& p : m) { ma[p.first] = 1; mb[p.second] = 1; }
  auto spans = [&](const std::vector<Tok>& t, const std::vector<char>& mk, std::vector<Span>& out) {
    for (size_t i = 0; i < t.size(); i++) {
      if (mk[i]) continue;
      size_t j = i;
      while (j + 1 < t.size() && !mk[j + 1]) j++;
      out.push_back({t[i].off, t[j].off + t[j].len});
      i = j;
    }
  };
  spans(ta, ma, res.left);
  spans(tb, mb, res.right);
  return res;
}

bool compareTextFiles(const std::string& lpath, const std::string& rpath, const DiffOptions& opt, TextCompare& out,
                      std::string* err, Enc lenc, Enc renc) {
  std::string a, b;
  if (!readFile(lpath, a, err) || !readFile(rpath, b, err)) return false;
  out.bytesEqual = (a == b);
  out.left = parseText(a, lenc);
  out.right = parseText(b, renc);
  out.diff = diffLines(out.left.lines, out.right.lines, opt);
  return true;
}

}  // namespace bc

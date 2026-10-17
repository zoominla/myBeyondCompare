#include "merge.h"

#include <unordered_map>

namespace bc {

MergeResult merge3(const TextFile& base, const TextFile& left, const TextFile& right, const MergeOptions& opt) {
  MergeResult res;
  Normalizer nz(opt.diff);
  std::unordered_map<std::string, int> ids;
  auto idv = [&](const TextFile& tf, std::vector<int>& v) {
    for (auto& l : tf.lines) {
      std::string k = nz.keyText(l.text);
      auto it = ids.find(k);
      if (it == ids.end()) it = ids.emplace(k, (int)ids.size()).first;
      v.push_back(it->second);
    }
  };
  std::vector<int> ib, il, ir;
  idv(base, ib);
  idv(left, il);
  idv(right, ir);
  int nb = (int)ib.size(), nl = (int)il.size(), nr = (int)ir.size();
  std::vector<int> mL(nb, -1), mR(nb, -1);
  for (auto& p : lcsMatches(ib, il)) mL[p.first] = p.second;
  for (auto& p : lcsMatches(ib, ir)) mR[p.first] = p.second;

  std::vector<Line> out;
  auto emit = [&](const TextFile& tf, int a, int b) {
    for (int i = a; i < b; i++) out.push_back(tf.lines[i]);
  };
  auto segEq = [&](const std::vector<int>& x, int xa, int xb, const std::vector<int>& y, int ya, int yb) {
    if (xb - xa != yb - ya) return false;
    for (int k = 0; k < xb - xa; k++)
      if (x[xa + k] != y[ya + k]) return false;
    return true;
  };

  int bp = 0, lp = 0, rp = 0;  // next unprocessed index in each
  auto rec = [&](MergeRegion::Kind k, int bc, int lc, int rc) {
    if (bc == bp && lc == lp && rc == rp) return;
    MergeRegion g;
    g.kind = k;
    g.base.assign(base.lines.begin() + bp, base.lines.begin() + bc);
    g.left.assign(left.lines.begin() + lp, left.lines.begin() + lc);
    g.right.assign(right.lines.begin() + rp, right.lines.begin() + rc);
    res.regions.push_back(std::move(g));
  };
  auto region0 = [&](int bc, int lc, int rc) {
    bool eqL = segEq(ib, bp, bc, il, lp, lc);
    bool eqR = segEq(ib, bp, bc, ir, rp, rc);
    if (eqL && eqR) {
      rec(MergeRegion::Same, bc, lc, rc);
      emit(right, rp, rc);
    } else if (eqL) {
      rec(MergeRegion::RightChanged, bc, lc, rc);
      emit(right, rp, rc);
      res.autoMerged++;
    } else if (eqR) {
      rec(MergeRegion::LeftChanged, bc, lc, rc);
      emit(left, lp, lc);
      res.autoMerged++;
    } else if (segEq(il, lp, lc, ir, rp, rc)) {
      rec(MergeRegion::BothSame, bc, lc, rc);
      emit(left, lp, lc);
      res.autoMerged++;
    } else {
      rec(MergeRegion::Conflict, bc, lc, rc);
      switch (opt.resolve) {
        case Resolve::Left: emit(left, lp, lc); res.resolved++; return;
        case Resolve::Right: emit(right, rp, rc); res.resolved++; return;
        case Resolve::Both:
          emit(left, lp, lc);
          emit(right, rp, rc);
          res.resolved++;
          return;
        case Resolve::None: break;
      }
      res.conflicts++;
      if (opt.markConflicts) {
        Eol e = left.dominantEol;
        out.push_back({"<<<<<<< " + opt.leftLabel, e});
        emit(left, lp, lc);
        out.push_back({"||||||| " + opt.baseLabel, e});
        for (int i = bp; i < bc; i++) out.push_back(base.lines[i]);
        out.push_back({"=======", e});
        emit(right, rp, rc);
        out.push_back({">>>>>>> " + opt.rightLabel, e});
      } else {
        emit(left, lp, lc);  // keep left so the output is still a usable file
      }
    }
  };

  auto region = [&](int bc, int lc, int rc) {
    size_t o0 = out.size(), n0 = res.regions.size();
    region0(bc, lc, rc);
    if (res.regions.size() > n0) res.regions.back().out.assign(out.begin() + o0, out.end());
  };

  for (int i = 0; i < nb; i++) {
    if (mL[i] < 0 || mR[i] < 0) continue;
    region(i, mL[i], mR[i]);
    const Line& L = left.lines[mL[i]];
    const Line& R = right.lines[mR[i]];
    const Line& B = base.lines[i];
    out.push_back(L.text == B.text ? R : L);  // the common (sync) line
    if (res.regions.empty() || res.regions.back().kind != MergeRegion::Same) res.regions.emplace_back();
    res.regions.back().base.push_back(B);
    res.regions.back().left.push_back(L);
    res.regions.back().right.push_back(R);
    res.regions.back().out.push_back(out.back());
    bp = i + 1;
    lp = mL[i] + 1;
    rp = mR[i] + 1;
  }
  region(nb, nl, nr);

  // only the very last line may lack a line ending
  Eol dom = left.dominantEol;
  for (size_t i = 0; i + 1 < out.size(); i++)
    if (out[i].eol == Eol::None) out[i].eol = dom;
  res.output.enc = left.enc;
  res.output.dominantEol = dom;
  res.output.lines = std::move(out);
  return res;
}

}  // namespace bc

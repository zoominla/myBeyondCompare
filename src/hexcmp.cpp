#include "hexcmp.h"

#include <algorithm>
#include <cstdio>

namespace bc {

HexResult hexCompare(const std::string& a, const std::string& b) {
  HexResult r;
  r.sizeL = a.size();
  r.sizeR = b.size();
  size_t n = std::min(a.size(), b.size());
  size_t i = 0;
  while (i < n) {
    if (a[i] == b[i]) { i++; continue; }
    size_t j = i;
    while (j < n && a[j] != b[j]) j++;
    r.ranges.push_back({i, j - i});
    r.diffBytes += j - i;
    i = j;
  }
  if (a.size() != b.size()) r.ranges.push_back({n, std::max(a.size(), b.size()) - n});
  r.equal = r.ranges.empty();
  return r;
}

std::string hexDump(const std::string& a, const std::string& b, const HexDumpOptions& o) {
  const int W = o.bytesPerRow;
  size_t maxLen = std::max(a.size(), b.size());
  size_t rows = (maxLen + W - 1) / W;
  std::vector<char> rowDiff(rows, 0);
  for (size_t r = 0; r < rows; r++)
    for (int k = 0; k < W; k++) {
      size_t p = r * W + k;
      if (p >= maxLen) break;
      bool ina = p < a.size(), inb = p < b.size();
      if (ina != inb || (ina && a[p] != b[p])) { rowDiff[r] = 1; break; }
    }
  std::vector<char> show(rows, 1);
  if (o.contextRows >= 0) {
    std::fill(show.begin(), show.end(), 0);
    for (size_t r = 0; r < rows; r++)
      if (rowDiff[r])
        for (long k = (long)r - o.contextRows; k <= (long)r + o.contextRows; k++)
          if (k >= 0 && k < (long)rows) show[k] = 1;
  }
  std::string out;
  char buf[64];
  const char* red = o.color ? "\x1b[31m" : "";
  const char* rst = o.color ? "\x1b[0m" : "";
  bool skipped = false;
  uint64_t shown = 0;
  auto side = [&](const std::string& s, const std::string& t, size_t r) {
    std::string hex, asc;
    for (int k = 0; k < W; k++) {
      size_t p = r * W + k;
      if (p >= s.size()) { hex += "   "; asc += ' '; continue; }
      bool diff = p >= t.size() || s[p] != t[p];
      unsigned char c = (unsigned char)s[p];
      snprintf(buf, sizeof buf, "%02X ", c);
      hex += diff ? std::string(red) + buf + rst : std::string(buf);
      char ch = (c >= 32 && c < 127) ? (char)c : '.';
      asc += diff ? std::string(red) + ch + rst : std::string(1, ch);
    }
    return hex + " " + asc;
  };
  for (size_t r = 0; r < rows; r++) {
    if (!show[r]) { skipped = true; continue; }
    if (o.maxRows && shown >= o.maxRows) { out += "...\n"; break; }
    if (skipped) { out += "   ...\n"; skipped = false; }
    snprintf(buf, sizeof buf, "%08zX  ", r * (size_t)W);
    out += buf;
    out += side(a, b, r);
    out += rowDiff[r] ? " | " : "   ";
    out += side(b, a, r);
    out += "\n";
    shown++;
  }
  if (skipped) out += "   ...\n";
  return out;
}

}  // namespace bc

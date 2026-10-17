#include "common.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>

namespace bc {

fs::path toPath(const std::string& utf8) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}
std::string fromPath(const fs::path& p) {
  auto u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}
std::string toLower(std::string s) {
  for (auto& c : s)
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  return s;
}
std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace((unsigned char)s[a])) a++;
  while (b > a && isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}
std::vector<std::string> splitAny(const std::string& s, const std::string& seps, bool keepEmpty) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (seps.find(c) != std::string::npos) {
      if (keepEmpty || !cur.empty()) out.push_back(cur);
      cur.clear();
    } else
      cur += c;
  }
  if (keepEmpty || !cur.empty()) out.push_back(cur);
  return out;
}
bool startsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool endsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
  if (from.empty()) return s;
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}
std::string joinPath(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  if (a.back() == '/' || a.back() == '\\') return a + b;
  return a + "/" + b;
}
std::string htmlEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += c;
    }
  }
  return o;
}
std::string xmlEscape(const std::string& s) { return htmlEscape(s); }
std::string csvEscape(const std::string& s) {
  if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
  return "\"" + replaceAll(s, "\"", "\"\"") + "\"";
}
size_t utf8Len(const std::string& s) {
  size_t n = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80) n++;
  return n;
}
std::vector<std::string> utf8Chars(const std::string& s) {
  std::vector<std::string> out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char)s[i];
    size_t l = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    if (i + l > s.size()) l = 1;
    out.push_back(s.substr(i, l));
    i += l;
  }
  return out;
}

bool wildMatch(const std::string& p, const std::string& t, bool icase) {
  size_t pi = 0, ti = 0, star = std::string::npos, mark = 0;
  auto eq = [&](char a, char b) {
    if (icase) {
      a = (a >= 'A' && a <= 'Z') ? char(a + 32) : a;
      b = (b >= 'A' && b <= 'Z') ? char(b + 32) : b;
    }
    return a == b;
  };
  while (ti < t.size()) {
    if (pi < p.size() && (p[pi] == '?' || (p[pi] != '*' && eq(p[pi], t[ti])))) {
      pi++;
      ti++;
    } else if (pi < p.size() && p[pi] == '*') {
      star = pi++;
      mark = ti;
    } else if (star != std::string::npos) {
      pi = star + 1;
      ti = ++mark;
    } else
      return false;
  }
  while (pi < p.size() && p[pi] == '*') pi++;
  return pi == p.size();
}

bool readFile(const std::string& path, std::string& out, std::string* err) {
  std::ifstream f(toPath(path), std::ios::binary);
  if (!f) {
    if (err) *err = "cannot open '" + path + "'";
    return false;
  }
  f.seekg(0, std::ios::end);
  auto n = f.tellg();
  f.seekg(0);
  out.resize((size_t)n);
  if (n > 0) f.read(&out[0], n);
  if (!f && n > 0) {
    if (err) *err = "read error '" + path + "'";
    return false;
  }
  return true;
}
bool writeFile(const std::string& path, const std::string& data, std::string* err) {
  std::error_code ec;
  auto p = toPath(path);
  if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f) {
    if (err) *err = "cannot write '" + path + "'";
    return false;
  }
  f.write(data.data(), (std::streamsize)data.size());
  f.flush();
  if (!f) {
    if (err) *err = "write error '" + path + "'";
    return false;
  }
  return true;
}

static const std::array<uint32_t, 256>& crcTable() {
  static std::array<uint32_t, 256> t = [] {
    std::array<uint32_t, 256> a{};
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      a[i] = c;
    }
    return a;
  }();
  return t;
}
uint32_t crc32(const void* data, size_t n, uint32_t crc) {
  const auto& t = crcTable();
  crc = ~crc;
  auto p = (const unsigned char*)data;
  for (size_t i = 0; i < n; i++) crc = t[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}
bool fileCrc32(const std::string& path, uint32_t& crc, std::string* err) {
  std::ifstream f(toPath(path), std::ios::binary);
  if (!f) {
    if (err) *err = "cannot open '" + path + "'";
    return false;
  }
  std::vector<char> buf(1 << 16);
  crc = 0;
  while (f) {
    f.read(buf.data(), (std::streamsize)buf.size());
    auto g = f.gcount();
    if (g > 0) crc = crc32(buf.data(), (size_t)g, crc);
  }
  return true;
}
bool filesBinaryEqual(const std::string& a, const std::string& b, std::string* err) {
  std::ifstream fa(toPath(a), std::ios::binary), fb(toPath(b), std::ios::binary);
  if (!fa || !fb) {
    if (err) *err = std::string("cannot open '") + (fa ? b : a) + "'";
    return false;
  }
  std::vector<char> ba(1 << 16), bb(1 << 16);
  while (true) {
    fa.read(ba.data(), (std::streamsize)ba.size());
    fb.read(bb.data(), (std::streamsize)bb.size());
    auto ga = fa.gcount(), gb = fb.gcount();
    if (ga != gb) return false;
    if (ga == 0) return true;
    if (std::memcmp(ba.data(), bb.data(), (size_t)ga) != 0) return false;
  }
}

#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
static std::chrono::system_clock::time_point toSys(fs::file_time_type t) { return std::chrono::clock_cast<std::chrono::system_clock>(t); }
static fs::file_time_type fromSys(std::chrono::system_clock::time_point s) { return std::chrono::clock_cast<fs::file_time_type::clock>(s); }
#else
// libc++ has no clock_cast: derive the clock offset from "now" (whole second resolution is all we need).
static std::chrono::system_clock::duration clockOffset() {
  // the epochs differ by a whole number of seconds, so round away the sampling jitter
  static const auto off = std::chrono::round<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch() -
      std::chrono::duration_cast<std::chrono::system_clock::duration>(fs::file_time_type::clock::now().time_since_epoch()));
  return std::chrono::duration_cast<std::chrono::system_clock::duration>(off);
}
static std::chrono::system_clock::time_point toSys(fs::file_time_type t) {
  auto d = std::chrono::duration_cast<std::chrono::system_clock::duration>(t.time_since_epoch()) + clockOffset();
  auto sec = std::chrono::round<std::chrono::seconds>(d);
  return std::chrono::system_clock::time_point(sec);
}
static fs::file_time_type fromSys(std::chrono::system_clock::time_point s) {
  auto d = s.time_since_epoch() - clockOffset();
  return fs::file_time_type(std::chrono::duration_cast<fs::file_time_type::duration>(d));
}
#endif

int64_t fileMTime(const fs::path& p) {
  std::error_code ec;
  auto t = fs::last_write_time(p, ec);
  if (ec) return 0;
  auto s = toSys(t);
  return std::chrono::duration_cast<std::chrono::seconds>(s.time_since_epoch()).count();
}
bool setFileMTime(const fs::path& p, int64_t t) {
  std::error_code ec;
  auto s = std::chrono::system_clock::time_point(std::chrono::seconds(t));
  fs::last_write_time(p, fromSys(s), ec);
  return !ec;
}
std::string formatTime(int64_t t, bool utc) {
  std::time_t tt = (std::time_t)t;
  std::tm tmv{};
#ifdef _WIN32
  if (utc) gmtime_s(&tmv, &tt); else localtime_s(&tmv, &tt);
#else
  if (utc) gmtime_r(&tt, &tmv); else localtime_r(&tt, &tmv);
#endif
  char buf[64];
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tmv);
  return buf;
}
std::string formatSize(uint64_t n) {
  std::string s = std::to_string(n), o;
  int c = 0;
  for (size_t i = s.size(); i-- > 0;) {
    o.insert(o.begin(), s[i]);
    if (++c % 3 == 0 && i > 0) o.insert(o.begin(), ',');
  }
  return o;
}
}  // namespace bc

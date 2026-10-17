#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bc {

static const Json kNull;

Json& Json::operator[](const std::string& k) {
  if (t != Obj) { *this = Json::object(); }
  for (auto& p : o)
    if (p.first == k) return p.second;
  o.emplace_back(k, Json());
  return o.back().second;
}
const Json& Json::at(const std::string& k) const {
  if (t == Obj)
    for (auto& p : o)
      if (p.first == k) return p.second;
  return kNull;
}
bool Json::has(const std::string& k) const {
  if (t == Obj)
    for (auto& p : o)
      if (p.first == k) return true;
  return false;
}
Json& Json::push(Json v) {
  if (t != Arr) *this = Json::array();
  a.push_back(std::move(v));
  return a.back();
}

static void appendU(std::string& out, unsigned cp) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "\\u%04x", cp);
  out += buf;
}

// Length of the valid UTF-8 sequence starting at s[i] (>=2 for multi-byte), or 0 if invalid.
static int utf8SeqLen(const std::string& s, size_t i) {
  size_t n = s.size();
  unsigned char c = s[i];
  int len = (c >= 0xF0 && c < 0xF8) ? 4 : (c >= 0xE0 && c < 0xF0) ? 3 : (c >= 0xC2 && c < 0xE0) ? 2 : 0;
  if (len == 0 || i + len > n) return 0;
  for (int k = 1; k < len; k++)
    if ((s[i + k] & 0xC0) != 0x80) return 0;
  if (len == 3) {
    unsigned cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
    if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
  }
  if (len == 4) {
    unsigned cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
    if (cp < 0x10000 || cp > 0x10FFFF) return 0;
  }
  return len;
}

size_t utf16Units(const std::string& s, size_t byteOff) {
  size_t units = 0, i = 0;
  if (byteOff > s.size()) byteOff = s.size();
  while (i < byteOff) {
    unsigned char c = s[i];
    if (c < 0x80) { units++; i++; continue; }
    int len = utf8SeqLen(s, i);
    if (len == 0) { units++; i++; }
    else { units += (len == 4) ? 2 : 1; i += len; }
  }
  return units;
}

// Strings are emitted as valid UTF-8 JSON. Bytes that are not valid UTF-8 (e.g. ANSI text) are
// mapped to the code point with the same value (Latin-1) so nothing is lost or breaks the page.
static void dumpString(const std::string& s, std::string& out) {
  out += '"';
  size_t i = 0, n = s.size();
  while (i < n) {
    unsigned char c = s[i];
    if (c < 0x80) {
      switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
          if (c < 0x20 || c == 0x7f) appendU(out, c);
          else out += (char)c;
      }
      i++;
      continue;
    }
    int len = utf8SeqLen(s, i);
    bool ok = len > 0;
    if (ok) {
      out.append(s, i, len);
      i += len;
    } else {
      // Latin-1 fallback: emit as two-byte UTF-8 for the same code point
      out += (char)(0xC0 | (c >> 6));
      out += (char)(0x80 | (c & 0x3F));
      i++;
    }
  }
  out += '"';
}

void Json::dump(std::string& out) const {
  switch (t) {
    case Null: out += "null"; break;
    case Bool: out += b ? "true" : "false"; break;
    case Num:
      if (std::isfinite(n) && n == std::floor(n) && std::fabs(n) < 9e15) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%lld", (long long)n);
        out += buf;
      } else if (std::isfinite(n)) {
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.17g", n);
        out += buf;
      } else out += "null";
      break;
    case Str: dumpString(s, out); break;
    case Arr:
      out += '[';
      for (size_t i = 0; i < a.size(); i++) {
        if (i) out += ',';
        a[i].dump(out);
      }
      out += ']';
      break;
    case Obj:
      out += '{';
      for (auto it = o.begin(); it != o.end(); ++it) {
        if (it != o.begin()) out += ',';
        dumpString(it->first, out);
        out += ':';
        it->second.dump(out);
      }
      out += '}';
      break;
  }
}
std::string Json::dump() const {
  std::string s;
  dump(s);
  return s;
}

namespace {
struct P {
  const std::string& s;
  size_t i = 0;
  std::string err;
  int depth = 0;
  void ws() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
  }
  bool fail(const std::string& m) {
    if (err.empty()) err = m + " at offset " + std::to_string(i);
    return false;
  }
  static void putUtf8(std::string& o, unsigned cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
  }
  bool hex4(unsigned& v) {
    if (i + 4 > s.size()) return fail("bad \\u escape");
    v = 0;
    for (int k = 0; k < 4; k++) {
      char c = s[i++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= c - '0';
      else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
      else return fail("bad hex digit");
    }
    return true;
  }
  bool str(std::string& out) {
    if (s[i] != '"') return fail("expected string");
    i++;
    while (i < s.size()) {
      char c = s[i++];
      if (c == '"') return true;
      if (c != '\\') { out += c; continue; }
      if (i >= s.size()) break;
      char e = s[i++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned v;
          if (!hex4(v)) return false;
          if (v >= 0xD800 && v < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
            size_t save = i;
            i += 2;
            unsigned lo;
            if (!hex4(lo)) return false;
            if (lo >= 0xDC00 && lo < 0xE000) v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
            else i = save;
          }
          putUtf8(out, v);
          break;
        }
        default: return fail("bad escape");
      }
    }
    return fail("unterminated string");
  }
  bool val(Json& out) {
    ws();
    if (i >= s.size()) return fail("unexpected end");
    if (++depth > 200) return fail("too deep");
    struct D { int& d; ~D() { d--; } } guard{depth};
    char c = s[i];
    if (c == '{') {
      i++;
      out = Json::object();
      ws();
      if (i < s.size() && s[i] == '}') { i++; return true; }
      for (;;) {
        ws();
        std::string k;
        if (i >= s.size() || !str(k)) return fail("expected key");
        ws();
        if (i >= s.size() || s[i] != ':') return fail("expected ':'");
        i++;
        Json v;
        if (!val(v)) return false;
        out.o.emplace_back(std::move(k), std::move(v));
        ws();
        if (i < s.size() && s[i] == ',') { i++; continue; }
        if (i < s.size() && s[i] == '}') { i++; return true; }
        return fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      i++;
      out = Json::array();
      ws();
      if (i < s.size() && s[i] == ']') { i++; return true; }
      for (;;) {
        Json v;
        if (!val(v)) return false;
        out.a.push_back(std::move(v));
        ws();
        if (i < s.size() && s[i] == ',') { i++; continue; }
        if (i < s.size() && s[i] == ']') { i++; return true; }
        return fail("expected ',' or ']'");
      }
    }
    if (c == '"') {
      out.t = Json::Str;
      return str(out.s);
    }
    if (s.compare(i, 4, "true") == 0) { i += 4; out = Json(true); return true; }
    if (s.compare(i, 5, "false") == 0) { i += 5; out = Json(false); return true; }
    if (s.compare(i, 4, "null") == 0) { i += 4; out = Json(); return true; }
    char* end = nullptr;
    double d = std::strtod(s.c_str() + i, &end);
    if (end == s.c_str() + i) return fail("unexpected character");
    i = end - s.c_str();
    out = Json(d);
    return true;
  }
};
}  // namespace

bool Json::parse(const std::string& src, Json& out, std::string* err) {
  P p{src, 0, std::string(), 0};
  Json v;
  if (!p.val(v)) {
    if (err) *err = p.err;
    return false;
  }
  p.ws();
  if (p.i != src.size()) {
    if (err) *err = "trailing characters in JSON";
    return false;
  }
  out = std::move(v);
  return true;
}

}  // namespace bc

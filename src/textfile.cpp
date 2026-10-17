#include "textfile.h"

#include "common.h"

namespace bc {

const char* encName(Enc e) {
  switch (e) {
    case Enc::Auto: return "auto";
    case Enc::UTF8: return "UTF-8";
    case Enc::UTF8BOM: return "UTF-8 (BOM)";
    case Enc::UTF16LE: return "UTF-16LE";
    case Enc::UTF16BE: return "UTF-16BE";
    case Enc::ANSI: return "ANSI";
  }
  return "?";
}
const char* eolName(Eol e) {
  switch (e) {
    case Eol::None: return "none";
    case Eol::LF: return "Unix (LF)";
    case Eol::CRLF: return "DOS (CRLF)";
    case Eol::CR: return "Mac (CR)";
  }
  return "?";
}
const char* eolStr(Eol e) {
  switch (e) {
    case Eol::LF: return "\n";
    case Eol::CRLF: return "\r\n";
    case Eol::CR: return "\r";
    default: return "";
  }
}
bool parseEnc(const std::string& name, Enc& out) {
  std::string n = toLower(name);
  if (n == "auto") out = Enc::Auto;
  else if (n == "utf8" || n == "utf-8") out = Enc::UTF8;
  else if (n == "utf8bom" || n == "utf-8-bom") out = Enc::UTF8BOM;
  else if (n == "utf16" || n == "utf16le" || n == "utf-16" || n == "utf-16le") out = Enc::UTF16LE;
  else if (n == "utf16be" || n == "utf-16be") out = Enc::UTF16BE;
  else if (n == "ansi" || n == "latin1" || n == "gbk" || n == "raw") out = Enc::ANSI;
  else return false;
  return true;
}

static bool validUtf8(const std::string& s) {
  size_t i = 0, n = s.size();
  while (i < n) {
    unsigned char c = (unsigned char)s[i];
    size_t l;
    if (c < 0x80) l = 1;
    else if (c >= 0xC2 && c <= 0xDF) l = 2;
    else if (c >= 0xE0 && c <= 0xEF) l = 3;
    else if (c >= 0xF0 && c <= 0xF4) l = 4;
    else return false;
    if (i + l > n) return false;
    for (size_t k = 1; k < l; k++)
      if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
    i += l;
  }
  return true;
}

static bool hasBom(const std::string& r, Enc& e) {
  if (r.size() >= 3 && (unsigned char)r[0] == 0xEF && (unsigned char)r[1] == 0xBB && (unsigned char)r[2] == 0xBF) { e = Enc::UTF8BOM; return true; }
  if (r.size() >= 2 && (unsigned char)r[0] == 0xFF && (unsigned char)r[1] == 0xFE) { e = Enc::UTF16LE; return true; }
  if (r.size() >= 2 && (unsigned char)r[0] == 0xFE && (unsigned char)r[1] == 0xFF) { e = Enc::UTF16BE; return true; }
  return false;
}

bool looksBinary(const std::string& raw) {
  Enc e;
  if (hasBom(raw, e) && (e == Enc::UTF16LE || e == Enc::UTF16BE)) return false;
  size_t n = std::min<size_t>(raw.size(), 65536);
  size_t ctrl = 0;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)raw[i];
    if (c == 0) return true;
    if (c < 9 || (c > 13 && c < 32 && c != 27)) ctrl++;
  }
  return n > 0 && ctrl * 100 > n;  // >1% control chars
}

Enc detectEncoding(const std::string& raw) {
  Enc e;
  if (hasBom(raw, e)) return e;
  // BOM-less UTF-16 heuristic: many NULs at odd or even positions in ASCII-ish text
  size_t n = std::min<size_t>(raw.size(), 4096);
  if (n >= 4) {
    size_t ev = 0, od = 0;
    for (size_t i = 0; i < n; i++)
      if (raw[i] == 0) (i % 2 ? od : ev)++;
    if (od * 4 > n / 2 && ev == 0) return Enc::UTF16LE;
    if (ev * 4 > n / 2 && od == 0) return Enc::UTF16BE;
  }
  return validUtf8(raw) ? Enc::UTF8 : Enc::ANSI;
}

static void appendUtf8(std::string& o, uint32_t cp) {
  if (cp < 0x80) o += char(cp);
  else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
  else { o += char(0xF0 | (cp >> 18)); o += char(0x80 | ((cp >> 12) & 0x3F)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
}

static std::string utf16ToUtf8(const std::string& raw, size_t start, bool be) {
  std::string o;
  size_t n = raw.size();
  auto at = [&](size_t i) -> uint32_t {
    unsigned char a = (unsigned char)raw[i], b = (unsigned char)raw[i + 1];
    return be ? (a << 8 | b) : (b << 8 | a);
  };
  for (size_t i = start; i + 1 < n; i += 2) {
    uint32_t u = at(i);
    if (u >= 0xD800 && u < 0xDC00 && i + 3 < n) {
      uint32_t l = at(i + 2);
      if (l >= 0xDC00 && l < 0xE000) {
        appendUtf8(o, 0x10000 + ((u - 0xD800) << 10) + (l - 0xDC00));
        i += 2;
        continue;
      }
    }
    appendUtf8(o, u);
  }
  return o;
}

static std::string utf8ToUtf16(const std::string& s, bool be) {
  std::string o;
  auto put = [&](uint32_t u) {
    if (be) { o += char(u >> 8); o += char(u & 0xFF); }
    else { o += char(u & 0xFF); o += char(u >> 8); }
  };
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char)s[i];
    uint32_t cp;
    size_t l;
    if (c < 0x80) { cp = c; l = 1; }
    else if ((c >> 5) == 6) { cp = c & 0x1F; l = 2; }
    else if ((c >> 4) == 14) { cp = c & 0x0F; l = 3; }
    else if ((c >> 3) == 30) { cp = c & 0x07; l = 4; }
    else { cp = c; l = 1; }
    if (i + l > s.size()) { cp = c; l = 1; }
    else for (size_t k = 1; k < l; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
    i += l;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      put(0xD800 + (cp >> 10));
      put(0xDC00 + (cp & 0x3FF));
    } else
      put(cp);
  }
  return o;
}

TextFile parseText(const std::string& raw, Enc forced) {
  TextFile tf;
  Enc e = forced == Enc::Auto ? detectEncoding(raw) : forced;
  tf.enc = e;
  std::string body;
  switch (e) {
    case Enc::UTF8BOM: body = (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF) ? raw.substr(3) : raw; break;
    case Enc::UTF16LE: body = utf16ToUtf8(raw, (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) ? 2 : 0, false); break;
    case Enc::UTF16BE: body = utf16ToUtf8(raw, (raw.size() >= 2 && (unsigned char)raw[0] == 0xFE && (unsigned char)raw[1] == 0xFF) ? 2 : 0, true); break;
    default: body = raw;
  }
  size_t cnt[4] = {0, 0, 0, 0};
  size_t i = 0, n = body.size(), st = 0;
  while (i < n) {
    char c = body[i];
    if (c == '\n' || c == '\r') {
      Eol el = Eol::LF;
      size_t adv = 1;
      if (c == '\r') {
        if (i + 1 < n && body[i + 1] == '\n') { el = Eol::CRLF; adv = 2; }
        else el = Eol::CR;
      }
      tf.lines.push_back({body.substr(st, i - st), el});
      cnt[(int)el]++;
      i += adv;
      st = i;
    } else
      i++;
  }
  if (st < n) tf.lines.push_back({body.substr(st), Eol::None});
  int best = 0;
  size_t bc = 0, kinds = 0;
  for (int k = 1; k < 4; k++) {
    if (cnt[k]) kinds++;
    if (cnt[k] > bc) { bc = cnt[k]; best = k; }
  }
  tf.dominantEol = bc ? (Eol)best : Eol::LF;
  tf.mixedEol = kinds > 1;
  return tf;
}

std::string serializeText(const TextFile& tf, Enc enc, bool forceEol, Eol eol) {
  std::string body;
  for (size_t i = 0; i < tf.lines.size(); i++) {
    body += tf.lines[i].text;
    Eol e = tf.lines[i].eol;
    if (forceEol && e != Eol::None) e = eol;
    body += eolStr(e);
  }
  switch (enc) {
    case Enc::UTF8BOM: return "\xEF\xBB\xBF" + body;
    case Enc::UTF16LE: return std::string("\xFF\xFE", 2) + utf8ToUtf16(body, false);
    case Enc::UTF16BE: return std::string("\xFE\xFF", 2) + utf8ToUtf16(body, true);
    default: return body;
  }
}
std::string serializeText(const TextFile& tf) { return serializeText(tf, tf.enc); }

bool loadTextFile(const std::string& path, TextFile& tf, std::string* err, Enc forced) {
  std::string raw;
  if (!readFile(path, raw, err)) return false;
  tf = parseText(raw, forced);
  return true;
}

}  // namespace bc

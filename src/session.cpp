#include "session.h"

#include <algorithm>
#include <cctype>

#include "common.h"

namespace bc {

std::string XNode::attr(const std::string& k, const std::string& def) const {
  for (auto& a : attrs)
    if (a.first == k) return a.second;
  return def;
}
const XNode* XNode::child(const std::string& t) const {
  for (auto& k : kids)
    if (k.tag == t) return &k;
  return nullptr;
}
void XNode::setAttr(const std::string& k, const std::string& v) {
  for (auto& a : attrs)
    if (a.first == k) { a.second = v; return; }
  attrs.push_back({k, v});
}

namespace {
void appendUtf8(std::string& o, unsigned cp) {
  if (cp < 0x80) o += (char)cp;
  else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
  else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
}
std::string unescape(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] != '&') { o += s[i]; continue; }
    size_t e = s.find(';', i);
    if (e == std::string::npos) { o += s[i]; continue; }
    std::string ent = s.substr(i + 1, e - i - 1);
    if (ent == "lt") o += '<';
    else if (ent == "gt") o += '>';
    else if (ent == "amp") o += '&';
    else if (ent == "quot") o += '"';
    else if (ent == "apos") o += '\'';
    else if (!ent.empty() && ent[0] == '#') {
      unsigned cp = 0;
      try { cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? std::stoul(ent.substr(2), nullptr, 16) : std::stoul(ent.substr(1)); }
      catch (...) { o += s[i]; continue; }
      appendUtf8(o, cp);
    } else { o += s[i]; continue; }
    i = e;
  }
  return o;
}

struct P {
  const std::string& s;
  size_t i = 0;
  std::string err;
  void ws() { while (i < s.size() && isspace((unsigned char)s[i])) i++; }
  bool skipMisc() {  // comments, PI, doctype
    for (;;) {
      ws();
      if (s.compare(i, 4, "<!--") == 0) {
        size_t e = s.find("-->", i + 4);
        if (e == std::string::npos) return false;
        i = e + 3;
      } else if (s.compare(i, 2, "<?") == 0) {
        size_t e = s.find("?>", i + 2);
        if (e == std::string::npos) return false;
        i = e + 2;
      } else if (s.compare(i, 9, "<!DOCTYPE") == 0) {
        size_t e = s.find('>', i);
        if (e == std::string::npos) return false;
        i = e + 1;
      } else
        return true;
    }
  }
  static bool nameCh(char c) { return isalnum((unsigned char)c) || c == '_' || c == ':' || c == '-' || c == '.' || (unsigned char)c >= 0x80; }
  bool elem(XNode& n) {
    if (i >= s.size() || s[i] != '<') { err = "expected '<'"; return false; }
    i++;
    size_t st = i;
    while (i < s.size() && nameCh(s[i])) i++;
    n.tag = s.substr(st, i - st);
    if (n.tag.empty()) { err = "empty tag name"; return false; }
    for (;;) {
      ws();
      if (i >= s.size()) { err = "unexpected end"; return false; }
      if (s[i] == '/') {
        if (s.compare(i, 2, "/>") != 0) { err = "bad tag end"; return false; }
        i += 2;
        return true;
      }
      if (s[i] == '>') { i++; break; }
      size_t a = i;
      while (i < s.size() && nameCh(s[i])) i++;
      std::string k = s.substr(a, i - a);
      if (k.empty()) { err = "bad attribute"; return false; }
      ws();
      if (i >= s.size() || s[i] != '=') { err = "expected '='"; return false; }
      i++;
      ws();
      if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) { err = "expected quote"; return false; }
      char q = s[i++];
      size_t e = s.find(q, i);
      if (e == std::string::npos) { err = "unterminated attribute"; return false; }
      n.attrs.push_back({k, unescape(s.substr(i, e - i))});
      i = e + 1;
    }
    for (;;) {
      size_t t = s.find('<', i);
      if (t == std::string::npos) { err = "unterminated element <" + n.tag + ">"; return false; }
      n.text += unescape(s.substr(i, t - i));
      i = t;
      if (s.compare(i, 4, "<!--") == 0 || s.compare(i, 2, "<?") == 0) {
        if (!skipMisc()) { err = "bad comment"; return false; }
        continue;
      }
      if (s.compare(i, 9, "<![CDATA[") == 0) {
        size_t e = s.find("]]>", i);
        if (e == std::string::npos) { err = "bad CDATA"; return false; }
        n.text += s.substr(i + 9, e - i - 9);
        i = e + 3;
        continue;
      }
      if (s.compare(i, 2, "</") == 0) {
        size_t e = s.find('>', i);
        if (e == std::string::npos) { err = "bad close tag"; return false; }
        std::string ct = trim(s.substr(i + 2, e - i - 2));
        if (ct != n.tag) { err = "mismatched </" + ct + "> for <" + n.tag + ">"; return false; }
        i = e + 1;
        return true;
      }
      XNode k;
      if (!elem(k)) return false;
      n.kids.push_back(std::move(k));
    }
  }
};

void writeNode(const XNode& n, int depth, std::string& o) {
  std::string ind(depth, '\t');
  o += ind + "<" + n.tag;
  for (auto& a : n.attrs) o += " " + a.first + "=\"" + xmlEscape(a.second) + "\"";
  std::string txt = trim(n.text);
  if (n.kids.empty() && txt.empty()) { o += "/>\n"; return; }
  o += ">";
  if (!n.kids.empty()) {
    o += "\n";
    for (auto& k : n.kids) writeNode(k, depth + 1, o);
    o += ind;
  } else
    o += xmlEscape(txt);
  o += "</" + n.tag + ">\n";
}
}  // namespace

bool parseXml(const std::string& src, XNode& root, std::string* err) {
  P p{src, 0, {}};
  size_t st = 0;
  if (src.compare(0, 3, "\xEF\xBB\xBF") == 0) st = 3;
  p.i = st;
  if (!p.skipMisc()) { if (err) *err = "malformed prolog"; return false; }
  XNode r;
  if (!p.elem(r)) { if (err) *err = p.err + " at offset " + std::to_string(p.i); return false; }
  root = std::move(r);
  return true;
}

std::string writeXml(const XNode& root) {
  std::string o = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  writeNode(root, 0, o);
  return o;
}

// ---------------------------------------------------------------------------------
static void collect(const XNode& items, const std::string& folder, std::vector<Session>& out) {
  for (auto& k : items.kids) {
    if (k.tag == "TSessionFolder") {
      std::string name = k.attr("Value");
      std::string sub = name.empty() ? folder : (folder.empty() ? name : folder + "/" + name);
      if (auto* it = k.child("Items")) collect(*it, sub, out);
    } else if (k.tag.size() > 7 && endsWith(k.tag, "Session")) {
      Session s;
      s.type = k.tag;
      s.name = k.attr("Value");
      s.folder = folder;
      for (auto& c : k.kids) {
        if (c.tag == "LastModified") s.lastModified = c.attr("Value");
        else if (c.tag == "Specs") {
          for (auto& sp : c.kids) {
            if (sp.tag == "Left") s.left = sp.attr("Value");
            else if (sp.tag == "Right") s.right = sp.attr("Value");
            else if (sp.tag == "Center") s.center = sp.attr("Value");
          }
        } else
          s.extra.push_back({c.tag, c.attr("Value")});
      }
      out.push_back(std::move(s));
    }
  }
}

bool SessionStore::load(const std::string& path, std::string* err) {
  std::string raw;
  if (!readFile(path, raw, err)) return false;
  XNode root;
  if (!parseXml(raw, root, err)) return false;
  items.clear();
  for (auto& k : root.kids)
    if (k.tag == "TSessionFolder") {
      if (auto* it = k.child("Items")) collect(*it, "", items);
    }
  return true;
}

bool SessionStore::save(const std::string& path, std::string* err) const {
  XNode root;
  root.tag = "BCSessions";
  root.setAttr("Version", "1");
  root.setAttr("MinVersion", "1");
  XNode top;
  top.tag = "TSessionFolder";
  XNode topItems;
  topItems.tag = "Items";
  // folder tree: create nested TSessionFolder nodes by path
  auto getFolder = [&](const std::string& fpath) -> XNode* {
    XNode* cur = &topItems;
    for (auto& part : splitAny(fpath, "/")) {
      XNode* found = nullptr;
      for (auto& k : cur->kids)
        if (k.tag == "TSessionFolder" && k.attr("Value") == part) found = &k;
      if (!found) {
        XNode f;
        f.tag = "TSessionFolder";
        f.setAttr("Value", part);
        XNode it;
        it.tag = "Items";
        f.kids.push_back(it);
        cur->kids.push_back(std::move(f));
        found = &cur->kids.back();
      }
      cur = &found->kids[0];
    }
    return cur;
  };
  for (auto& s : items) {
    XNode n;
    n.tag = s.type;
    n.setAttr("Value", s.name);
    XNode lm;
    lm.tag = "LastModified";
    lm.setAttr("Value", s.lastModified);
    n.kids.push_back(lm);
    XNode specs;
    specs.tag = "Specs";
    auto add = [&](const char* t, const std::string& v) {
      if (v.empty()) return;
      XNode x;
      x.tag = t;
      x.setAttr("Value", v);
      specs.kids.push_back(x);
    };
    add("Left", s.left);
    add("Center", s.center);
    add("Right", s.right);
    n.kids.push_back(specs);
    for (auto& e : s.extra) {
      XNode x;
      x.tag = e.first;
      x.setAttr("Value", e.second);
      n.kids.push_back(x);
    }
    getFolder(s.folder)->kids.push_back(std::move(n));
  }
  top.kids.push_back(topItems);
  root.kids.push_back(top);
  return writeFile(path, writeXml(root), err);
}

const Session* SessionStore::find(const std::string& name) const {
  for (auto& s : items)
    if (s.name == name) return &s;
  std::string ln = toLower(name);
  for (auto& s : items)
    if (toLower(s.name) == ln) return &s;
  const Session* hit = nullptr;
  int n = 0;
  for (auto& s : items)
    if (startsWith(toLower(s.name), ln)) { hit = &s; n++; }
  return n == 1 ? hit : nullptr;
}

bool SessionStore::remove(const std::string& name) {
  auto it = std::find_if(items.begin(), items.end(), [&](const Session& s) { return s.name == name; });
  if (it == items.end()) return false;
  items.erase(it);
  return true;
}

void SessionStore::put(const Session& s) {
  for (auto& x : items)
    if (x.name == s.name && x.folder == s.folder) { x = s; return; }
  items.push_back(s);
}

std::string nativeSessionPath(const std::string& p) {
#ifdef _WIN32
  return p;
#else
  std::string s = p;
  if (s.size() >= 3 && isalpha((unsigned char)s[0]) && s[1] == ':' && (s[2] == '\\' || s[2] == '/'))
    s = "/mnt/" + std::string(1, (char)tolower(s[0])) + s.substr(2);
  else if (startsWith(toLower(s), "\\\\wsl.localhost\\") || startsWith(toLower(s), "\\\\wsl$\\")) {
    size_t a = s.find('\\', 2);
    size_t b = s.find('\\', a + 1);
    s = b == std::string::npos ? "/" : s.substr(b);
  }
  for (auto& c : s)
    if (c == '\\') c = '/';
  return s;
#endif
}

}  // namespace bc

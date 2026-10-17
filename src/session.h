// Minimal XML reader/writer + BCSessions.xml support (named compare sessions).
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace bc {

struct XNode {
  std::string tag;
  std::vector<std::pair<std::string, std::string>> attrs;
  std::vector<XNode> kids;
  std::string text;
  std::string attr(const std::string& k, const std::string& def = "") const;
  const XNode* child(const std::string& tag) const;
  void setAttr(const std::string& k, const std::string& v);
};
bool parseXml(const std::string& src, XNode& root, std::string* err);
std::string writeXml(const XNode& root);  // tab indented, with XML declaration

struct Session {
  std::string name;
  std::string type;    // TDirCompareSession / TTextCompareSession / THexCompareSession ...
  std::string folder;  // "a/b" path of enclosing session folders (without the unnamed root)
  std::string left, right, center;
  std::string lastModified;
  std::vector<std::pair<std::string, std::string>> extra;  // any other <Tag Value=".."/> entries (filters, ...)
  bool isDir() const { return type == "TDirCompareSession"; }
  bool isText() const { return type == "TTextCompareSession"; }
};

struct SessionStore {
  std::vector<Session> items;
  bool load(const std::string& path, std::string* err);
  bool save(const std::string& path, std::string* err) const;
  const Session* find(const std::string& name) const;  // exact, then case-insensitive, then unique prefix
  bool remove(const std::string& name);
  void put(const Session& s);                          // add or replace by name
};

// Convert a path written by BC on Windows into something usable on this OS.
std::string nativeSessionPath(const std::string& p);

}  // namespace bc

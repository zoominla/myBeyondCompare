// Tiny JSON value / parser / writer used by the local GUI server.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <list>
#include <vector>

namespace bc {

struct Json {
  enum Type { Null, Bool, Num, Str, Arr, Obj } t = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<Json> a;
  std::list<std::pair<std::string, Json>> o;  // list: references returned by operator[] stay valid

  Json() {}
  Json(bool v) : t(Bool), b(v) {}
  Json(int v) : t(Num), n(v) {}
  Json(int64_t v) : t(Num), n((double)v) {}
  Json(uint64_t v) : t(Num), n((double)v) {}
  Json(double v) : t(Num), n(v) {}
  Json(const char* v) : t(Str), s(v) {}
  Json(const std::string& v) : t(Str), s(v) {}
  static Json array() { Json j; j.t = Arr; return j; }
  static Json object() { Json j; j.t = Obj; return j; }

  Json& operator[](const std::string& k);        // object access, creates
  const Json& at(const std::string& k) const;    // object access, Null when absent
  Json& push(Json v);                            // append to array
  bool has(const std::string& k) const;
  bool isNull() const { return t == Null; }

  std::string str(const std::string& def = "") const { return t == Str ? s : def; }
  double num(double def = 0) const { return t == Num ? n : def; }
  int64_t i64(int64_t def = 0) const { return t == Num ? (int64_t)n : def; }
  bool boolean(bool def = false) const { return t == Bool ? b : def; }
  size_t size() const { return t == Arr ? a.size() : t == Obj ? o.size() : 0; }

  std::string dump() const;
  void dump(std::string& out) const;
  static bool parse(const std::string& src, Json& out, std::string* err);
};

// Number of UTF-16 code units in the first byteOff bytes of s (invalid bytes count as one unit,
// matching how Json::dump emits them) - lets C++ byte offsets be used as JavaScript string indexes.
size_t utf16Units(const std::string& s, size_t byteOff);

}  // namespace bc

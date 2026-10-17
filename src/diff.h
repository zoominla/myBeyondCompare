// Line / character diff engine (Myers, patience, positional, sorted) with BC style
// "importance" classification, similarity pairing of changed blocks and inline diff.
#pragma once
#include <memory>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "textfile.h"

namespace bc {

enum class Align { Smart, Complete, Fast, Patience, Sort, None };
bool parseAlign(const std::string& s, Align& out);

struct DiffOptions {
  bool ignoreCase = false;
  bool ignoreWsAll = false;       // remove every whitespace char
  bool ignoreWsLeading = false;
  bool ignoreWsTrailing = false;
  bool ignoreWsAmount = false;    // runs of whitespace compare equal
  bool ignoreEol = true;          // line-end style differences are unimportant
  bool ignoreBlankLines = false;  // empty (after rules) lines become unimportant orphans
  std::vector<std::string> ignoreRegexes;  // "unimportant text": matches are removed before compare
  bool ignoreUnimportant = false; // treat unimportant differences as equal in the result
  Align align = Align::Smart;
};

// Applies the rules to a line and produces the comparison key.
class Normalizer {
 public:
  explicit Normalizer(const DiffOptions& o);
  std::string key(const Line& l) const;
  std::string keyText(const std::string& s) const;
  bool active() const { return plain_ == false; }
 private:
  DiffOptions o_;
  std::vector<std::regex> re_;
  bool plain_;
};

enum class RowKind { Same, Unimportant, Different, LeftOnly, RightOnly };

struct DiffRow {
  int l = -1, r = -1;  // line indices, -1 = none
  RowKind kind = RowKind::Same;
  bool minor = false;  // orphan that only exists because of an ignored rule (e.g. blank line)
};
inline bool rowImportant(const DiffRow& r) {
  return !r.minor && (r.kind == RowKind::Different || r.kind == RowKind::LeftOnly || r.kind == RowKind::RightOnly);
}

struct Section {  // a maximal run of rows that are not Same
  size_t begin = 0, end = 0;  // [begin,end) into rows
  bool important = false;
};

struct DiffResult {
  std::vector<DiffRow> rows;
  std::vector<Section> sections;
  size_t nSame = 0, nUnimportant = 0, nDifferent = 0, nLeftOnly = 0, nRightOnly = 0;
  size_t importantSections() const;
  bool identical() const { return nUnimportant + nDifferent + nLeftOnly + nRightOnly == 0; }
  bool importantSame() const { return nDifferent + nLeftOnly + nRightOnly == 0; }
};

// Longest common subsequence of two integer sequences; returns matching index pairs.
// maxCost>0 bounds the search effort (heuristic fallback when exceeded).
std::vector<std::pair<int, int>> lcsMatches(const std::vector<int>& a, const std::vector<int>& b, long maxCost = 0);
std::vector<std::pair<int, int>> patienceMatches(const std::vector<int>& a, const std::vector<int>& b);

DiffResult diffLines(const std::vector<Line>& L, const std::vector<Line>& R, const DiffOptions& opt);

struct Span { size_t begin, end; };  // byte offsets
struct InlineDiff { std::vector<Span> left, right; };
InlineDiff inlineDiff(const std::string& a, const std::string& b, const DiffOptions& opt);

// Result of comparing two text files
struct TextCompare {
  TextFile left, right;
  DiffResult diff;
  bool bytesEqual = false;
};
bool compareTextFiles(const std::string& lpath, const std::string& rpath, const DiffOptions& opt,
                      TextCompare& out, std::string* err, Enc lenc = Enc::Auto, Enc renc = Enc::Auto);

}  // namespace bc

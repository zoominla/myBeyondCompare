// Three-way merge (left / right / center(base) -> output)
#pragma once
#include <string>
#include <vector>

#include "diff.h"
#include "textfile.h"

namespace bc {

enum class Resolve { None, Left, Right, Both };

struct MergeOptions {
  DiffOptions diff;
  Resolve resolve = Resolve::None;  // automatic conflict resolution
  bool markConflicts = false;       // /force : write conflict markers
  std::string leftLabel = "Left", rightLabel = "Right", baseLabel = "Center";
};

// Region-level view of a merge (used by the GUI to let the user pick per conflict).
struct MergeRegion {
  enum Kind { Same, LeftChanged, RightChanged, BothSame, Conflict } kind = Same;
  std::vector<Line> base, left, right;
  std::vector<Line> out;  // what merge3 emitted for this region
};

struct MergeResult {
  std::vector<MergeRegion> regions;
  TextFile output;
  int conflicts = 0;        // number of unresolved conflicts
  int resolved = 0;         // conflicts resolved by option
  int autoMerged = 0;       // regions changed on one side only (or identical on both)
};

MergeResult merge3(const TextFile& base, const TextFile& left, const TextFile& right, const MergeOptions& opt);

}  // namespace bc

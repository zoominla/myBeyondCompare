// Folder compare: scan, filter, compare, summarise, plan and run sync / copy / delete operations.
#pragma once
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "common.h"
#include "diff.h"

namespace bc {

// BC style name filter: "*.c;*.h;-*.obj;-.git\"  ("|" starts an exclude list as well)
struct NameFilter {
  std::vector<std::string> fileInc, fileExc, dirInc, dirExc;
  std::string spec;
  void parse(const std::string& s);
  bool acceptFile(const std::string& name, const std::string& rel, bool icase) const;
  bool acceptDir(const std::string& name, const std::string& rel, bool icase) const;
  bool empty() const { return fileInc.empty() && fileExc.empty() && dirInc.empty() && dirExc.empty(); }
};

enum class DStatus { Same, Different, Similar, LeftOnly, RightOnly, TypeMismatch, Error };
enum class Newer { None, Left, Right };
const char* statusName(DStatus s);

struct DirEntry {
  std::string rel;   // display path relative to roots, '/' separated
  std::string relL, relR;  // actual relative path on each side (differs from rel only in case)
  std::string name;
  int depth = 0;
  bool hasL = false, hasR = false;
  bool dirL = false, dirR = false;
  uint64_t sizeL = 0, sizeR = 0;
  int64_t mtimeL = 0, mtimeR = 0;
  DStatus status = DStatus::Same;
  Newer newer = Newer::None;
  std::string reason;   // why different: size,time,content,...
  std::string error;
  bool selected = false;
  bool expanded = true;
  size_t subtreeEnd = 0;  // index one past last descendant (folders)
  bool isDir() const { return dirL || dirR; }
};

struct DirCompareOptions {
  bool recursive = true;
  int maxDepth = -1;
  bool cmpSize = false, cmpTime = false, cmpCrc = false, cmpBinary = true, cmpRules = false;
  int64_t timeTolerance = 0;  // seconds
  bool ignoreDst = false;     // ignore exactly 1 hour differences
  bool icaseNames = false;
  bool followSymlinks = false;
  bool orphanChildren = true;  // list contents of orphan folders
  NameFilter filter;
  DiffOptions rules;           // used with cmpRules
};
// Parses "size,time,crc,binary,rules" (comma / space / '+' separated). Returns false on unknown token.
bool parseCriteria(const std::string& s, DirCompareOptions& o);

struct DirSummary {
  size_t files = 0, folders = 0;
  size_t same = 0, different = 0, similar = 0, leftOnly = 0, rightOnly = 0, errors = 0, mismatch = 0;
  size_t newerLeft = 0, newerRight = 0;
  size_t foldersLeftOnly = 0, foldersRightOnly = 0, foldersDifferent = 0, foldersSame = 0;
};

struct DirResult {
  std::string left, right;
  std::vector<DirEntry> entries;
  DirSummary summary;
  bool identical() const;
};

bool compareDirs(const std::string& left, const std::string& right, const DirCompareOptions& opt, DirResult& out,
                 std::string* err);

// Display filter ("show" spec): all | diff | same | left | right | orphans | newer-left | newer-right | similar ...
struct ShowFilter {
  bool same = true, diff = true, similar = true, left = true, right = true, newerLeft = true, newerRight = true,
       sameTimeDiff = true, mismatch = true;
  bool hideEmptyDirs = false;
  bool parse(const std::string& s);  // comma separated tokens
  bool matches(const DirEntry& e) const;
};
std::vector<size_t> visibleEntries(const DirResult& r, const ShowFilter& f);

// ---- operations -----------------------------------------------------------------
enum class OpType { CopyLR, CopyRL, DeleteL, DeleteR, MkdirL, MkdirR };
struct Op {
  OpType type;
  std::string rel;
  std::string src, dst;  // full paths (dst empty for delete)
  bool isDir = false;
  uint64_t bytes = 0;
};
const char* opName(OpType t);

enum class SyncMode { UpdateLR, MirrorLR, UpdateRL, MirrorRL, UpdateBoth };
bool parseSyncMode(const std::string& s, SyncMode& out);

// Plan from the compare result. If onlySelected is set, only selected entries are considered.
std::vector<Op> planSync(const DirResult& r, SyncMode mode, bool createEmpty = true, bool onlySelected = false);
std::vector<Op> planCopySelected(const DirResult& r, bool toRight);
std::vector<Op> planDeleteSelected(const DirResult& r, bool leftSide, bool rightSide);
// Execute; returns number of failed operations. log receives one line per op.
int runOps(const std::vector<Op>& ops, bool dryRun, const std::function<void(const std::string&)>& log);

bool copyPathPreserve(const std::string& src, const std::string& dst, std::string* err);

}  // namespace bc

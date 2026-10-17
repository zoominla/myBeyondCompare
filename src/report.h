// Reports: text diff reports (several layouts) and folder reports.
#pragma once
#include <string>

#include "dircmp.h"
#include "diff.h"
#include "hexcmp.h"

namespace bc {

struct TextReportOptions {
  std::string style = "sidebyside";  // sidebyside | unified | context | normal | html | summary
  int context = 3;                   // context lines (unified/context/html/sidebyside with onlyDiffs)
  bool onlyDiffs = false;            // sidebyside/html: show only differences (+context)
  bool lineNumbers = true;
  int width = 0;                     // sidebyside column width, 0 = auto
  std::string leftTitle, rightTitle; // usually the paths
  bool showUnimportant = true;       // unified/context/normal: treat unimportant rows as changes (needed for exact patches)
};
bool validTextStyle(const std::string& s);
std::string textReport(const TextCompare& tc, const TextReportOptions& o);

struct FolderReportOptions {
  std::string style = "tree";  // tree | list | csv | html | xml | summary
  ShowFilter show;
  bool summary = true;         // append totals (tree/list/html)
  bool fullPaths = false;      // list style: prefix with root
};
bool validFolderStyle(const std::string& s);
std::string folderReport(const DirResult& r, const FolderReportOptions& o);

std::string folderSummaryText(const DirResult& r);

}  // namespace bc

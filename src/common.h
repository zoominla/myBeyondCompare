// mbc - Beyond Compare style compare tool (clean-room C++ reimplementation)
// Common utilities.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bc {
namespace fs = std::filesystem;

// ---- paths / strings -------------------------------------------------------
fs::path toPath(const std::string& utf8);
std::string fromPath(const fs::path& p);  // UTF-8, '/' separators normalised later by callers
std::string toLower(std::string s);
std::string trim(const std::string& s);
std::vector<std::string> splitAny(const std::string& s, const std::string& seps, bool keepEmpty = false);
bool startsWith(const std::string& s, const std::string& p);
bool endsWith(const std::string& s, const std::string& p);
std::string replaceAll(std::string s, const std::string& from, const std::string& to);
std::string joinPath(const std::string& a, const std::string& b);  // uses '/'
std::string htmlEscape(const std::string& s);
std::string xmlEscape(const std::string& s);
std::string csvEscape(const std::string& s);
size_t utf8Len(const std::string& s);  // code point count
std::vector<std::string> utf8Chars(const std::string& s);

// glob match with '*' and '?'; whole-string match
bool wildMatch(const std::string& pattern, const std::string& text, bool icase);

// ---- file helpers ------------------------------------------------------------
bool readFile(const std::string& path, std::string& out, std::string* err = nullptr);
bool writeFile(const std::string& path, const std::string& data, std::string* err = nullptr);
uint32_t crc32(const void* data, size_t n, uint32_t crc = 0);
bool fileCrc32(const std::string& path, uint32_t& crc, std::string* err = nullptr);
bool filesBinaryEqual(const std::string& a, const std::string& b, std::string* err = nullptr);

// seconds since unix epoch (UTC) of the file's modification time; 0 on failure
int64_t fileMTime(const fs::path& p);
bool setFileMTime(const fs::path& p, int64_t t);
std::string formatTime(int64_t t, bool utc = false);   // "YYYY-MM-DD HH:MM:SS"
std::string formatSize(uint64_t n);

// ---- exit codes (match BComp.com) ------------------------------------------------
enum ExitCode {
  EXIT_OK = 0,
  EXIT_BINARY_SAME = 1,
  EXIT_RULES_SAME = 2,
  EXIT_BINARY_DIFF = 11,
  EXIT_SIMILAR = 12,
  EXIT_RULES_DIFF = 13,
  EXIT_CONFLICTS = 14,
  EXIT_ERROR = 100,
  EXIT_CONFLICTS_UNSAVED = 101,
  EXIT_ERROR2 = 102
};
}  // namespace bc

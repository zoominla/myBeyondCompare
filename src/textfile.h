// Text file loading: encoding detection, line splitting (keeps EOL style).
#pragma once
#include <string>
#include <vector>

namespace bc {

enum class Enc { Auto, UTF8, UTF8BOM, UTF16LE, UTF16BE, ANSI };
enum class Eol { None, LF, CRLF, CR };

struct Line {
  std::string text;  // UTF-8 (or raw bytes for ANSI), without EOL
  Eol eol = Eol::None;
};

struct TextFile {
  Enc enc = Enc::UTF8;
  Eol dominantEol = Eol::LF;
  bool mixedEol = false;
  std::vector<Line> lines;
};

const char* encName(Enc e);
const char* eolName(Eol e);
const char* eolStr(Eol e);
bool parseEnc(const std::string& name, Enc& out);

// Heuristic: is this raw buffer a text file? (BC style: NULs / control chars => binary)
bool looksBinary(const std::string& raw);
Enc detectEncoding(const std::string& raw);

// Decode raw bytes into lines. Enc::Auto auto-detects.
TextFile parseText(const std::string& raw, Enc forced = Enc::Auto);
// Serialize back using the given encoding (default: file's own); eol used for lines with Eol::None
// when they are not last line.
std::string serializeText(const TextFile& tf, Enc enc, bool forceEol = false, Eol eol = Eol::LF);
std::string serializeText(const TextFile& tf);
bool loadTextFile(const std::string& path, TextFile& tf, std::string* err, Enc forced = Enc::Auto);

}  // namespace bc

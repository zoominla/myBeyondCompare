// Hex (binary) compare: byte-by-byte by position.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bc {

struct HexRange { uint64_t offset, length; };

struct HexResult {
  uint64_t sizeL = 0, sizeR = 0;
  uint64_t diffBytes = 0;  // differing bytes inside the common length
  std::vector<HexRange> ranges;  // differing runs (including the tail present on only one side)
  bool equal = true;
};

HexResult hexCompare(const std::string& a, const std::string& b);

struct HexDumpOptions {
  int bytesPerRow = 16;
  int contextRows = -1;   // <0 : show everything; otherwise only rows within N rows of a difference
  bool color = false;
  uint64_t maxRows = 0;   // 0 = unlimited
};
std::string hexDump(const std::string& a, const std::string& b, const HexDumpOptions& o = {});

}  // namespace bc

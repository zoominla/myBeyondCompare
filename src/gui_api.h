// JSON API of the local web GUI.
#pragma once
#include <string>

#include "json.h"

namespace bc {

struct GuiContext {
  std::string sessionsPath;
  Json start;  // optional initial view requested on the command line
};

// name examples: "text", "dir", "dir/op", "sessions/list". Never throws; errors are {"ok":false,"error":...}.
Json guiApi(const std::string& name, const Json& request, const GuiContext& ctx);

}  // namespace bc

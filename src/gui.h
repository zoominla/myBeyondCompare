// Local web GUI: `mbc gui` starts a loopback HTTP server that serves a single-page app and a JSON API
// built on the compare engine.
#pragma once
#include <string>

namespace bc {

struct GuiOptions {
  int port = 0;                 // 0 = pick a free port
  bool openBrowser = true;
  std::string sessionsPath;     // BCSessions.xml to read / write
  std::string startLeft, startRight, startView;  // optional: open a compare right away (view: dir|text|hex|merge)
  std::string startBase;
  std::string token;            // empty = random
};

int runGui(const GuiOptions& o);

// Used by tests / the embedded page generator.
const char* guiPageHtml();

}  // namespace bc

// BC style scripting (subset): load / filter / criteria / select / copyto / sync / delete / *-report / log / option ...
#pragma once
#include <iosfwd>
#include <string>
#include <vector>

#include "diff.h"
#include "dircmp.h"
#include "session.h"

namespace bc {

struct ScriptEnv {
  std::vector<std::string> args;  // %1 .. %9
  const SessionStore* sessions = nullptr;
  DiffOptions diff;
  DirCompareOptions dir;
  Enc lenc = Enc::Auto, renc = Enc::Auto;
  bool dryRun = false;
  std::ostream* out = nullptr;  // defaults to std::cout
  std::ostream* err = nullptr;  // defaults to std::cerr
  std::string baseDir;          // relative output paths are used as given (cwd); kept for tests
};

// Write report/text to a file ("-" or empty = stdout).
bool emitOutput(const std::string& path, const std::string& data, std::ostream& out, std::string* err);

// Runs a script. Returns 0 on success, 100 on error (message on env.err).
int runScript(const std::string& text, ScriptEnv& env);

// Splits a script line into tokens: whitespace separated, double quotes group, key:"a b" -> key:a b.
std::vector<std::string> scriptTokens(const std::string& line);

}  // namespace bc

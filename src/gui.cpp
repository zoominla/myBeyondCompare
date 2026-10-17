#include "gui.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <random>

#include "common.h"
#include "gui_api.h"
#include "httpd.h"
#include "json.h"
#include "session.h"

namespace bc {

static std::string randomToken() {
  std::random_device rd;
  std::mt19937_64 g(((uint64_t)rd() << 32) ^ rd() ^ (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count());
  static const char* hx = "0123456789abcdef";
  std::string t;
  for (int i = 0; i < 32; i++) t += hx[g() & 15];
  return t;
}

static void openBrowser(const std::string& url) {
#ifdef _WIN32
  std::string cmd = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
  std::string cmd = "open \"" + url + "\" >/dev/null 2>&1 &";
#else
  std::string cmd = "(xdg-open \"" + url + "\" >/dev/null 2>&1 &)";
#endif
  if (std::system(cmd.c_str()) != 0) std::cerr << "mbc: could not launch a browser, open the URL manually\n";
}

int runGui(const GuiOptions& o) {
  HttpServer srv;
  std::string err;
  if (!srv.listen(o.port, &err)) {
    std::cerr << "mbc: " << err << "\n";
    return EXIT_ERROR;
  }
  std::string token = o.token.empty() ? randomToken() : o.token;
  std::string createdMsg;
  // Make sure the sessions file exists: read when present, otherwise create an empty one.
  if (!o.sessionsPath.empty()) {
    std::error_code ec;
    fs::path sp = toPath(o.sessionsPath);
    if (!fs::exists(sp, ec)) {
      if (sp.has_parent_path()) fs::create_directories(sp.parent_path(), ec);
      std::string werr;
      if (SessionStore().save(o.sessionsPath, &werr)) createdMsg = "sessions file: " + o.sessionsPath + " (created)";
      else std::cerr << "mbc: warning: cannot create sessions file '" << o.sessionsPath << "': " << werr << "\n";
    } else {
      SessionStore probe;
      std::string rerr;
      if (!probe.load(o.sessionsPath, &rerr)) std::cerr << "mbc: warning: cannot read sessions file '" << o.sessionsPath << "': " << rerr << "\n";
    }
  }
  GuiContext ctx;
  ctx.sessionsPath = o.sessionsPath;
  ctx.start = Json::object();
  if (!o.startView.empty() || !o.startLeft.empty()) {
    ctx.start["view"] = o.startView;
    ctx.start["left"] = o.startLeft;
    ctx.start["right"] = o.startRight;
    ctx.start["base"] = o.startBase;
  }
  const int port = srv.port();
  const std::string hostA = "127.0.0.1:" + std::to_string(port), hostB = "localhost:" + std::to_string(port);
  std::string page = guiPageHtml();
  {
    size_t p;
    while ((p = page.find("__MBC_TOKEN__")) != std::string::npos) page.replace(p, 13, token);
  }
  std::mutex apiLock;

  std::string url = "http://" + hostA + "/?token=" + token;
  std::cout << "mbc gui listening on " << url << std::endl;
  if (!createdMsg.empty()) std::cout << createdMsg << std::endl;
  if (o.openBrowser) openBrowser(url);

  srv.serve([&](const HttpRequest& rq) -> HttpResponse {
    HttpResponse rs;
    std::string host = rq.header("host");
    if (host != hostA && host != hostB) {
      rs.status = 403;
      rs.body = "forbidden host";
      return rs;
    }
    if (rq.method == "GET" && rq.path == "/") {
      if (rq.query.find("token=" + token) == std::string::npos) {
        rs.status = 403;
        rs.body = "missing or wrong token - open the URL printed by `mbc gui`";
        return rs;
      }
      rs.contentType = "text/html; charset=utf-8";
      rs.body = page;
      return rs;
    }
    if (rq.method == "GET" && rq.path == "/favicon.ico") {
      rs.status = 204;
      return rs;
    }
    if (rq.method == "POST" && rq.path.rfind("/api/", 0) == 0) {
      if (rq.header("x-mbc-token") != token) {
        rs.status = 403;
        rs.body = "bad token";
        return rs;
      }
      std::string name = rq.path.substr(5);
      Json req = Json::object(), out;
      std::string perr;
      if (!rq.body.empty() && !Json::parse(rq.body, req, &perr)) {
        out = Json::object();
        out["ok"] = false;
        out["error"] = "bad JSON: " + perr;
      } else if (name == "quit") {
        out = Json::object();
        out["ok"] = true;
        srv.stop();
      } else {
        std::lock_guard<std::mutex> lk(apiLock);
        out = guiApi(name, req, ctx);
      }
      rs.contentType = "application/json; charset=utf-8";
      rs.body = out.dump();
      return rs;
    }
    rs.status = 404;
    rs.body = "not found";
    return rs;
  });
  return 0;
}

}  // namespace bc

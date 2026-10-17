// Minimal blocking HTTP/1.1 server (loopback only), one thread per connection.
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <string>

namespace bc {

struct HttpRequest {
  std::string method, path, query;
  std::map<std::string, std::string> headers;  // lower-case names
  std::string body;
  std::string header(const std::string& k) const {
    auto it = headers.find(k);
    return it == headers.end() ? std::string() : it->second;
  }
};

struct HttpResponse {
  int status = 200;
  std::string contentType = "text/plain; charset=utf-8";
  std::string body;
  std::map<std::string, std::string> headers;
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

class HttpServer {
 public:
  HttpServer();
  ~HttpServer();
  // port 0 = pick a free port. Binds 127.0.0.1 only.
  bool listen(int port, std::string* err);
  int port() const { return port_; }
  // Blocks until stop() is called.
  void serve(HttpHandler h);
  void stop() { stop_ = true; }
  size_t maxBody = 512u * 1024 * 1024;

 private:
  void handle(long long sock, const HttpHandler& h);
  long long fd_ = -1;
  int port_ = 0;
  std::atomic<bool> stop_{false};
};

}  // namespace bc

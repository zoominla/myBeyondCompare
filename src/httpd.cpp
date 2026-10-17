#include "httpd.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET sock_t;
static void closeSock(sock_t s) { closesocket(s); }
static const sock_t kBad = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
static void closeSock(sock_t s) { ::close(s); }
static const sock_t kBad = -1;
#endif

namespace bc {

HttpServer::HttpServer() {
#ifdef _WIN32
  WSADATA d;
  WSAStartup(MAKEWORD(2, 2), &d);
#else
  signal(SIGPIPE, SIG_IGN);
#endif
}
HttpServer::~HttpServer() {
  if (fd_ != -1) closeSock((sock_t)fd_);
}

bool HttpServer::listen(int port, std::string* err) {
  sock_t s = ::socket(AF_INET, SOCK_STREAM, 0);
  if (s == kBad) {
    if (err) *err = "socket() failed";
    return false;
  }
  int one = 1;
#ifndef _WIN32
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
#endif
  sockaddr_in a;
  std::memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons((unsigned short)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(s, (sockaddr*)&a, sizeof a) != 0 || ::listen(s, 64) != 0) {
    if (err) *err = "cannot listen on 127.0.0.1:" + std::to_string(port) + " (port in use?)";
    closeSock(s);
    return false;
  }
  socklen_t len = sizeof a;
  getsockname(s, (sockaddr*)&a, &len);
  port_ = ntohs(a.sin_port);
  fd_ = (long long)s;
  return true;
}

static bool sendAll(sock_t s, const char* p, size_t n) {
  while (n) {
    int k = ::send(s, p, (int)(n > (1u << 20) ? (1u << 20) : n), 0);
    if (k <= 0) return false;
    p += k;
    n -= (size_t)k;
  }
  return true;
}

static std::string lower(std::string s) {
  for (auto& c : s)
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return s;
}

static std::string urlDecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
      o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16);
      i += 2;
    } else if (s[i] == '+') o += ' ';
    else o += s[i];
  }
  return o;
}

void HttpServer::handle(long long sock, const HttpHandler& h) {
  sock_t s = (sock_t)sock;
#ifdef _WIN32
  DWORD tv = 60000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
#else
  timeval tv{60, 0};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
#endif
  std::string buf;
  char tmp[65536];
  size_t hdrEnd = std::string::npos;
  while (hdrEnd == std::string::npos) {
    int k = ::recv(s, tmp, sizeof tmp, 0);
    if (k <= 0) { closeSock(s); return; }  // idle / speculative connection
    buf.append(tmp, k);
    hdrEnd = buf.find("\r\n\r\n");
    if (hdrEnd == std::string::npos && buf.size() > 65536) { closeSock(s); return; }
  }
  HttpRequest rq;
  HttpResponse rs;
  bool bad = false;
  {
    size_t le = buf.find("\r\n");
    std::string line = buf.substr(0, le);
    size_t a = line.find(' '), b = line.rfind(' ');
    if (a == std::string::npos || b == a) bad = true;
    else {
      rq.method = line.substr(0, a);
      std::string target = line.substr(a + 1, b - a - 1);
      size_t q = target.find('?');
      rq.path = urlDecode(q == std::string::npos ? target : target.substr(0, q));
      if (q != std::string::npos) rq.query = target.substr(q + 1);
    }
    size_t p = le + 2;
    while (!bad && p < hdrEnd) {
      size_t e = buf.find("\r\n", p);
      if (e == std::string::npos || e > hdrEnd) e = hdrEnd;
      std::string l = buf.substr(p, e - p);
      size_t c = l.find(':');
      if (c != std::string::npos) {
        std::string v = l.substr(c + 1);
        size_t f = v.find_first_not_of(" \t");
        v = f == std::string::npos ? "" : v.substr(f);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
        rq.headers[lower(l.substr(0, c))] = v;
      }
      p = e + 2;
    }
  }
  if (!bad) {
    size_t clen = 0;
    auto it = rq.headers.find("content-length");
    if (it != rq.headers.end()) clen = (size_t)std::strtoull(it->second.c_str(), nullptr, 10);
    if (clen > maxBody) bad = true;
    else {
      rq.body = buf.substr(hdrEnd + 4);
      while (rq.body.size() < clen) {
        int k = ::recv(s, tmp, sizeof tmp, 0);
        if (k <= 0) { bad = true; break; }
        rq.body.append(tmp, k);
      }
      if (rq.body.size() > clen) rq.body.resize(clen);
    }
  }
  if (bad) {
    rs.status = 400;
    rs.body = "bad request";
  } else {
    try {
      rs = h(rq);
    } catch (const std::exception& ex) {
      rs.status = 500;
      rs.body = std::string("internal error: ") + ex.what();
    } catch (...) {
      rs.status = 500;
      rs.body = "internal error";
    }
  }
  const char* reason = rs.status == 200 ? "OK" : rs.status == 204 ? "No Content" : rs.status == 400 ? "Bad Request"
                       : rs.status == 403 ? "Forbidden" : rs.status == 404 ? "Not Found" : "Error";
  std::string head = "HTTP/1.1 " + std::to_string(rs.status) + " " + reason + "\r\nContent-Type: " + rs.contentType +
                     "\r\nContent-Length: " + std::to_string(rs.body.size()) +
                     "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n";
  for (auto& kv : rs.headers) head += kv.first + ": " + kv.second + "\r\n";
  head += "\r\n";
  if (sendAll(s, head.data(), head.size())) sendAll(s, rs.body.data(), rs.body.size());
#ifdef _WIN32
  shutdown(s, SD_SEND);
#else
  shutdown(s, SHUT_WR);
#endif
  closeSock(s);
}

void HttpServer::serve(HttpHandler h) {
  sock_t ls = (sock_t)fd_;
  while (!stop_) {
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(ls, &rf);
    timeval tv{0, 200000};
    int r = ::select((int)ls + 1, &rf, nullptr, nullptr, &tv);
    if (r <= 0) continue;
    sock_t c = ::accept(ls, nullptr, nullptr);
    if (c == kBad) continue;
    std::thread([this, c, h]() { handle((long long)c, h); }).detach();
  }
  // give in-flight responses (e.g. the one that requested shutdown) a moment to flush
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

}  // namespace bc

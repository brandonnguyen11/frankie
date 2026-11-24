#include "net/NetClient.h"
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSESOCK close
#endif

namespace og {

static bool parseAddress(std::string a, std::string& host, int& port) {
  const std::string scheme = "opera://";
  if (a.rfind(scheme, 0) == 0) a = a.substr(scheme.size());
  while (!a.empty() && (a.back() == '/' || a.back() == ' ')) a.pop_back();
  while (!a.empty() && a.front() == ' ') a.erase(a.begin());
  port = 7777;
  auto c = a.rfind(':');
  if (c != std::string::npos) {
    try { port = std::stoi(a.substr(c + 1)); } catch (...) { return false; }
    a = a.substr(0, c);
  }
  host = a.empty() ? "127.0.0.1" : a;
  return port > 0 && port < 65536;
}

bool NetClient::connect(const std::string& address, const std::string& name, std::string& error) {
  disconnect();
  std::string host;
  int port;
  if (!parseAddress(address, host, port)) { error = "bad room address"; return false; }
#ifdef _WIN32
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
    error = "cannot resolve " + host;
    return false;
  }
  sock_ = (int)::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock_ < 0) { freeaddrinfo(res); error = "socket() failed"; return false; }
  configureSocket(sock_);

  // non-blocking connect with a 3 s timeout so the UI never hangs
#ifndef _WIN32
  int flags = fcntl(sock_, F_GETFL, 0);
  fcntl(sock_, F_SETFL, flags | O_NONBLOCK);
#endif
  int rc = ::connect(sock_, res->ai_addr, (socklen_t)res->ai_addrlen);
  freeaddrinfo(res);
  bool ok = rc == 0;
#ifndef _WIN32
  if (!ok && errno == EINPROGRESS) {
    fd_set w;
    FD_ZERO(&w);
    FD_SET(sock_, &w);
    timeval tv{3, 0};
    if (select(sock_ + 1, nullptr, &w, nullptr, &tv) == 1) {
      int err = 0;
      socklen_t len = sizeof(err);
      getsockopt(sock_, SOL_SOCKET, SO_ERROR, &err, &len);
      ok = err == 0;
    }
  }
  fcntl(sock_, F_SETFL, flags);
#endif
  if (!ok) {
    CLOSESOCK(sock_);
    sock_ = -1;
    error = "no room server at " + host + ":" + std::to_string(port);
    return false;
  }
  address_ = host + ":" + std::to_string(port);
  connected_ = true;
  playerId_ = 0;
  send(makePacket(MsgType::Join, 0, name.data(), name.size()));
  reader_ = std::thread([this] { readerLoop(); });
  return true;
}

void NetClient::disconnect() {
  connected_ = false;
  if (sock_ >= 0) {
#ifndef _WIN32
    shutdown(sock_, SHUT_RDWR);
#endif
    CLOSESOCK(sock_);
    sock_ = -1;
  }
  if (reader_.joinable()) reader_.join();
  std::lock_guard<std::mutex> l(inMutex_);
  stream_ = PacketStream{};
  playerId_ = 0;
}

void NetClient::send(const std::string& packet) {
  if (!connected_ || sock_ < 0) return;
  std::lock_guard<std::mutex> l(sendMutex_);
  if (!sendAll(sock_, packet)) connected_ = false;
}

void NetClient::readerLoop() {
  char buf[16384];
  while (connected_) {
    long n = ::recv(sock_, buf, sizeof(buf), 0);
    if (n <= 0) { connected_ = false; break; }
    std::lock_guard<std::mutex> l(inMutex_);
    stream_.feed(buf, (size_t)n);
  }
}

void NetClient::poll(const Handler& handler) {
  std::vector<std::pair<PacketHeader, std::string>> msgs;
  {
    std::lock_guard<std::mutex> l(inMutex_);
    PacketHeader h;
    std::string p;
    bool bad = false;
    while (stream_.next(h, p, bad)) msgs.emplace_back(h, std::move(p));
    if (bad) {
      std::cerr << "Net: corrupt stream, disconnecting\n";
      connected_ = false;
    }
  }
  for (auto& [h, p] : msgs) {
    if (h.type == MsgType::Welcome && p.size() >= 4) {
      uint32_t id;
      std::memcpy(&id, p.data(), 4);
      playerId_ = id;
    }
    handler(h, p);
  }
}

}  // namespace og

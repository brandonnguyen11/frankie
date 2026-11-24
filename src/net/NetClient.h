#pragma once
#include "net/Protocol.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace og {

class NetClient {
 public:
  using Handler = std::function<void(const PacketHeader&, const std::string& payload)>;
  ~NetClient() { disconnect(); }

  // `address` is "host:port", "host" (port 7777) or an opera://host:port link.
  bool connect(const std::string& address, const std::string& name, std::string& error);
  void disconnect();
  void send(const std::string& packet);
  void poll(const Handler& handler);
  bool connected() const { return connected_; }
  uint32_t playerId() const { return playerId_; }
  const std::string& address() const { return address_; }

 private:
  int sock_ = -1;
  std::atomic<bool> connected_{false};
  std::atomic<uint32_t> playerId_{0};
  std::string address_;
  std::mutex inMutex_, sendMutex_;
  PacketStream stream_;
  std::thread reader_;
  void readerLoop();
};

}  // namespace og

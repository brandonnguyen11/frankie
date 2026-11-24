#include "net/Protocol.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

namespace og {

std::string makePacket(MsgType type, uint32_t id, const void* payload, size_t len) {
  PacketHeader h{type, (uint32_t)(sizeof(PacketHeader) + len), id};
  std::string out(h.size, '\0');
  std::memcpy(out.data(), &h, sizeof(h));
  if (len) std::memcpy(out.data() + sizeof(h), payload, len);
  return out;
}

bool PacketStream::next(PacketHeader& h, std::string& payload, bool& bad) {
  bad = false;
  size_t avail = buf_.size() - off_;
  if (avail < sizeof(PacketHeader)) {
    if (off_ > 0) { buf_.erase(buf_.begin(), buf_.begin() + off_); off_ = 0; }
    return false;
  }
  std::memcpy(&h, buf_.data() + off_, sizeof(h));
  if (h.size < sizeof(PacketHeader) || h.size > kMaxPacket) {
    bad = true;
    return false;
  }
  if (avail < h.size) {
    if (off_ > 0) { buf_.erase(buf_.begin(), buf_.begin() + off_); off_ = 0; }
    return false;
  }
  payload.assign(buf_.data() + off_ + sizeof(h), h.size - sizeof(h));
  off_ += h.size;
  return true;
}

void configureSocket(int fd) {
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
#ifdef SO_NOSIGPIPE
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (const char*)&one, sizeof(one));
#endif
}

bool sendAll(int fd, const std::string& data) {
  size_t off = 0;
#ifdef MSG_NOSIGNAL
  const int flags = MSG_NOSIGNAL;
#else
  const int flags = 0;
#endif
  while (off < data.size()) {
    long n = ::send(fd, data.data() + off, (int)(data.size() - off), flags);
    if (n <= 0) return false;
    off += (size_t)n;
  }
  return true;
}

}  // namespace og

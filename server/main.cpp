// Opéra Grenouille room server.
// One room per process. Relays notes, poses, voice/song audio and rig edits,
// and keeps the latest shared state so late joiners get everything (M3/L6):
// lighting rig, desk owner, recording flag, every player's last pose, and the
// currently playing song.
#include "net/Protocol.h"
#include <algorithm>
#include <atomic>
#include <csignal>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSESOCK close
#endif

using namespace og;

struct Client {
  int fd = -1;
  uint32_t id = 0;
  std::string name;
  std::string lastPose;  // full packet
  bool joined = false;
  std::mutex sendMutex;
};

static std::mutex gMutex;
static std::vector<std::shared_ptr<Client>> gClients;
static std::string gRigPacket;
static std::string gDeskPacket;
static std::string gSongPacket;
static std::map<uint32_t, std::string> gRecFlags;  // recorder id -> packet
static std::atomic<uint32_t> gNextId{1};
static const size_t kMaxPlayers = 8;

static void sendTo(const std::shared_ptr<Client>& c, const std::string& pkt) {
  std::lock_guard<std::mutex> l(c->sendMutex);
  sendAll(c->fd, pkt);
}

static void broadcast(const std::string& pkt, const Client* except) {
  std::vector<std::shared_ptr<Client>> targets;
  {
    std::lock_guard<std::mutex> l(gMutex);
    for (auto& c : gClients)
      if (c.get() != except && c->joined) targets.push_back(c);
  }
  for (auto& c : targets) sendTo(c, pkt);
}

static void handleClient(std::shared_ptr<Client> me) {
  PacketStream stream;
  char buf[16384];
  while (true) {
    long n = ::recv(me->fd, buf, sizeof(buf), 0);
    if (n <= 0) break;
    stream.feed(buf, (size_t)n);
    PacketHeader h;
    std::string p;
    bool bad = false;
    while (stream.next(h, p, bad)) {
      if (h.type == MsgType::Join) {
        if (me->joined) continue;
        me->id = gNextId++;
        me->name = p.substr(0, 24);
        std::vector<std::string> catchUp;
        {
          std::lock_guard<std::mutex> l(gMutex);
          if (!gRigPacket.empty()) catchUp.push_back(gRigPacket);
          if (!gDeskPacket.empty()) catchUp.push_back(gDeskPacket);
          if (!gSongPacket.empty()) catchUp.push_back(gSongPacket);
          for (auto& [id, pkt] : gRecFlags) catchUp.push_back(pkt);
          for (auto& c : gClients)
            if (c != me && c->joined && !c->lastPose.empty()) catchUp.push_back(c->lastPose);
        }
        uint32_t id = me->id;
        sendTo(me, makePacket(MsgType::Welcome, id, &id, 4));
        for (auto& pkt : catchUp) sendTo(me, pkt);
        me->joined = true;
        std::cout << "+ " << me->name << " joined as #" << id << "\n";
        continue;
      }
      if (!me->joined) continue;
      // stamp the authoritative sender id
      h.playerId = me->id;
      std::string pkt = makePacket(h.type, me->id, p.data(), p.size());
      switch (h.type) {
        case MsgType::Pose:
          me->lastPose = pkt;
          broadcast(pkt, me.get());
          break;
        case MsgType::Rig: {
          std::lock_guard<std::mutex> l(gMutex);
          gRigPacket = pkt;
        }
          broadcast(pkt, me.get());
          break;
        case MsgType::Desk: {
          std::lock_guard<std::mutex> l(gMutex);
          gDeskPacket = pkt;
        }
          broadcast(pkt, me.get());
          break;
        case MsgType::Song: {
          std::lock_guard<std::mutex> l(gMutex);
          gSongPacket = (!p.empty() && p[0]) ? pkt : std::string();
        }
          broadcast(pkt, me.get());
          break;
        case MsgType::RecFlag: {
          std::lock_guard<std::mutex> l(gMutex);
          if (!p.empty() && p[0]) gRecFlags[me->id] = pkt;
          else gRecFlags.erase(me->id);
        }
          broadcast(pkt, me.get());
          break;
        case MsgType::Note:
        case MsgType::Audio:
          broadcast(pkt, me.get());
          break;
        default: break;
      }
    }
    if (bad) break;
  }

  uint32_t id = me->id;
  bool releaseDesk = false;
  {
    std::lock_guard<std::mutex> l(gMutex);
    gClients.erase(std::remove(gClients.begin(), gClients.end(), me), gClients.end());
    gRecFlags.erase(id);
    if (gDeskPacket.size() >= sizeof(PacketHeader) + 4) {
      int32_t owner;
      std::memcpy(&owner, gDeskPacket.data() + sizeof(PacketHeader), 4);
      if ((uint32_t)owner == id) releaseDesk = true;
    }
    if (releaseDesk) {
      int32_t none = -1;
      gDeskPacket = makePacket(MsgType::Desk, 0, &none, 4);
    }
  }
  CLOSESOCK(me->fd);
  if (me->joined) {
    broadcast(makePacket(MsgType::Leave, id, nullptr, 0), nullptr);
    uint8_t off = 0;
    broadcast(makePacket(MsgType::RecFlag, id, &off, 1), nullptr);
    if (releaseDesk) {
      int32_t none = -1;
      broadcast(makePacket(MsgType::Desk, 0, &none, 4), nullptr);
    }
    std::cout << "- " << me->name << " (#" << id << ") left\n";
  }
}

static void printAddresses(int port) {
#ifndef _WIN32
  ifaddrs* ifs = nullptr;
  if (getifaddrs(&ifs) != 0) return;
  for (ifaddrs* i = ifs; i; i = i->ifa_next) {
    if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((sockaddr_in*)i->ifa_addr)->sin_addr, ip, sizeof(ip));
    if (std::string(ip) == "127.0.0.1") continue;
    std::cout << "  room code: " << ip << ":" << port << "   (link: opera://" << ip << ":" << port << ")\n";
  }
  freeifaddrs(ifs);
#endif
}

int main(int argc, char** argv) {
  std::cout << std::unitbuf;  // log lines appear immediately
  int port = argc > 1 ? std::atoi(argv[1]) : 7777;
#ifdef _WIN32
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#else
  std::signal(SIGPIPE, SIG_IGN);
#endif
  int srv = (int)::socket(AF_INET, SOCK_STREAM, 0);
  int yes = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons((uint16_t)port);
  if (bind(srv, (sockaddr*)&addr, sizeof(addr)) < 0) {
    std::cerr << "bind failed on port " << port << "\n";
    return 1;
  }
  listen(srv, 16);
  std::cout << "Opéra Grenouille room server on port " << port << "\n";
  std::cout << "  room code: 127.0.0.1:" << port << " (this machine)\n";
  printAddresses(port);

  while (true) {
    sockaddr_in cli{};
    socklen_t len = sizeof(cli);
    int fd = (int)::accept(srv, (sockaddr*)&cli, &len);
    if (fd < 0) continue;
    configureSocket(fd);
    auto c = std::make_shared<Client>();
    c->fd = fd;
    {
      std::lock_guard<std::mutex> l(gMutex);
      if (gClients.size() >= kMaxPlayers) {
        CLOSESOCK(fd);
        std::cout << "room full, rejected a connection\n";
        continue;
      }
      gClients.push_back(c);
    }
    std::thread(handleClient, c).detach();
  }
}

#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace og {

// Wire protocol: every packet = PacketHeader + payload, little-endian, over TCP.
enum class MsgType : uint8_t {
  Join = 1,         // C->S  payload: name (utf8)
  Welcome = 2,      // S->C  payload: u32 assigned id
  Leave = 3,        // S->C  header.playerId left the room
  Pose = 4,         // C->S->C  PlayerState
  Note = 5,         // C->S->C  NotePayload
  Rig = 6,          // C->S->C  lighting rig text blob (server keeps the latest)
  RecFlag = 7,      // C->S->C  u8 recording on/off
  Audio = 8,        // C->S->C  AudioHeader + int16 samples (24 kHz mono)
  Desk = 9,         // C->S->C  i32 lighting-desk owner id (-1 = free)
  Song = 10,        // C->S->C  SongPayload: loaded song started/stopped
};

#pragma pack(push, 1)
struct PacketHeader {
  MsgType type;
  uint32_t size;      // total bytes including this header
  uint32_t playerId;  // sender (stamped by the server)
};

struct PlayerState {
  float x = 0, y = 0, z = 0, yaw = 0;
  uint8_t role = 4;
  int8_t seat = -1;
  uint8_t sitting = 0;
  uint8_t playing = 0;
  float mouth = 0;
  uint8_t instrument = 0;
  uint8_t suit = 0;
  uint8_t inHouse = 0;  // 0 plaza, 1 auditorium
  char name[24] = {};
};

struct NotePayload {
  uint8_t instrument, note, velocity, on;
  int8_t seat;  // orchestra seat that plays it (-1 = the sender's frog)
};

struct AudioHeader {
  uint8_t kind;  // StreamKind
  uint32_t seq;
};

struct SongPayload {
  uint8_t playing;
  uint8_t isMidi;
  char title[48];
};
#pragma pack(pop)

constexpr size_t kMaxPacket = 1 << 20;

std::string makePacket(MsgType type, uint32_t id, const void* payload, size_t len);
template <typename T>
std::string makePacket(MsgType type, uint32_t id, const T& payload) {
  return makePacket(type, id, &payload, sizeof(T));
}

// Accumulates a TCP byte stream and yields complete packets.
class PacketStream {
 public:
  void feed(const char* data, size_t n) { buf_.insert(buf_.end(), data, data + n); }
  // Returns false when no complete packet is buffered; sets `bad` on a corrupt stream.
  bool next(PacketHeader& h, std::string& payload, bool& bad);

 private:
  std::vector<char> buf_;
  size_t off_ = 0;
};

void configureSocket(int fd);  // TCP_NODELAY + no SIGPIPE
bool sendAll(int fd, const std::string& data);

}  // namespace og

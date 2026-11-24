#include "instruments/Orchestra.h"
#include <algorithm>

namespace og {

const std::vector<OrchestraSeat>& orchestraLayout() {
  static std::vector<OrchestraSeat> layout = [] {
    std::vector<OrchestraSeat> v;
    const InstrumentId front[6] = {InstrumentId::Piano, InstrumentId::Violin, InstrumentId::Violin,
                                   InstrumentId::Flute, InstrumentId::Flute, InstrumentId::Trumpet};
    const InstrumentId back[6] = {InstrumentId::Cello, InstrumentId::Cello, InstrumentId::Violin,
                                  InstrumentId::Violin, InstrumentId::Trumpet, InstrumentId::Timpani};
    for (int i = 0; i < 12; ++i) {
      OrchestraSeat s;
      s.index = i;
      int row = i / 6, col = i % 6;
      s.position = {(col - 2.5f) * 2.3f, -0.5f, 0.6f + row * 1.6f};
      if (i == 0) s.position.x = -7.7f;  // piano sits at the pit's edge so its body clears the cellos
      s.yaw = 0.f;
      s.instrument = row == 0 ? front[col] : back[col];
      s.label = std::string(instrumentName(s.instrument)) + " " + std::to_string(i + 1);
      v.push_back(s);
    }
    return v;
  }();
  return layout;
}

void Orchestra::buildDefault() { seats_ = orchestraLayout(); }

bool Orchestra::claim(int seatIndex, int playerId) {
  if (seatIndex < 0 || seatIndex >= (int)seats_.size()) return false;
  auto& s = seats_[seatIndex];
  if (s.claimedBy != -1 && s.claimedBy != playerId) return false;
  release(playerId);
  s.claimedBy = playerId;
  return true;
}

void Orchestra::release(int playerId) {
  for (auto& s : seats_)
    if (s.claimedBy == playerId) s.claimedBy = -1;
}

void Orchestra::releaseAllExcept(const std::vector<int>& live) {
  for (auto& s : seats_)
    if (s.claimedBy != -1 && std::find(live.begin(), live.end(), s.claimedBy) == live.end()) s.claimedBy = -1;
}

int Orchestra::seatOf(int playerId) const {
  for (auto& s : seats_)
    if (s.claimedBy == playerId) return s.index;
  return -1;
}

int Orchestra::nearestFree(const glm::vec3& p, float maxDist, int playerId) const {
  int best = -1;
  float bestD = maxDist;
  for (auto& s : seats_) {
    if (s.claimedBy != -1 && s.claimedBy != playerId) continue;
    float d = glm::distance(glm::vec2(p.x, p.z), glm::vec2(s.position.x, s.position.z));
    if (d < bestD) { bestD = d; best = s.index; }
  }
  return best;
}

}  // namespace og

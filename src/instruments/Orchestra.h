#pragma once
#include "audio/AudioEngine.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace og {

struct OrchestraSeat {
  int index = 0;
  glm::vec3 position{0.f};  // where the frog's root sits (pit floor y=-0.5)
  float yaw = 0.f;          // musicians face the conductor / house (+Z)
  InstrumentId instrument = InstrumentId::Piano;
  int claimedBy = -1;       // player id, -1 free
  int midiTrack = -1;       // loaded-song track assigned to this seat (S1)
  std::string label;
};

// Fixed pit layout shared by the scene builder (chairs, stands, piano) and gameplay.
const std::vector<OrchestraSeat>& orchestraLayout();

class Orchestra {
 public:
  void buildDefault();
  bool claim(int seatIndex, int playerId);
  void release(int playerId);
  void releaseAllExcept(const std::vector<int>& livePlayers);
  int seatOf(int playerId) const;
  int nearestFree(const glm::vec3& p, float maxDist, int playerId) const;
  const std::vector<OrchestraSeat>& seats() const { return seats_; }
  std::vector<OrchestraSeat>& seats() { return seats_; }

 private:
  std::vector<OrchestraSeat> seats_;
};

}  // namespace og

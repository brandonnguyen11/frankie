#pragma once
#include "audio/AudioEngine.h"
#include "scene/Primitives.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace og {

enum class Role { Musician = 0, Singer, LightingDesigner, CameraOperator, Audience, Count };
const char* roleName(Role r);

// A suited frog performer built as a scene-graph hierarchy
// (root -> hips -> torso -> head/jaw, shoulders -> upper arm -> forearm -> hand,
//  hips -> thigh -> shin -> foot). Poses are procedural and driven by state
// that is either local input or replicated over the network.
struct FrogActor {
  uint32_t id = 0;
  std::string name = "Frog";
  bool local = false;
  bool npc = false;
  bool inHouse = false;  // replicated zone (remote frogs)

  // replicated state
  glm::vec3 position{0.f};
  float yaw = 180.f;  // degrees; 0 faces +Z
  bool sitting = false;
  bool playing = false;
  float mouthOpen = 0.f;  // 0..1, singers drive this from voice level
  Role role = Role::Audience;
  int seatIndex = -1;
  InstrumentId instrument = InstrumentId::Piano;
  glm::vec3 suitColor{0.06f, 0.07f, 0.12f};
  glm::vec3 skinColor{0.24f, 0.52f, 0.16f};

  // animation
  float anim = 0.f;
  float walkPhase = 0.f;
  float speed = 0.f;           // m/s, smoothed
  float noteImpulse = 0.f;     // spikes on each note, decays
  int noteCount = 0;           // alternates hands / bow direction
  float sitBlend = 0.f;        // 0 stand .. 1 sit
  float songLevel = 0.f;       // loudness envelope for audio-file playback (S3)

  // network smoothing for remote frogs
  glm::vec3 netPosition{0.f};
  float netYaw = 0.f;
  double lastHeard = 0.0;

  void onNote(float velocity);
  void update(float dt);
  void appendDraw(std::vector<SceneObject>& out) const;
};

}  // namespace og

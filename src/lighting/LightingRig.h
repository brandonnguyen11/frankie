#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace og {

// A fixed rigging position (truss pipe, balcony rail, boom, booth).
struct MountPoint {
  std::string name;
  glm::vec3 position;
};

enum class Gobo : int { None = 0, Breakup = 1, Window = 2, Stars = 3, Count };
const char* goboName(Gobo g);

struct SpotLight {
  std::string name = "Spot";
  int mount = 0;               // index into LightingRig::mounts
  glm::vec3 position{0, 10, 6};  // derived from mount (kept for serialisation/network)
  glm::vec3 direction{0, -1, 0};
  bool useKelvin = true;
  float kelvin = 3200.f;
  glm::vec3 rgb{1.f};
  float intensityKcd = 150.f;  // luminous intensity, kilocandela
  float beamAngle = 22.f;      // full cone, degrees
  float softness = 0.35f;      // 0 = hard edge, 1 = fully soft
  bool castsShadow = true;
  bool enabled = true;
  int followTarget = -1;       // performer index; -1 = manual aim
  Gobo gobo = Gobo::None;

  glm::vec3 color() const;     // linear RGB, unit luminance
  float cosOuter() const;
  float cosInner() const;
  float range() const;         // distance where contribution is negligible
  void aimAt(const glm::vec3& p);
};

struct RigState {
  std::vector<SpotLight> lights;
  float haze = 0.30f;
  float houseLights = 0.10f;
  float chandelier = 0.35f;
};

struct LightCue {
  std::string name;
  RigState state;
  float fadeSeconds = 3.f;
};

struct LightingRig {
  static constexpr int kMaxLights = 12;
  static constexpr int kMaxShadowed = 8;

  std::vector<MountPoint> mounts;
  std::vector<SpotLight> lights;
  float haze = 0.30f;        // 0..1 stage haze density
  float houseLights = 0.10f; // 0..1 house dimmer
  float chandelier = 0.35f;  // 0..1 chandelier dimmer
  int selected = 0;
  uint32_t revision = 1;     // bumped on every edit so the network layer knows to send

  // Cues (P1 L8)
  std::vector<LightCue> cues;
  int activeCue = -1;

  void buildMounts();
  void applyDefaultAgent327();
  void addLight();
  void removeSelected();
  void setMount(SpotLight& L, int mount);
  void updateFollowspots(const std::vector<glm::vec3>& targets);
  void update(float dt);
  void markDirty() { ++revision; }

  void recordCue(const std::string& name, float fade);
  void goCue(int index);

  RigState snapshot() const;
  void apply(const RigState& s);

  std::string serialize() const;
  bool deserialize(const std::string& data);
  bool saveFile(const std::string& path) const;
  bool loadFile(const std::string& path);

 private:
  RigState fadeFrom_, fadeTo_;
  float fadeT_ = 1.f, fadeDur_ = 0.f;
};

glm::vec3 kelvinToLinearRGB(float kelvin);

}  // namespace og

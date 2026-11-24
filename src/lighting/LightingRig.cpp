#include "lighting/LightingRig.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace og {

const char* goboName(Gobo g) {
  switch (g) {
    case Gobo::None: return "Open";
    case Gobo::Breakup: return "Leaf breakup";
    case Gobo::Window: return "Window";
    case Gobo::Stars: return "Stars";
    default: return "?";
  }
}

// Tanner Helland's blackbody fit (sRGB), linearised and normalised to unit
// luminance so changing Kelvin changes hue, not brightness.
glm::vec3 kelvinToLinearRGB(float kelvin) {
  float t = std::clamp(kelvin, 1000.f, 40000.f) / 100.f;
  float r, g, b;
  if (t <= 66.f) {
    r = 255.f;
    g = 99.4708025861f * std::log(t) - 161.1195681661f;
  } else {
    r = 329.698727446f * std::pow(t - 60.f, -0.1332047592f);
    g = 288.1221695283f * std::pow(t - 60.f, -0.0755148492f);
  }
  if (t >= 66.f) b = 255.f;
  else if (t <= 19.f) b = 0.f;
  else b = 138.5177312231f * std::log(t - 10.f) - 305.0447927307f;
  glm::vec3 s = glm::clamp(glm::vec3(r, g, b) / 255.f, 0.f, 1.f);
  glm::vec3 lin = glm::pow(s, glm::vec3(2.2f));
  float lum = glm::dot(lin, glm::vec3(0.2126f, 0.7152f, 0.0722f));
  return lin / std::max(lum, 1e-4f);
}

glm::vec3 SpotLight::color() const {
  if (useKelvin) return kelvinToLinearRGB(kelvin);
  float lum = glm::dot(rgb, glm::vec3(0.2126f, 0.7152f, 0.0722f));
  return rgb / std::max(lum, 1e-3f);
}

float SpotLight::cosOuter() const { return std::cos(glm::radians(std::clamp(beamAngle, 2.f, 120.f) * 0.5f)); }
float SpotLight::cosInner() const {
  float outer = std::clamp(beamAngle, 2.f, 120.f) * 0.5f;
  float inner = outer * (1.f - std::clamp(softness, 0.f, 0.95f));
  return std::cos(glm::radians(inner));
}
// Distance where illuminance drops to ~1 lux.
float SpotLight::range() const { return std::clamp(std::sqrt(intensityKcd * 1000.f), 8.f, 80.f); }

void SpotLight::aimAt(const glm::vec3& p) {
  glm::vec3 d = p - position;
  if (glm::length(d) > 1e-3f) direction = glm::normalize(d);
}

// Auditorium layout (see scene/Venue.cpp): stage deck y=1.1, z<-1; pit z -1..3;
// stalls rake back to z=20; horseshoe centred (0,5) r=11.5..15; tiers y=4.5/8/11.5.
void LightingRig::buildMounts() {
  mounts.clear();
  for (float x : {-6.f, -2.f, 2.f, 6.f}) mounts.push_back({"FOH truss " + std::to_string((int)x), {x, 11.5f, 2.5f}});
  for (float x : {-6.f, -2.f, 2.f, 6.f}) mounts.push_back({"1st electric " + std::to_string((int)x), {x, 10.5f, -6.f}});
  for (float x : {-6.f, 0.f, 6.f}) mounts.push_back({"Upstage electric " + std::to_string((int)x), {x, 10.f, -11.f}});
  mounts.push_back({"Box boom L", {-9.5f, 6.f, -1.5f}});
  mounts.push_back({"Box boom R", {9.5f, 6.f, -1.5f}});
  mounts.push_back({"Balcony rail L", {-9.f, 5.2f, 10.f}});
  mounts.push_back({"Balcony rail R", {9.f, 5.2f, 10.f}});
  // booth sits above the top tier's parapet (y 12.4) so the beams clear it
  mounts.push_back({"Followspot booth L", {-3.f, 13.6f, 17.5f}});
  mounts.push_back({"Followspot booth R", {3.f, 13.6f, 17.5f}});
  mounts.push_back({"Stage boom L", {-9.f, 3.f, -8.f}});
  mounts.push_back({"Stage boom R", {9.f, 3.f, -8.f}});
}

void LightingRig::setMount(SpotLight& L, int mount) {
  if (mounts.empty()) return;
  glm::vec3 aim = L.position + L.direction * 10.f;
  L.mount = std::clamp(mount, 0, (int)mounts.size() - 1);
  L.position = mounts[L.mount].position;
  L.aimAt(aim);
}

static int findMount(const std::vector<MountPoint>& m, const std::string& prefix) {
  for (size_t i = 0; i < m.size(); ++i)
    if (m[i].name.rfind(prefix, 0) == 0) return (int)i;
  return 0;
}

void LightingRig::applyDefaultAgent327() {
  if (mounts.empty()) buildMounts();
  lights.clear();
  auto place = [&](SpotLight L, const std::string& mount, glm::vec3 aim) {
    L.mount = findMount(mounts, mount);
    L.position = mounts[L.mount].position;
    L.aimAt(aim);
    lights.push_back(L);
  };
  // Two warm front followspots (key light on faces)
  SpotLight f;
  f.kelvin = 3000.f;
  f.intensityKcd = 650.f;
  f.beamAngle = 9.f;
  f.softness = 0.45f;
  f.name = "Followspot L (warm)";
  f.followTarget = 0;
  place(f, "Followspot booth L", {-2.f, 2.2f, -5.f});
  f.name = "Followspot R (warm)";
  f.followTarget = 1;
  place(f, "Followspot booth R", {2.f, 2.2f, -5.f});

  // One cool back light per section: singers, orchestra left, orchestra right (rim light)
  SpotLight b;
  b.kelvin = 7500.f;
  b.intensityKcd = 90.f;
  b.beamAngle = 32.f;
  b.softness = 0.5f;
  b.name = "Back light stage (cool)";
  place(b, "Upstage electric 0", {0.f, 1.1f, -4.5f});
  b.name = "Back light orchestra L (cool)";
  b.beamAngle = 38.f;
  place(b, "Upstage electric -6", {-3.3f, 1.3f, -8.4f});
  b.name = "Back light orchestra R (cool)";
  place(b, "Upstage electric 6", {3.3f, 1.3f, -8.4f});

  // Low warm side fill so the orchestra reads under the house
  SpotLight s;
  s.kelvin = 2800.f;
  s.intensityKcd = 25.f;
  s.beamAngle = 45.f;
  s.softness = 0.8f;
  s.castsShadow = true;
  s.intensityKcd = 60.f;
  s.name = "Box boom L (warm fill)";
  place(s, "Box boom L", {0.f, 1.6f, -8.f});

  haze = 0.30f;
  houseLights = 0.10f;
  chandelier = 0.35f;
  selected = 0;
  markDirty();
}

void LightingRig::addLight() {
  if ((int)lights.size() >= kMaxLights) return;
  SpotLight n = lights.empty() ? SpotLight{} : lights[std::clamp(selected, 0, (int)lights.size() - 1)];
  n.name = "Spot " + std::to_string(lights.size() + 1);
  n.followTarget = -1;
  n.castsShadow = (int)lights.size() < kMaxShadowed;
  if (!mounts.empty()) {
    n.mount = (int)(lights.size() * 3) % (int)mounts.size();
    n.position = mounts[n.mount].position;
  }
  n.aimAt({0.f, 1.1f, -5.f});
  lights.push_back(n);
  selected = (int)lights.size() - 1;
  markDirty();
}

int LightingRig::addFollowspot(int target, const glm::vec3& targetPos, const std::string& name) {
  for (size_t i = 0; i < lights.size(); ++i)
    if (lights[i].followTarget == target) { selected = (int)i; return (int)i; }  // already lit
  if ((int)lights.size() >= kMaxLights) return -1;
  if (mounts.empty()) buildMounts();
  // hang it on the front-of-house truss position closest to the frog
  int best = 0;
  float bd = 1e9f;
  for (size_t m = 0; m < mounts.size(); ++m) {
    if (mounts[m].name.rfind("FOH truss", 0) != 0) continue;
    float d = std::abs(mounts[m].position.x - targetPos.x);
    if (d < bd) { bd = d; best = (int)m; }
  }
  SpotLight L;
  L.name = "Spot: " + name;
  L.mount = best;
  L.position = mounts[best].position;
  L.kelvin = 3200.f;
  L.intensityKcd = 220.f;
  L.beamAngle = 7.f;
  L.softness = 0.45f;
  L.castsShadow = true;
  L.followTarget = target;
  L.aimAt(targetPos + glm::vec3(0, 1.3f, 0));
  lights.push_back(L);
  selected = (int)lights.size() - 1;
  markDirty();
  return selected;
}

void LightingRig::removeFollowspots() {
  // keep the preset's two singer followspots (targets 0/1), drop the per-frog spots
  lights.erase(std::remove_if(lights.begin(), lights.end(),
                              [](const SpotLight& L) { return L.name.rfind("Spot: ", 0) == 0; }),
               lights.end());
  selected = std::clamp(selected, 0, std::max(0, (int)lights.size() - 1));
  markDirty();
}

void LightingRig::removeSelected() {
  if (lights.empty()) return;
  lights.erase(lights.begin() + std::clamp(selected, 0, (int)lights.size() - 1));
  selected = std::max(0, std::min(selected, (int)lights.size() - 1));
  markDirty();
}

void LightingRig::updateFollowspots(const std::vector<glm::vec3>& targets) {
  for (auto& L : lights) {
    if (L.followTarget >= 0 && L.followTarget < (int)targets.size()) {
      glm::vec3 aim = targets[L.followTarget] + glm::vec3(0, 1.3f, 0);
      glm::vec3 want = glm::normalize(aim - L.position);
      // operators don't snap: critically-damped-ish follow
      L.direction = glm::normalize(glm::mix(L.direction, want, 0.25f));
    }
  }
}

RigState LightingRig::snapshot() const { return {lights, haze, houseLights, chandelier}; }

void LightingRig::apply(const RigState& s) {
  lights = s.lights;
  haze = s.haze;
  houseLights = s.houseLights;
  chandelier = s.chandelier;
  selected = std::clamp(selected, 0, std::max(0, (int)lights.size() - 1));
  markDirty();
}

void LightingRig::recordCue(const std::string& name, float fade) {
  cues.push_back({name, snapshot(), fade});
}

void LightingRig::goCue(int index) {
  if (index < 0 || index >= (int)cues.size()) return;
  activeCue = index;
  fadeFrom_ = snapshot();
  fadeTo_ = cues[index].state;
  fadeDur_ = std::max(0.f, cues[index].fadeSeconds);
  fadeT_ = 0.f;
  if (fadeDur_ <= 0.f || fadeFrom_.lights.size() != fadeTo_.lights.size()) {
    apply(fadeTo_);
    fadeT_ = 1.f;
  }
}

void LightingRig::update(float dt) {
  if (fadeT_ >= 1.f) return;
  fadeT_ = std::min(1.f, fadeT_ + dt / std::max(fadeDur_, 1e-3f));
  float e = fadeT_ * fadeT_ * (3.f - 2.f * fadeT_);
  RigState s = fadeTo_;
  for (size_t i = 0; i < s.lights.size(); ++i) {
    auto& a = fadeFrom_.lights[i];
    auto& b = s.lights[i];
    float ia = a.enabled ? a.intensityKcd : 0.f;
    float ib = b.enabled ? b.intensityKcd : 0.f;
    b.intensityKcd = glm::mix(ia, ib, e);
    b.enabled = b.intensityKcd > 0.01f;
    b.direction = glm::normalize(glm::mix(a.direction, b.direction, e));
    b.kelvin = glm::mix(a.kelvin, b.kelvin, e);
    b.rgb = glm::mix(a.rgb, b.rgb, e);
    b.beamAngle = glm::mix(a.beamAngle, b.beamAngle, e);
    b.softness = glm::mix(a.softness, b.softness, e);
  }
  s.haze = glm::mix(fadeFrom_.haze, fadeTo_.haze, e);
  s.houseLights = glm::mix(fadeFrom_.houseLights, fadeTo_.houseLights, e);
  s.chandelier = glm::mix(fadeFrom_.chandelier, fadeTo_.chandelier, e);
  int sel = selected;
  apply(s);
  selected = sel;
}

std::string LightingRig::serialize() const {
  std::ostringstream ss;
  ss.precision(6);
  ss << "OGRIG 2\n" << haze << " " << houseLights << " " << chandelier << " " << lights.size() << "\n";
  for (auto& L : lights) {
    std::string name = L.name;
    std::replace(name.begin(), name.end(), '\n', ' ');
    ss << name << "\n"
       << L.mount << " " << L.position.x << " " << L.position.y << " " << L.position.z << " "
       << L.direction.x << " " << L.direction.y << " " << L.direction.z << " " << (int)L.useKelvin << " "
       << L.kelvin << " " << L.rgb.x << " " << L.rgb.y << " " << L.rgb.z << " " << L.intensityKcd << " "
       << L.beamAngle << " " << L.softness << " " << (int)L.castsShadow << " " << (int)L.enabled << " "
       << L.followTarget << " " << (int)L.gobo << "\n";
  }
  return ss.str();
}

bool LightingRig::deserialize(const std::string& data) {
  std::istringstream ss(data);
  std::string magic;
  int ver = 0;
  ss >> magic >> ver;
  if (magic != "OGRIG" || ver != 2) return false;
  RigState s;
  size_t n = 0;
  ss >> s.haze >> s.houseLights >> s.chandelier >> n;
  if (!ss || n > (size_t)kMaxLights) return false;
  ss.ignore(1, '\n');
  for (size_t i = 0; i < n; ++i) {
    SpotLight L;
    std::getline(ss, L.name);
    int uk = 1, cs = 1, en = 1, gobo = 0;
    ss >> L.mount >> L.position.x >> L.position.y >> L.position.z >> L.direction.x >> L.direction.y >>
        L.direction.z >> uk >> L.kelvin >> L.rgb.x >> L.rgb.y >> L.rgb.z >> L.intensityKcd >> L.beamAngle >>
        L.softness >> cs >> en >> L.followTarget >> gobo;
    if (!ss) return false;
    ss.ignore(1, '\n');
    L.useKelvin = uk != 0;
    L.castsShadow = cs != 0;
    L.enabled = en != 0;
    L.gobo = (Gobo)std::clamp(gobo, 0, (int)Gobo::Count - 1);
    if (glm::length(L.direction) < 1e-4f) L.direction = {0, -1, 0};
    L.direction = glm::normalize(L.direction);
    s.lights.push_back(L);
  }
  uint32_t rev = revision;
  int sel = selected;
  apply(s);
  revision = rev;  // remote state is not a local edit
  selected = std::clamp(sel, 0, std::max(0, (int)lights.size() - 1));
  return true;
}

bool LightingRig::saveFile(const std::string& path) const {
  std::ofstream f(path);
  if (!f) return false;
  f << serialize();
  return (bool)f;
}

bool LightingRig::loadFile(const std::string& path) {
  std::ifstream f(path);
  if (!f) return false;
  std::string s((std::istreambuf_iterator<char>(f)), {});
  if (!deserialize(s)) return false;
  markDirty();
  return true;
}

}  // namespace og

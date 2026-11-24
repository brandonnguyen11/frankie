#pragma once
#include "render/Mesh.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace og {

// Which dimmer an emissive/point light belongs to.
enum class LightGroup : int { Always = 0, House = 1, Chandelier = 2 };

struct SceneObject {
  MeshPtr mesh;
  Material mat;
  glm::mat4 model{1.f};
  bool castsShadow = true;
  LightGroup group = LightGroup::Always;  // scales emissive by that dimmer
  std::string name;
};

// Non-shadowed omni light: chandelier, sconces, streetlamps, neon spill.
struct PointLight {
  glm::vec3 position{0.f};
  glm::vec3 color{1.f};       // linear, unit luminance
  float intensityCd = 1000.f; // candela
  float radius = 10.f;        // window radius for falloff
  LightGroup group = LightGroup::Always;
};

struct SceneData {
  std::vector<SceneObject> objects;
  std::vector<PointLight> points;
  void add(MeshPtr mesh, const Material& mat, const glm::mat4& model = glm::mat4(1.f), bool shadow = true,
           LightGroup group = LightGroup::Always) {
    SceneObject o;
    o.mesh = std::move(mesh);
    o.mat = mat;
    o.model = model;
    o.castsShadow = shadow;
    o.group = group;
    objects.push_back(std::move(o));
  }
};

// Seat placement used by the stalls/balconies and by audience seating.
struct SeatSpot {
  glm::vec3 position;
  float yaw;  // degrees, frog faces +Z at yaw 0
};

SceneData buildCity();         // plaza, avenue, storefronts, cars, sky (CityScene.cpp)
SceneData buildAuditorium(std::vector<SeatSpot>* audienceSeats);  // AuditoriumScene.cpp

// Optional imported models (assets/models/*.glb|gltf) replace procedural parts.
bool loadModelInto(const std::string& path, SceneData& out, const glm::mat4& xf);

}  // namespace og

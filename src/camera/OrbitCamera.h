#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace og {

// Third-person follow camera orbiting the player frog (W1).
class OrbitCamera {
 public:
  glm::vec3 target{0, 1, 0};
  float distance = 5.5f;
  float yaw = 90.f;    // 90 = camera behind a frog walking toward -Z
  float pitch = 18.f;
  float fov = 55.f;
  float nearP = 0.1f;
  float farP = 600.f;

  void orbit(float dx, float dy);
  void zoom(float delta);
  void setFollow(const glm::vec3& pos, float height = 1.5f);
  glm::mat4 view() const;
  glm::mat4 proj(float aspect) const;
  glm::vec3 position() const;
};

}  // namespace og

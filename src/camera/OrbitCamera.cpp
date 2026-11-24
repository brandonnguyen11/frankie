#include "camera/OrbitCamera.h"
#include <algorithm>
#include <cmath>

namespace og {

void OrbitCamera::orbit(float dx, float dy) {
  yaw += dx * 0.15f;
  pitch = std::clamp(pitch + dy * 0.15f, -10.f, 75.f);
}

void OrbitCamera::zoom(float delta) { distance = std::clamp(distance - delta * 0.5f, 2.f, 14.f); }

void OrbitCamera::setFollow(const glm::vec3& pos, float height) {
  glm::vec3 want = pos + glm::vec3(0, height, 0);
  target = want;
}

glm::vec3 OrbitCamera::position() const {
  float yr = glm::radians(yaw), pr = glm::radians(pitch);
  return target + glm::vec3(distance * std::cos(pr) * std::cos(yr), distance * std::sin(pr),
                            distance * std::cos(pr) * std::sin(yr));
}

glm::mat4 OrbitCamera::view() const { return glm::lookAt(position(), target, glm::vec3(0, 1, 0)); }

glm::mat4 OrbitCamera::proj(float aspect) const {
  return glm::perspective(glm::radians(fov), aspect, nearP, farP);
}

}  // namespace og

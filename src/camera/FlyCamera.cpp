#include "camera/FlyCamera.h"
#include "camera/RailCamera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace og {

void FlyCamera::reset(Mode m) {
  velocity = glm::vec3(0.f);
  bank = 0.f;
  target = stageCentre();
  if (m == Mode::Free) {
    position = {0.f, 7.f, 13.f};
    yaw = -90.f;
    pitch = -14.f;
  } else {
    position = {-5.f, 5.5f, 6.f};  // three-quarter view from over the front stalls
  }
  glm::vec3 f = m == Mode::Free ? glm::vec3(0, std::sin(glm::radians(pitch)), -std::cos(glm::radians(pitch)))
                                : target - position;
  orientation = lookRotation(f);
}

void FlyCamera::update(float dt, Mode m, glm::vec3 move, float mdx, float mdy, bool slow) {
  time += dt;
  float maxSpeed = (m == Mode::Free ? 4.5f : 3.2f) * (slow ? 0.3f : 1.f);
  glm::vec3 fwd, right;
  if (m == Mode::Free) {
    yaw += mdx * 0.12f;
    pitch = std::clamp(pitch - mdy * 0.12f, -89.f, 89.f);
    float yr = glm::radians(yaw), pr = glm::radians(pitch);
    fwd = {std::cos(pr) * std::cos(yr), std::sin(pr), std::cos(pr) * std::sin(yr)};  // fly where you look
    right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
  } else {
    // mouse slides the lock-on point across the stage
    target.x = std::clamp(target.x + mdx * 0.012f, -8.f, 8.f);
    target.y = std::clamp(target.y - mdy * 0.008f, 0.8f, 7.f);
    glm::vec3 toT = target - position;
    toT.y = 0.f;
    fwd = glm::length(toT) > 1e-3f ? glm::normalize(toT) : glm::vec3(0, 0, -1);  // level flight
    right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
  }
  glm::vec3 want = (right * move.x + glm::vec3(0, 1, 0) * move.y + fwd * move.z) * maxSpeed;
  // inertia: accelerate/brake smoothly like a real drone
  velocity += (want - velocity) * (1.f - std::exp(-dt * (m == Mode::Drone ? 2.5f : 6.f)));
  position += velocity * dt;

  glm::quat wantQ;
  if (m == Mode::Free) {
    float yr = glm::radians(yaw), pr = glm::radians(pitch);
    wantQ = lookRotation({std::cos(pr) * std::cos(yr), std::sin(pr), std::cos(pr) * std::sin(yr)});
  } else {
    glm::vec3 toT = target - eye();
    if (glm::length(toT) < 1.5f) {  // never fly into the subject
      position -= glm::normalize(toT) * (1.5f - glm::length(toT));
      toT = target - eye();
    }
    // bank into sideways motion, gimbal keeps the horizon mostly level
    float side = glm::dot(velocity, right);
    bank += (std::clamp(-side * 4.f, -12.f, 12.f) - bank) * (1.f - std::exp(-dt * 3.f));
    wantQ = glm::angleAxis(glm::radians(bank * 0.35f), glm::normalize(toT)) * lookRotation(toT);
  }
  // gyro stabilisation: slerp toward the wanted attitude
  float k = 1.f - std::exp(-dt * (m == Mode::Drone ? 14.f : 16.f));  // drone gimbal keeps up with strafes
  orientation = glm::normalize(glm::slerp(orientation, wantQ, k));
}

glm::vec3 FlyCamera::eye() const {
  // a hovering drone is never perfectly still
  return position + glm::vec3(std::sin(time * 1.3f) * 0.012f, std::sin(time * 2.1f) * 0.018f, 0.f);
}

glm::mat4 FlyCamera::view() const {
  return glm::mat4_cast(glm::conjugate(orientation)) * glm::translate(glm::mat4(1.f), -eye());
}

}  // namespace og

#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace og {

// Free-flying cameras for the camera operator, alongside the rails.
//  Free:  360-degree spectator camera; the mouse looks anywhere, WASD flies along the view.
//  Drone: stabilised gimbal locked onto a point on the stage; you fly the drone
//         (strafe, climb, approach) and the camera keeps the stage framed.
class FlyCamera {
 public:
  enum class Mode { Free, Drone };

  glm::vec3 position{0.f, 6.f, 12.f};
  glm::vec3 velocity{0.f};
  float yaw = -90.f, pitch = -12.f;               // free-look angles (degrees)
  glm::vec3 target{0.f, 2.2f, -6.f};              // drone lock-on point
  glm::quat orientation{1, 0, 0, 0};
  float bank = 0.f;                               // drone roll from sideways motion (degrees)
  float time = 0.f;

  static glm::vec3 stageCentre() { return {0.f, 2.2f, -6.f}; }
  void reset(Mode m);
  // move: x = right, y = up, z = forward (each -1..1); mdx/mdy = mouse delta
  void update(float dt, Mode m, glm::vec3 move, float mdx, float mdy, bool slow);
  glm::vec3 forward() const { return orientation * glm::vec3(0, 0, -1); }
  glm::vec3 eye() const;                          // position + hover bob
  glm::mat4 view() const;
};

}  // namespace og

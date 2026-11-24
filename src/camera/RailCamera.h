#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace og {

// Catmull-Rom spline with an arc-length table so the dolly moves at constant speed.
struct CameraRail {
  std::string name;
  std::vector<glm::vec3> points;
  std::vector<float> arcTable;  // cumulative length at uniform parameter steps
  float totalLength = 0.f;
  float defaultYaw = -90.f;     // facing the stage

  void rebuildArcTable();
  glm::vec3 sampleParam(float u) const;  // u in [0, points-1]
  glm::vec3 sampleByArc(float s) const;
};

class RailCamera {
 public:
  std::vector<CameraRail> rails;
  int activeRail = 0;
  float s = 0.f;            // arc length along the rail
  float velocity = 0.f;     // eased dolly speed (C8)
  float yaw = -90.f;        // degrees, -90 looks down -Z at the stage
  float pitch = -4.f;
  float focalLengthMm = 35.f;   // 18-135 mm full-frame equivalent
  float focusDistance = 12.f;   // metres
  float fStop = 2.8f;
  float evOffset = 0.f;
  bool slowPan = false;
  glm::quat orientation{1, 0, 0, 0};  // smoothed (slerped) toward yaw/pitch

  void buildDefaultRails();
  void update(float dt, float moveAxis, float yawDelta, float pitchDelta);
  void switchRail(int index);
  glm::vec3 position() const;
  glm::quat targetOrientation() const;
  glm::mat4 view() const;
  glm::mat4 proj(float aspect) const;
  float fovYDegrees() const;  // from focal length on a 24 mm-high sensor
};

glm::quat lookRotation(const glm::vec3& forward);

}  // namespace og

#include "camera/RailCamera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace og {
namespace {

glm::vec3 catmull(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
  float t2 = t * t, t3 = t2 * t;
  return 0.5f * ((2.f * p1) + (-p0 + p2) * t + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t2 +
                 (-p0 + 3.f * p1 - 3.f * p2 + p3) * t3);
}
constexpr int kArcSteps = 256;

}  // namespace

glm::quat lookRotation(const glm::vec3& forward) {
  return glm::quatLookAt(glm::normalize(forward), glm::vec3(0, 1, 0));
}

glm::vec3 CameraRail::sampleParam(float u) const {
  int n = (int)points.size();
  if (n == 0) return {};
  if (n == 1) return points[0];
  u = std::clamp(u, 0.f, (float)(n - 1));
  int seg = std::min((int)std::floor(u), n - 2);
  float t = u - seg;
  auto at = [&](int i) {
    // reflect end points so the curve starts/ends with sensible tangents
    if (i < 0) return 2.f * points[0] - points[1];
    if (i >= n) return 2.f * points[n - 1] - points[n - 2];
    return points[i];
  };
  return catmull(at(seg - 1), at(seg), at(seg + 1), at(seg + 2), t);
}

void CameraRail::rebuildArcTable() {
  arcTable.assign(1, 0.f);
  totalLength = 0.f;
  if (points.size() < 2) return;
  glm::vec3 prev = sampleParam(0.f);
  for (int i = 1; i <= kArcSteps; ++i) {
    glm::vec3 p = sampleParam((float)i / kArcSteps * (points.size() - 1));
    totalLength += glm::length(p - prev);
    arcTable.push_back(totalLength);
    prev = p;
  }
}

glm::vec3 CameraRail::sampleByArc(float s) const {
  if (points.size() < 2 || arcTable.size() < 2) return points.empty() ? glm::vec3(0) : points[0];
  s = std::clamp(s, 0.f, totalLength);
  // invert the arc-length table: binary search then linear interpolate
  auto it = std::lower_bound(arcTable.begin(), arcTable.end(), s);
  int i = std::clamp((int)std::distance(arcTable.begin(), it), 1, (int)arcTable.size() - 1);
  float a0 = arcTable[i - 1], a1 = arcTable[i];
  float f = a1 > a0 ? (s - a0) / (a1 - a0) : 0.f;
  float u = ((float)(i - 1) + f) / kArcSteps * (points.size() - 1);
  return sampleParam(u);
}

void RailCamera::buildDefaultRails() {
  rails.clear();
  auto straight = [&](const std::string& name, float z, float y, float x0, float x1, float yaw) {
    CameraRail r;
    r.name = name;
    for (int i = 0; i <= 6; ++i) r.points.push_back({x0 + (x1 - x0) * i / 6.f, y, z});
    r.defaultYaw = yaw;
    r.rebuildArcTable();
    rails.push_back(r);
  };
  straight("Front stalls", 3.9f, 1.5f, -8.f, 8.f, -90.f);
  straight("Mid stalls", 11.4f, 2.4f, -8.f, 8.f, -90.f);
  straight("Rear stalls", 15.6f, 3.0f, -8.f, 8.f, -90.f);
  {
    CameraRail r;
    r.name = "First balcony";
    for (int i = 0; i <= 10; ++i) {
      float a = -1.9f + 3.8f * i / 10.f;
      r.points.push_back({std::sin(a) * 12.0f, 5.9f, 5.f + std::cos(a) * 12.0f});
    }
    r.defaultYaw = -90.f;
    r.rebuildArcTable();
    rails.push_back(r);
  }
  straight("On stage", -4.3f, 2.6f, -8.f, 8.f, -90.f);
  switchRail(1);
  orientation = targetOrientation();
}

void RailCamera::switchRail(int index) {
  if (index < 0 || index >= (int)rails.size()) return;
  activeRail = index;
  s = rails[activeRail].totalLength * 0.5f;
  velocity = 0.f;
  yaw = rails[activeRail].defaultYaw;
  pitch = index == 3 ? -14.f : (index == 4 ? -6.f : -4.f);
  focusDistance = std::max(2.f, glm::distance(position(), glm::vec3(0, 1.8f, -5.f)));
}

void RailCamera::update(float dt, float moveAxis, float yawDelta, float pitchDelta) {
  if (rails.empty()) return;
  auto& rail = rails[activeRail];
  // eased dolly: accelerate toward target speed rather than jumping (C8)
  float maxSpeed = slowPan ? 0.8f : 2.6f;
  float target = moveAxis * maxSpeed;
  float accel = 3.f;
  velocity += std::clamp(target - velocity, -accel * dt, accel * dt);
  s += velocity * dt;
  if (s <= 0.f || s >= rail.totalLength) velocity = 0.f;
  s = std::clamp(s, 0.f, rail.totalLength);
  // pan sensitivity scales with focal length so long lenses stay controllable
  float panScale = (slowPan ? 0.025f : 0.1f) * std::min(1.f, 35.f / focalLengthMm);
  yaw += yawDelta * panScale;
  pitch = std::clamp(pitch - pitchDelta * panScale, -75.f, 60.f);
  focalLengthMm = std::clamp(focalLengthMm, 18.f, 135.f);
  fStop = std::clamp(fStop, 1.4f, 16.f);
  focusDistance = std::clamp(focusDistance, 0.5f, 60.f);
  // quaternion slerp gives the fluid-head feel on pans and tilts
  float k = 1.f - std::exp(-dt * (slowPan ? 6.f : 14.f));
  orientation = glm::slerp(orientation, targetOrientation(), k);
}

glm::quat RailCamera::targetOrientation() const {
  float yr = glm::radians(yaw), pr = glm::radians(pitch);
  glm::vec3 f{std::cos(pr) * std::cos(yr), std::sin(pr), std::cos(pr) * std::sin(yr)};
  return lookRotation(f);
}

float RailCamera::fovYDegrees() const { return 2.f * glm::degrees(std::atan(12.f / focalLengthMm)); }

glm::vec3 RailCamera::position() const {
  if (rails.empty()) return {};
  return rails[activeRail].sampleByArc(s);
}

glm::mat4 RailCamera::view() const {
  glm::mat4 r = glm::mat4_cast(glm::conjugate(orientation));
  return r * glm::translate(glm::mat4(1), -position());
}

glm::mat4 RailCamera::proj(float aspect) const {
  return glm::perspective(glm::radians(fovYDegrees()), aspect, 0.1f, 600.f);
}

}  // namespace og

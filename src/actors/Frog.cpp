#include "actors/Frog.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace og {

const char* roleName(Role r) {
  switch (r) {
    case Role::Musician: return "Musician";
    case Role::Singer: return "Singer";
    case Role::LightingDesigner: return "Lighting designer";
    case Role::CameraOperator: return "Camera operator";
    case Role::Audience: return "Audience";
    default: return "?";
  }
}

namespace {

constexpr float kPi = 3.14159265f;

glm::mat4 T(glm::vec3 p) { return glm::translate(glm::mat4(1), p); }
glm::mat4 Rx(float deg) { return glm::rotate(glm::mat4(1), glm::radians(deg), {1, 0, 0}); }
glm::mat4 Ry(float deg) { return glm::rotate(glm::mat4(1), glm::radians(deg), {0, 1, 0}); }
glm::mat4 Rz(float deg) { return glm::rotate(glm::mat4(1), glm::radians(deg), {0, 0, 1}); }
glm::mat4 S(glm::vec3 s) { return glm::scale(glm::mat4(1), s); }

// Shared unit meshes, created on first use (GL context must exist).
struct FrogMeshes {
  MeshPtr sphere, limb, box, cyl, bell, bowl, violinBody;
  FrogMeshes() {
    sphere = makeSphere(1.f, 20, 14);
    MeshBuilder l;  // capsule-ish limb, length 1 hanging down from the pivot
    l.cylinder(T({0, -0.5f, 0}), 1.f, 0.85f, 1.f, 14, false);
    l.sphere(T({0, 0, 0}), 1.f, 14, 8);
    l.sphere(T({0, -1.f, 0}), 0.85f, 14, 8);
    limb = l.build();
    box = makeBox({1, 1, 1});
    cyl = makeCylinder(1.f, 1.f, 16);
    MeshBuilder b;
    b.lathe(glm::mat4(1), {{0.06f, 0.f}, {0.08f, 0.5f}, {0.2f, 0.85f}, {0.5f, 1.f}, {0.48f, 1.02f}}, 20);
    bell = b.build();
    MeshBuilder bw;
    bw.lathe(glm::mat4(1), {{0.f, 0.f}, {0.25f, 0.02f}, {0.42f, 0.18f}, {0.5f, 0.42f}, {0.52f, 0.5f}}, 28);
    bw.cylinder(T({0, 0.505f, 0}), 0.515f, 0.515f, 0.01f, 28);
    bowl = bw.build();
    MeshBuilder vb;  // figure-of-eight body
    vb.sphere(glm::scale(T({0, 0.28f, 0}), {1.f, 0.9f, 0.32f}), 0.2f, 16, 10);
    vb.sphere(glm::scale(T({0, -0.05f, 0}), {1.15f, 1.f, 0.32f}), 0.24f, 16, 10);
    violinBody = vb.build();
  }
};
const FrogMeshes& meshes() {
  static FrogMeshes m;
  return m;
}

struct Painter {
  std::vector<SceneObject>& out;
  void draw(const MeshPtr& m, const Material& mat, const glm::mat4& xf) {
    SceneObject o;
    o.mesh = m;
    o.mat = mat;
    o.model = xf;
    out.push_back(std::move(o));
  }
};

// Limb pose: forward swing (deg, + = toward +Z), sideways roll (deg, + = away from body on that side)
glm::mat4 limbRot(float forward, float outward, float side) { return Rz(outward * side) * Rx(-forward); }

}  // namespace

void FrogActor::onNote(float velocity) {
  noteImpulse = std::min(1.f, 0.6f + 0.4f * velocity);
  ++noteCount;
}

void FrogActor::update(float dt) {
  anim += dt;
  noteImpulse = std::max(0.f, noteImpulse - dt * 4.f);
  sitBlend += ((sitting ? 1.f : 0.f) - sitBlend) * std::min(1.f, dt * 6.f);
  walkPhase += speed * dt * 4.2f;
  if (!local && !npc && lastHeard > 0.0) {
    // remote frogs: exponential smoothing toward last network pose
    float k = std::min(1.f, dt * 12.f);
    glm::vec3 before = position;
    position += (netPosition - position) * k;
    float dy = std::remainder(netYaw - yaw, 360.f);
    yaw += dy * k;
    float v = glm::length(glm::vec2(position.x - before.x, position.z - before.z)) / std::max(dt, 1e-4f);
    speed += (v - speed) * std::min(1.f, dt * 8.f);
  }
}

void FrogActor::appendDraw(std::vector<SceneObject>& out) const {
  const auto& M = meshes();
  Painter p{out};

  Material skin;
  skin.type = MatType::FrogSkin;
  skin.albedo = skinColor;
  skin.roughness = 0.42f;
  skin.translucency = 0.45f;
  skin.clearcoat = 0.6f;  // wet sheen
  Material belly = skin;
  belly.albedo = glm::mix(skinColor, glm::vec3(0.75f, 0.72f, 0.45f), 0.6f);
  Material suit;
  suit.type = MatType::Suit;
  suit.albedo = suitColor;
  suit.roughness = 0.75f;
  suit.sheen = 0.6f;
  Material shirt = Material::standard({0.85f, 0.84f, 0.8f}, 0.6f);
  Material black = Material::standard({0.01f, 0.01f, 0.01f}, 0.35f);
  Material eyeM = Material::standard({0.85f, 0.7f, 0.15f}, 0.08f);
  eyeM.clearcoat = 1.f;
  Material pupil = Material::standard({0.005f, 0.005f, 0.005f}, 0.05f);
  pupil.clearcoat = 1.f;
  Material mouthIn = Material::standard({0.25f, 0.04f, 0.05f}, 0.4f);
  Material wood = Material::wood({0.35f, 0.12f, 0.04f}, 3.f);
  wood.clearcoat = 1.f;
  wood.roughness = 0.3f;
  Material brass = Material::gold(0.2f);
  brass.roughness = 0.2f;
  Material silver = Material::standard({0.92f, 0.92f, 0.94f}, 0.15f, 1.f);
  Material copper = Material::standard({0.95f, 0.64f, 0.54f}, 0.3f, 1.f);
  Material skinHead = Material::standard({0.85f, 0.8f, 0.7f}, 0.7f);

  float t = anim;
  float sb = sitBlend;
  float breathe = std::sin(t * 1.6f) * 0.012f;
  float walkAmt = std::clamp(speed / 3.5f, 0.f, 1.f) * (1.f - sb);
  float ph = walkPhase;
  float swing = std::sin(ph) * 28.f * walkAmt;
  float bob = std::abs(std::sin(ph)) * 0.04f * walkAmt;

  glm::mat4 root = T(position) * Ry(yaw);
  float hipY = glm::mix(0.86f, 0.5f, sb) + bob;
  glm::mat4 hips = root * T({0, hipY, glm::mix(0.f, -0.06f, sb)});

  // ---- legs
  for (int s = -1; s <= 1; s += 2) {
    float legSwing = s * swing;
    float thighF = glm::mix(legSwing, 88.f, sb);
    float kneeBend = glm::mix(std::max(0.f, -std::sin(ph + (s > 0 ? 0.f : kPi))) * 45.f * walkAmt, 88.f, sb);
    glm::mat4 thigh = hips * T({s * 0.12f, 0, 0}) * limbRot(thighF, 3.f, (float)s);
    p.draw(M.limb, suit, thigh * S({0.085f, 0.42f, 0.085f}));
    glm::mat4 shin = thigh * T({0, -0.42f, 0}) * Rx(kneeBend);
    p.draw(M.limb, suit, shin * S({0.075f, 0.4f, 0.075f}));
    // shin's world pitch is (kneeBend - thighF); undo it so the foot stays level
    glm::mat4 foot = shin * T({0, -0.42f, 0}) * Rx(thighF - kneeBend);
    p.draw(M.sphere, skin, foot * T({0, -0.02f, 0.1f}) * S({0.1f, 0.035f, 0.17f}));
    p.draw(M.box, black, foot * T({0, 0.02f, 0.02f}) * S({0.07f, 0.03f, 0.09f}));  // spats
  }

  // ---- torso
  float lean = 0.f, torsoTwist = 0.f;
  bool hasInst = role == Role::Musician && seatIndex >= 0;
  float imp = noteImpulse;
  if (hasInst && instrument == InstrumentId::Piano) lean = 12.f + imp * 3.f;
  if (hasInst && instrument == InstrumentId::Cello) lean = 8.f;
  if (role == Role::Singer) lean = -4.f * mouthOpen;
  float breath = breathe + (hasInst && (instrument == InstrumentId::Flute || instrument == InstrumentId::Trumpet)
                                ? imp * 0.03f : 0.f);
  glm::mat4 torso = hips * Rx(lean) * Ry(torsoTwist);
  p.draw(M.sphere, suit, torso * T({0, 0.3f, 0}) * S({0.27f + breath, 0.34f, 0.2f + breath}));
  p.draw(M.sphere, suit, torso * T({0, 0.05f, -0.02f}) * S({0.25f, 0.16f, 0.19f}));
  p.draw(M.sphere, shirt, torso * T({0, 0.42f, 0.12f}) * Rx(-12.f) * S({0.1f, 0.17f, 0.08f}));
  // tail coat flaps
  for (int s = -1; s <= 1; s += 2)
    p.draw(M.box, suit, torso * T({s * 0.1f, -0.1f, -0.17f}) * Rx(10.f + 70.f * sb) * T({0, -0.14f, 0}) *
                           S({0.09f, 0.16f, 0.015f}));
  // bow tie
  glm::mat4 neck = torso * T({0, 0.6f, 0.02f});
  for (int s = -1; s <= 1; s += 2)
    p.draw(M.sphere, black, neck * T({s * 0.055f, -0.03f, 0.13f}) * Rz(s * 90.f) * S({0.045f, 0.06f, 0.02f}));

  // ---- head
  float headTilt = role == Role::Singer ? -10.f * mouthOpen : 0.f;
  if (hasInst && instrument == InstrumentId::Piano) headTilt = 8.f + imp * 4.f;
  if (playing && !hasInst) headTilt = std::sin(t * 4.f) * 4.f;
  float nod = imp * 4.f;
  glm::mat4 head = neck * Rx(headTilt + nod);
  p.draw(M.sphere, skin, head * T({0, 0.14f, 0.02f}) * S({0.27f, 0.17f, 0.25f}));
  // jaw opens with singing / breathing into a wind instrument
  float jaw = std::clamp(mouthOpen, 0.f, 1.f) * 28.f;
  glm::mat4 jawM = head * T({0, 0.1f, -0.1f}) * Rx(jaw);
  p.draw(M.sphere, skin, jawM * T({0, -0.01f, 0.12f}) * S({0.255f, 0.085f, 0.23f}));
  p.draw(M.sphere, belly, jawM * T({0, -0.04f, 0.1f}) * S({0.2f, 0.06f, 0.19f}));
  if (jaw > 1.f) p.draw(M.sphere, mouthIn, head * T({0, 0.07f, 0.04f}) * S({0.22f, 0.06f, 0.19f}));
  for (int s = -1; s <= 1; s += 2) {
    glm::vec3 e{s * 0.13f, 0.27f, 0.07f};
    p.draw(M.sphere, skin, head * T(e - glm::vec3(0, 0.02f, 0)) * S(glm::vec3(0.095f)));
    p.draw(M.sphere, eyeM, head * T(e + glm::vec3(s * 0.01f, 0.012f, 0.03f)) * S(glm::vec3(0.075f)));
    p.draw(M.sphere, pupil, head * T(e + glm::vec3(s * 0.012f, 0.02f, 0.098f)) * S({0.042f, 0.022f, 0.012f}));
  }

  // ---- arms
  // default relaxed / walking
  float uaF[2] = {-swing * 0.8f, swing * 0.8f}, uaOut[2] = {8.f, 8.f};
  float faBend[2] = {12.f, 12.f}, faTwist[2] = {0.f, 0.f};
  if (sb > 0.5f && !hasInst) { uaF[0] = uaF[1] = 25.f; faBend[0] = faBend[1] = 50.f; }

  float bow = std::sin(t * 2.2f + noteCount * kPi) * 0.5f + 0.5f;  // 0..1 bow travel
  if (hasInst) {
    switch (instrument) {
      case InstrumentId::Piano: {
        int hand = noteCount & 1;
        for (int i = 0; i < 2; ++i) {
          uaF[i] = 30.f;
          uaOut[i] = 12.f;
          faBend[i] = 52.f - (i == hand ? imp * 14.f : 0.f) + std::sin(t * 5.f + i) * 2.f;
        }
        break;
      }
      case InstrumentId::Violin:
        uaF[0] = 75.f; uaOut[0] = 30.f; faBend[0] = 95.f; faTwist[0] = 40.f;  // left holds the neck
        uaF[1] = 55.f; uaOut[1] = 35.f + bow * 30.f; faBend[1] = 50.f + bow * 40.f;
        break;
      case InstrumentId::Cello:
        uaF[0] = 40.f; uaOut[0] = 10.f; faBend[0] = 85.f;
        uaF[1] = 30.f; uaOut[1] = 25.f + bow * 35.f; faBend[1] = 60.f - bow * 25.f;
        break;
      case InstrumentId::Flute:
        uaF[0] = 60.f; uaOut[0] = -15.f; faBend[0] = 110.f;
        uaF[1] = 50.f; uaOut[1] = 40.f; faBend[1] = 110.f;
        break;
      case InstrumentId::Trumpet:
        uaF[0] = uaF[1] = 60.f + imp * 6.f; uaOut[0] = uaOut[1] = 12.f; faBend[0] = faBend[1] = 75.f;
        break;
      case InstrumentId::Timpani: {
        int hand = noteCount & 1;
        for (int i = 0; i < 2; ++i) {
          uaF[i] = 35.f;
          uaOut[i] = 22.f;
          faBend[i] = 60.f + (i == hand ? -imp * 35.f : 15.f);
        }
        break;
      }
      default: break;
    }
  } else if (role == Role::Singer) {
    // operatic gesture: hands rise and open toward the house as the voice swells
    float m = std::clamp(mouthOpen, 0.f, 1.f);
    uaF[0] = 18.f + 30.f * m;
    uaF[1] = 30.f + 35.f * m;
    uaOut[0] = 12.f + 14.f * m;
    uaOut[1] = 14.f + 20.f * m;
    faBend[0] = 55.f - 15.f * m;
    faBend[1] = 45.f - 25.f * m;
  } else if (role == Role::LightingDesigner && !sitting) {
    uaF[1] = 80.f; uaOut[1] = 10.f; faBend[1] = 5.f;  // pointing at the rig
  }

  glm::mat4 hand[2];
  for (int i = 0; i < 2; ++i) {
    float s = i == 0 ? -1.f : 1.f;  // 0 = left (-X), 1 = right (+X)
    glm::mat4 ua = torso * T({s * 0.29f, 0.5f, 0}) * limbRot(uaF[i], uaOut[i], s);
    p.draw(M.limb, suit, ua * S({0.07f, 0.29f, 0.07f}));
    glm::mat4 fa = ua * T({0, -0.29f, 0}) * Rx(-faBend[i]) * Ry(faTwist[i] * s);
    p.draw(M.limb, suit, fa * S({0.06f, 0.26f, 0.06f}));
    p.draw(M.cyl, shirt, fa * T({0, -0.25f, 0}) * S({0.055f, 0.03f, 0.055f}));  // cuff
    hand[i] = fa * T({0, -0.3f, 0});
    p.draw(M.sphere, skin, hand[i] * S({0.06f, 0.075f, 0.035f}));
    for (int f = -1; f <= 1; ++f)  // webbed fingertips
      p.draw(M.sphere, skin, hand[i] * T({f * 0.035f, -0.07f, 0}) * S(glm::vec3(0.022f)));
  }

  // ---- instrument props (piano is a fixed set piece in the pit)
  if (!hasInst) return;
  switch (instrument) {
    case InstrumentId::Violin: {
      glm::mat4 v = torso * T({-0.12f, 0.62f, 0.22f}) * Ry(-35.f) * Rx(-80.f) * Rz(10.f);
      p.draw(M.violinBody, wood, v * S(glm::vec3(0.75f)));
      p.draw(M.box, black, v * T({0, 0.42f, 0.01f}) * S({0.018f, 0.2f, 0.015f}));
      glm::mat4 b = hand[1] * Rz(90.f + 20.f) * T({0, 0.f, 0.04f});
      p.draw(M.box, wood, b * S({0.006f, 0.34f, 0.006f}));
      break;
    }
    case InstrumentId::Cello: {
      glm::mat4 c = hips * T({0, 0.05f, 0.42f}) * Rx(-12.f);
      p.draw(M.violinBody, wood, c * T({0, 0.22f, 0}) * S({1.45f, 2.3f, 1.6f}));
      p.draw(M.box, black, c * T({0, 0.95f, 0.04f}) * S({0.025f, 0.38f, 0.02f}));
      p.draw(M.cyl, silver, c * T({0, -0.35f, 0}) * S({0.008f, 0.3f, 0.008f}));
      glm::mat4 b = hand[1] * Rz(95.f) * T({0, 0.f, 0.04f});
      p.draw(M.box, wood, b * S({0.007f, 0.38f, 0.007f}));
      break;
    }
    case InstrumentId::Flute: {
      glm::mat4 f = head * T({0.32f, 0.05f, 0.24f}) * Rz(90.f) * Ry(8.f);
      p.draw(M.cyl, silver, f * S({0.013f, 0.62f, 0.013f}));
      break;
    }
    case InstrumentId::Trumpet: {
      glm::mat4 tr = head * T({0, 0.03f, 0.25f}) * Rx(90.f - imp * 4.f);
      p.draw(M.cyl, brass, tr * T({0, 0.22f, 0}) * S({0.018f, 0.45f, 0.018f}));
      p.draw(M.bell, brass, tr * T({0, 0.35f, 0}) * S({0.22f, 0.25f, 0.22f}));
      p.draw(M.box, brass, tr * T({0.05f, 0.18f, 0}) * S({0.035f, 0.07f, 0.03f}));  // valves
      break;
    }
    case InstrumentId::Timpani: {
      for (int s = -1; s <= 1; s += 2) {
        glm::mat4 d = root * T({s * 0.48f, 0.f, 0.62f});
        p.draw(M.bowl, copper, d * T({0, 0.28f, 0}) * S({0.8f, 1.2f, 0.8f}));
        p.draw(M.cyl, skinHead, d * T({0, 0.88f, 0}) * S({0.41f, 0.01f, 0.41f}));
      }
      for (int i = 0; i < 2; ++i) {
        glm::mat4 m = hand[i] * Rx(80.f);
        p.draw(M.cyl, wood, m * T({0, 0.16f, 0}) * S({0.01f, 0.32f, 0.01f}));
        p.draw(M.sphere, Material::velvet({0.85f, 0.82f, 0.75f}, 0.4f), m * T({0, 0.33f, 0}) * S(glm::vec3(0.035f)));
      }
      break;
    }
    default: break;
  }
}

}  // namespace og

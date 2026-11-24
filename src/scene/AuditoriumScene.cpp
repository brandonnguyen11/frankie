// Procedural Palais Garnier-style auditorium.
//
// Layout (metres, +Z toward the back of the house, stage at -Z):
//   stage deck top y=1.1 for z in [-14,-1]; proscenium opening |x|<7.3 at z=-3.2
//   orchestra pit floor y=-0.5 for z in [-1,3.2]
//   stalls rake from y=0 at z=3.5 to y=1.95 at z=21
//   horseshoe centred C=(0,5): tier fronts r=11.5, back wall r=15, tiers y=4.5/8/11.5
//   dome over the house, chandelier at (0,12.8,5.5)
#include "scene/Primitives.h"
#include "lighting/LightingRig.h"
#include "instruments/Orchestra.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace og {
namespace {

constexpr float kPi = 3.14159265f;
const glm::vec2 kC{0.f, 5.f};         // horseshoe centre (x,z)
const glm::vec3 kStageFocus{0, 0, -5};
constexpr float kTierFront = 11.5f, kTierBack = 15.f;
constexpr float kArcEnd = 2.15f;      // radians from +Z; tiers stop at the proscenium boxes
const float kTiers[3] = {4.5f, 8.f, 11.5f};

glm::mat4 T(glm::vec3 p) { return glm::translate(glm::mat4(1), p); }
glm::mat4 TRy(glm::vec3 p, float yawDeg) {
  return glm::rotate(T(p), glm::radians(yawDeg), {0, 1, 0});
}

float stallsHeight(float z) { return std::clamp((z - 3.5f) * 0.11f, 0.f, 1.95f); }

float yawToward(glm::vec3 from, glm::vec3 to) {
  glm::vec3 d = to - from;
  return glm::degrees(std::atan2(d.x, d.z));
}

// Theatre seat facing +Z: velvet cushion/back, dark wood + gold frame.
void buildSeat(MeshBuilder& velvet, MeshBuilder& frame) {
  velvet.box(T({0, 0.44f, 0.02f}), {0.25f, 0.06f, 0.22f});
  velvet.box(glm::rotate(T({0, 0.78f, -0.2f}), glm::radians(-10.f), {1, 0, 0}), {0.25f, 0.3f, 0.06f});
  for (float x : {-0.29f, 0.29f}) {
    frame.box(T({x, 0.33f, 0.f}), {0.035f, 0.33f, 0.24f});
    frame.box(T({x, 0.67f, 0.02f}), {0.045f, 0.025f, 0.26f});
  }
  frame.box(T({0, 1.09f, -0.25f}), {0.26f, 0.03f, 0.05f});
}

// Pleated drape hanging in the XY plane at z, folds along X.
MeshPtr pleatedCurtain(float x0, float x1, float y0, float y1, float z, float amp, float wavelength,
                       bool swagBottom = false) {
  MeshBuilder b;
  int nx = std::max(8, (int)((x1 - x0) / wavelength * 10.f));
  int ny = 24;
  for (int j = 0; j <= ny; ++j) {
    float fy = (float)j / ny;
    for (int i = 0; i <= nx; ++i) {
      float fx = (float)i / nx;
      float x = x0 + (x1 - x0) * fx;
      float y = y0 + (y1 - y0) * fy;
      float ph = 2.f * kPi * (x - x0) / wavelength;
      // folds deepen toward the top where the fabric is gathered
      float a = amp * (0.75f + 0.25f * fy);
      float zz = z + a * std::sin(ph);
      if (swagBottom) y += 0.25f * std::sin(kPi * fx) * (1.f - fy) * -1.f;
      float dzdx = a * std::cos(ph) * 2.f * kPi / wavelength;
      glm::vec3 n = glm::normalize(glm::vec3(-dzdx, 0, 1));
      b.v.push_back({{x, y, zz}, n, {x * 2.f, y * 2.f}, glm::vec3(1)});
    }
  }
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      unsigned a = j * (nx + 1) + i, c = a + nx + 1;
      b.idx.insert(b.idx.end(), {a, a + 1, c + 1, a, c + 1, c});
    }
  return b.build();
}

}  // namespace

SceneData buildAuditorium(std::vector<SeatSpot>* audienceSeats) {
  SceneData s;
  const Material redVelvet = Material::velvet({0.42f, 0.025f, 0.03f}, 0.85f);
  const Material wallDamask = Material::velvet({0.30f, 0.03f, 0.035f}, 0.5f);
  const Material gold = Material::gold(0.6f);
  const Material darkWood = Material::wood({0.11f, 0.05f, 0.03f}, 1.f);
  const Material stageDeck = Material::wood({0.16f, 0.09f, 0.05f}, 1.f);
  const Material black = Material::standard({0.012f, 0.012f, 0.014f}, 0.9f);
  const Material cream = Material::standard({0.62f, 0.52f, 0.38f}, 0.7f);
  const Material carpet = Material::velvet({0.22f, 0.02f, 0.025f}, 0.4f);

  // ---------------------------------------------------------------- floors
  {
    MeshBuilder b;
    // stalls rake as one ramp quad from z=3.2 to z=21
    float z0 = 3.2f, z1 = 21.f, y1 = stallsHeight(z1);
    b.v = {{{-15, 0, z0}, {0, 1, 0}, {0, 0}}, {{-15, y1, z1}, {0, 1, 0}, {0, 18}},
           {{15, y1, z1}, {0, 1, 0}, {30, 18}}, {{15, 0, z0}, {0, 1, 0}, {30, 0}}};
    glm::vec3 n = glm::normalize(glm::cross(glm::vec3(0, y1, z1 - z0), glm::vec3(1, 0, 0)));
    for (auto& v : b.v) v.normal = n;
    b.idx = {0, 1, 2, 0, 2, 3};
    b.plane(T({0, 1.95f, 22.f}), 30.f, 2.f);  // landing behind the last row
    s.add(b.build(), carpet);
  }
  // orchestra pit floor + walls
  s.add(makeBox({9.f, 0.5f, 2.1f}), darkWood, T({0, -1.0f, 1.1f}));
  {
    MeshBuilder rail;
    rail.box(T({0, -0.03f, 3.3f}), {9.2f, 0.47f, 0.1f});
    s.add(rail.build(), darkWood);
    MeshBuilder railTop;
    railTop.box(T({0, 0.47f, 3.3f}), {9.25f, 0.04f, 0.14f});
    s.add(railTop.build(), gold);
  }
  // stage deck, apron face
  s.add(makeBox({11.f, 0.8f, 6.5f}), stageDeck, T({0, 0.3f, -7.5f}));
  s.add(makeBox({11.f, 0.8f, 0.05f}), black, T({0, 0.3f, -0.98f}));

  // ---------------------------------------------------------------- stage house
  {
    MeshBuilder b;
    b.box(T({-11.2f, 9.f, -8.6f}), {0.2f, 8.f, 5.4f});
    b.box(T({11.2f, 9.f, -8.6f}), {0.2f, 8.f, 5.4f});
    b.box(T({0, 9.f, -14.2f}), {11.4f, 8.f, 0.2f});
    b.box(T({0, 16.5f, -8.6f}), {11.4f, 0.2f, 5.6f});
    s.add(b.build(), black);
    // borders (black masking) hanging above the stage hide the electrics
    MeshBuilder borders;
    for (float z : {-5.2f, -9.f, -12.5f}) borders.box(T({0, 11.f, z}), {10.5f, 1.2f, 0.05f});
    s.add(borders.build(), black);
    // painted backdrop: dusk sky scrim, very dim so beams read against it
    s.add(makeBox({10.5f, 6.f, 0.05f}), Material::standard({0.03f, 0.04f, 0.08f}, 0.95f), T({0, 7.1f, -13.6f}));
  }

  // ---------------------------------------------------------------- proscenium
  {
    MeshBuilder w;
    w.box(T({-11.15f, 9.75f, -3.35f}), {3.85f, 9.75f, 0.15f});
    w.box(T({11.15f, 9.75f, -3.35f}), {3.85f, 9.75f, 0.15f});
    w.box(T({0, 14.75f, -3.35f}), {7.3f, 4.75f, 0.15f});
    s.add(w.build(), wallDamask);

    MeshBuilder g;  // gilded frame
    for (float x : {-7.65f, 7.65f}) {
      g.box(T({x, 4.9f, -3.1f}), {0.35f, 4.9f, 0.3f});
      g.box(T({x, 0.25f, -3.05f}), {0.5f, 0.25f, 0.4f});   // plinth
      g.box(T({x, 9.65f, -3.05f}), {0.5f, 0.2f, 0.4f});    // capital
      g.cylinder(glm::rotate(T({x, 5.f, -2.75f}), 0.f, {0, 1, 0}), 0.16f, 0.16f, 8.4f, 16);  // column
    }
    g.box(T({0, 10.1f, -3.05f}), {8.15f, 0.35f, 0.35f});   // header
    g.box(T({0, 10.65f, -3.0f}), {8.4f, 0.2f, 0.45f});     // cornice
    g.box(T({0, 9.55f, -3.1f}), {7.3f, 0.08f, 0.25f});     // inner moulding
    // cartouche (lyre crest) above the arch
    g.lathe(glm::rotate(T({0, 11.7f, -2.9f}), glm::radians(90.f), {1, 0, 0}),
            {{0.f, -0.3f}, {0.9f, -0.25f}, {1.1f, 0.f}, {0.9f, 0.25f}, {0.f, 0.3f}}, 32);
    s.add(g.build(), gold);
  }
  // main curtain: open, gathered to the sides, plus pleated valance with gold fringe
  s.add(pleatedCurtain(-7.3f, -4.6f, 1.1f, 9.5f, -3.7f, 0.16f, 0.32f), redVelvet, glm::mat4(1), true);
  s.add(pleatedCurtain(4.6f, 7.3f, 1.1f, 9.5f, -3.7f, 0.16f, 0.32f), redVelvet, glm::mat4(1), true);
  s.add(pleatedCurtain(-7.3f, 7.3f, 8.3f, 9.5f, -3.55f, 0.1f, 0.6f, true), redVelvet, glm::mat4(1), true);
  s.add(makeBox({7.3f, 0.06f, 0.06f}), gold, T({0, 8.05f, -3.5f}));

  // proscenium boxes: fill between tiers and proscenium wall
  for (float sx : {-1.f, 1.f}) {
    MeshBuilder pw;
    pw.box(T({sx * 11.2f, 8.f, -2.25f}), {1.4f, 8.f, 1.0f});
    s.add(pw.build(), wallDamask);
    MeshBuilder pg;
    for (float y : kTiers) {
      pg.box(T({sx * 9.82f, y + 0.25f, -2.25f}), {0.06f, 0.6f, 1.0f});
      pg.box(T({sx * 9.82f, y - 2.f, -2.25f}), {0.08f, 0.08f, 1.0f});
    }
    pg.box(T({sx * 9.82f, 8.f, -1.27f}), {0.12f, 8.f, 0.12f});
    s.add(pg.build(), gold);
  }

  // ---------------------------------------------------------------- horseshoe
  {
    MeshBuilder wall;
    wall.arcWall(kC.x, kC.y, kTierBack, -kArcEnd, kArcEnd, 0.f, 15.6f, 96, true);
    s.add(wall.build(), wallDamask);
  }
  for (int t = 0; t < 3; ++t) {
    float y = kTiers[t];
    MeshBuilder slab;
    slab.arcSlab(kC.x, kC.y, kTierFront, kTierBack, -kArcEnd, kArcEnd, y, 0.35f, 72);
    slab.arcSlab(kC.x, kC.y, 12.9f, kTierBack, -kArcEnd, kArcEnd, y + 0.35f, 0.35f, 72);  // raised back row
    s.add(slab.build(), carpet);
    // soffit under each tier: cream plaster
    MeshBuilder soffit;
    soffit.arcSlab(kC.x, kC.y, kTierFront, kTierBack, -kArcEnd, kArcEnd, y - 0.36f, 0.02f, 72);
    s.add(soffit.build(), cream);
    // gilded parapet + velvet rail
    MeshBuilder front;
    front.arcSlab(kC.x, kC.y, kTierFront - 0.12f, kTierFront, -kArcEnd, kArcEnd, y + 0.9f, 1.4f, 96);
    front.arcSlab(kC.x, kC.y, kTierFront - 0.22f, kTierFront + 0.05f, -kArcEnd, kArcEnd, y - 0.42f, 0.12f, 96);
    s.add(front.build(), gold);
    MeshBuilder rail;
    rail.arcSlab(kC.x, kC.y, kTierFront - 0.18f, kTierFront + 0.12f, -kArcEnd, kArcEnd, y + 1.02f, 0.12f, 96);
    s.add(rail.build(), redVelvet);
    // loge dividers
    MeshBuilder div;
    for (float a = -kArcEnd + 0.18f; a < kArcEnd; a += 0.3f) {
      glm::vec3 p{kC.x + std::sin(a) * 13.25f, y + 1.3f, kC.y + std::cos(a) * 13.25f};
      div.box(glm::rotate(T(p), a, {0, 1, 0}), {0.04f, 1.3f, 1.7f});
    }
    s.add(div.build(), wallDamask);
    // warm practical sconces along the parapet
    MeshBuilder bulbs, arms;
    for (float a = -kArcEnd + 0.12f; a < kArcEnd - 0.05f; a += 0.3f) {
      glm::vec3 p{kC.x + std::sin(a) * (kTierFront - 0.3f), y + 0.45f, kC.y + std::cos(a) * (kTierFront - 0.3f)};
      bulbs.sphere(T(p), 0.055f, 10, 8);
      glm::vec3 base{kC.x + std::sin(a) * (kTierFront - 0.14f), y + 0.4f, kC.y + std::cos(a) * (kTierFront - 0.14f)};
      arms.box(glm::rotate(T((p + base) * 0.5f - glm::vec3(0, 0.03f, 0)), a, {0, 1, 0}), {0.015f, 0.015f, 0.09f});
    }
    s.add(bulbs.build(), Material::emissiveLight(kelvinToLinearRGB(2400.f), 9000.f), glm::mat4(1), false,
          LightGroup::House);
    s.add(arms.build(), gold, glm::mat4(1), false);
  }
  // gilded columns between tiers at the tier front
  {
    MeshBuilder cols;
    for (float a0 = 0.45f; a0 < kArcEnd; a0 += 0.6f)
      for (float a : {-a0, a0}) {
      glm::vec3 p{kC.x + std::sin(a) * (kTierFront + 0.25f), 0.f, kC.y + std::cos(a) * (kTierFront + 0.25f)};
      for (int t = 0; t < 3; ++t) {
        float y0 = t == 0 ? 0.f : kTiers[t - 1] + 1.0f;
        float y1 = kTiers[t] - 0.45f;
        cols.cylinder(T({p.x, (y0 + y1) * 0.5f, p.z}), 0.13f, 0.11f, y1 - y0, 14);
      }
    }
    s.add(cols.build(), gold);
  }

  // ---------------------------------------------------------------- ceiling dome + chandelier
  {
    MeshBuilder dome;
    dome.lathe(T({kC.x, 0, kC.y}),
               {{15.2f, 15.4f}, {14.8f, 16.6f}, {13.4f, 17.6f}, {10.6f, 18.4f}, {6.f, 18.9f}, {0.f, 19.1f}}, 72);
    Material ceiling = Material::standard({0.55f, 0.42f, 0.3f}, 0.8f);
    s.add(dome.build(), ceiling, glm::mat4(1), false);
    MeshBuilder ring;
    ring.arcSlab(kC.x, kC.y, 14.2f, 15.2f, -kPi, kPi, 15.75f, 0.5f, 96);
    ring.arcSlab(kC.x, kC.y, 2.6f, 3.2f, -kPi, kPi, 19.05f, 0.25f, 48);
    s.add(ring.build(), gold, glm::mat4(1), false);
  }
  {
    glm::vec3 c{0.f, 12.8f, 5.5f};
    MeshBuilder frame;
    frame.cylinder(T(c + glm::vec3(0, 3.2f, 0)), 0.05f, 0.05f, 6.4f, 8);  // chain to dome
    frame.lathe(T(c), {{0.f, -1.3f}, {0.35f, -1.1f}, {0.5f, -0.6f}, {0.3f, -0.1f}, {0.55f, 0.4f},
                       {0.25f, 1.0f}, {0.12f, 1.6f}, {0.f, 1.8f}}, 32);
    const float ringR[3] = {2.3f, 1.65f, 1.0f};
    const float ringY[3] = {-0.25f, 0.45f, 1.1f};
    const int ringN[3] = {28, 20, 12};
    MeshBuilder bulbs, crystals;
    for (int r = 0; r < 3; ++r) {
      frame.arcSlab(c.x, c.z, ringR[r] - 0.06f, ringR[r] + 0.06f, -kPi, kPi, c.y + ringY[r], 0.07f, 48);
      for (int i = 0; i < ringN[r]; ++i) {
        float a = (float)i / ringN[r] * 2.f * kPi;
        glm::vec3 p = c + glm::vec3(std::sin(a) * ringR[r], ringY[r] + 0.14f, std::cos(a) * ringR[r]);
        bulbs.sphere(T(p), 0.07f, 10, 8);
        // drops hang between bulbs
        float a2 = a + kPi / ringN[r];
        glm::vec3 q = c + glm::vec3(std::sin(a2) * ringR[r], ringY[r] - 0.22f, std::cos(a2) * ringR[r]);
        crystals.sphere(glm::scale(T(q), {0.7f, 1.6f, 0.7f}), 0.06f, 8, 6);
      }
      // arms from the stem to the ring
      for (int i = 0; i < 8; ++i) {
        float a = (float)i / 8.f * 2.f * kPi + r * 0.2f;
        glm::vec3 p = c + glm::vec3(std::sin(a) * ringR[r] * 0.5f, ringY[r], std::cos(a) * ringR[r] * 0.5f);
        frame.box(glm::rotate(T(p), a, {0, 1, 0}), {0.025f, 0.025f, ringR[r] * 0.5f});
      }
    }
    s.add(frame.build(), gold, glm::mat4(1), false);
    s.add(bulbs.build(), Material::emissiveLight(kelvinToLinearRGB(2700.f), 30000.f), glm::mat4(1), false,
          LightGroup::Chandelier);
    Material crystal = Material::standard({0.9f, 0.92f, 0.95f}, 0.04f);
    crystal.clearcoat = 1.f;
    s.add(crystals.build(), crystal, glm::mat4(1), false);
    s.points.push_back({c, kelvinToLinearRGB(2700.f), 22000.f, 40.f, LightGroup::Chandelier});
  }

  // ---------------------------------------------------------------- seating (instanced)
  MeshBuilder seatVelvet, seatFrame;
  buildSeat(seatVelvet, seatFrame);
  std::vector<glm::mat4> seatXf;
  auto addSeat = [&](glm::vec3 p, float yaw) {
    seatXf.push_back(TRy(p, yaw));
    if (audienceSeats) audienceSeats->push_back({p, yaw});
  };
  // stalls: concentric rows around the stage focus
  for (int row = 0; row < 14; ++row) {
    float r = 9.6f + row * 1.05f;
    float step = 0.64f / r;
    for (float a = -1.2f; a <= 1.2f; a += step) {
      glm::vec3 p{kStageFocus.x + std::sin(a) * r, 0, kStageFocus.z + std::cos(a) * r};
      if (std::abs(p.x) < 0.55f) continue;  // centre aisle
      if (std::abs(p.x) > 8.8f) continue;   // side aisles
      if (glm::length(glm::vec2(p.x, p.z) - kC) > kTierBack - 0.8f) continue;
      p.y = stallsHeight(p.z);
      addSeat(p, yawToward(p, kStageFocus));
    }
  }
  // balconies: two rows per tier
  for (int t = 0; t < 3; ++t) {
    for (int row = 0; row < 2; ++row) {
      float r = row == 0 ? 12.3f : 13.6f;
      float y = kTiers[t] + (row == 0 ? 0.f : 0.35f);
      for (float a = -kArcEnd + 0.07f; a < kArcEnd - 0.05f; a += 0.62f / r) {
        // leave a gap at each loge divider
        float m = std::fmod(a + kArcEnd - 0.18f + 100.f * 0.3f, 0.3f);
        if (m < 0.03f || m > 0.27f) continue;
        glm::vec3 p{kC.x + std::sin(a) * r, y, kC.y + std::cos(a) * r};
        addSeat(p, yawToward(p, kStageFocus));
      }
    }
  }
  {
    auto v = seatVelvet.build();
    v->setInstances(seatXf);
    s.add(v, redVelvet);
    auto f = seatFrame.build();
    f->setInstances(seatXf);
    s.add(f, gold);
  }

  // ---------------------------------------------------------------- orchestra pit furniture
  {
    MeshBuilder stands, chairs, lamps;
    for (const auto& seat : orchestraLayout()) {
      glm::vec3 p = seat.position;
      chairs.box(T(p + glm::vec3(0, 0.45f, 0)), {0.22f, 0.04f, 0.2f});
      chairs.box(T(p + glm::vec3(0, 0.75f, -0.2f)), {0.22f, 0.28f, 0.03f});
      for (float x : {-0.18f, 0.18f})
        for (float z : {-0.16f, 0.16f}) chairs.box(T(p + glm::vec3(x, 0.22f, z)), {0.02f, 0.22f, 0.02f});
      if (seat.instrument == InstrumentId::Piano || seat.instrument == InstrumentId::Timpani) continue;
      glm::vec3 st = p + glm::vec3(0.f, 0.f, 0.85f);
      stands.cylinder(T(st + glm::vec3(0, 0.42f, 0)), 0.015f, 0.015f, 0.84f, 6);
      stands.box(glm::rotate(T(st + glm::vec3(0, 0.86f, 0)), glm::radians(-35.f), {1, 0, 0}), {0.25f, 0.16f, 0.01f});
      lamps.box(T(st + glm::vec3(0, 1.0f, -0.08f)), {0.12f, 0.015f, 0.03f});
    }
    s.add(chairs.build(), Material::standard({0.03f, 0.03f, 0.03f}, 0.4f));
    s.add(stands.build(), Material::standard({0.05f, 0.05f, 0.05f}, 0.35f, 1.f));
    s.add(lamps.build(), Material::emissiveLight(kelvinToLinearRGB(3000.f), 6000.f), glm::mat4(1), false);
    s.points.push_back({{-3.5f, 0.9f, 1.4f}, kelvinToLinearRGB(3000.f), 140.f, 5.f});
    s.points.push_back({{3.5f, 0.9f, 1.4f}, kelvinToLinearRGB(3000.f), 140.f, 5.f});
  }
  // grand piano in front of the piano seat (the frog plays it from behind, facing the house)
  {
    glm::vec3 p = orchestraLayout()[0].position;
    MeshBuilder body;
    body.box(T(p + glm::vec3(0, 0.82f, 1.15f)), {0.75f, 0.14f, 0.85f});
    body.box(glm::rotate(T(p + glm::vec3(0.0f, 1.25f, 1.45f)), glm::radians(-35.f), {1, 0, 0}), {0.72f, 0.01f, 0.55f});
    for (float x : {-0.65f, 0.65f}) body.box(T(p + glm::vec3(x, 0.38f, 0.5f)), {0.05f, 0.38f, 0.05f});
    body.box(T(p + glm::vec3(0, 0.38f, 1.8f)), {0.05f, 0.38f, 0.05f});
    Material lacquer = Material::standard({0.01f, 0.01f, 0.012f}, 0.12f);
    lacquer.clearcoat = 1.f;
    s.add(body.build(), lacquer);
    MeshBuilder keys;
    keys.box(T(p + glm::vec3(0, 0.75f, 0.35f)), {0.7f, 0.02f, 0.08f});
    s.add(keys.build(), Material::standard({0.85f, 0.83f, 0.78f}, 0.3f));
  }

  // ---------------------------------------------------------------- house lights (dimmed by houseLights)
  for (int t = 0; t < 3; ++t)
    for (float sx : {-1.f, 1.f}) {
      float a = sx * 1.2f;
      glm::vec3 p{kC.x + std::sin(a) * 10.5f, kTiers[t] + 0.6f, kC.y + std::cos(a) * 10.5f};
      s.points.push_back({p, kelvinToLinearRGB(2500.f), 2600.f, 16.f, LightGroup::House});
    }
  s.points.push_back({{0.f, 15.f, 14.f}, kelvinToLinearRGB(2700.f), 4000.f, 22.f, LightGroup::House});
  return s;
}

}  // namespace og

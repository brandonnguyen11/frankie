// Procedural 1950s-70s city block with a Beaux-Arts Garnier facade.
//
// Layout: facade front at z=0 (x in [-20,20]); terrace y=1.2 for z in [0,1];
// eight steps down to the plaza (y=0) at z=5; plaza to z=32; avenue z 34..44;
// storefront row from z=46; side streets/buildings beyond |x|=28.
#include "scene/Primitives.h"
#include "lighting/LightingRig.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace og {
namespace {

constexpr float kPi = 3.14159265f;
glm::mat4 T(glm::vec3 p) { return glm::translate(glm::mat4(1), p); }
glm::mat4 TRy(glm::vec3 p, float deg) { return glm::rotate(T(p), glm::radians(deg), {0, 1, 0}); }

// deterministic hash for variety
float h1(float n) { float x = std::sin(n * 127.1f) * 43758.5453f; return x - std::floor(x); }

// Gilded allegorical figure stand-in: plinth + robed body + raised wings/lyre.
void gildedFigure(MeshBuilder& b, glm::vec3 p, float s) {
  b.lathe(T(p), {{0.f, 0.f}, {0.55f * s, 0.f}, {0.5f * s, 0.25f * s}, {0.38f * s, 0.9f * s}, {0.5f * s, 1.5f * s},
                 {0.42f * s, 2.0f * s}, {0.2f * s, 2.2f * s}, {0.24f * s, 2.5f * s}, {0.f, 2.65f * s}}, 20);
  for (float sx : {-1.f, 1.f}) {
    glm::mat4 w = glm::rotate(T(p + glm::vec3(sx * 0.45f * s, 2.1f * s, -0.1f * s)), sx * glm::radians(35.f), {0, 0, 1});
    b.box(w, {0.08f * s, 0.75f * s, 0.32f * s});
  }
}

void addStreetlamp(SceneData& s, MeshBuilder& metal, MeshBuilder& globes, glm::vec3 base) {
  metal.cylinder(T(base + glm::vec3(0, 0.25f, 0)), 0.22f, 0.18f, 0.5f, 12);
  metal.cylinder(T(base + glm::vec3(0, 2.2f, 0)), 0.09f, 0.06f, 3.6f, 10);
  metal.box(T(base + glm::vec3(0, 4.05f, 0)), {0.18f, 0.04f, 0.18f});
  metal.lathe(T(base + glm::vec3(0, 4.55f, 0)), {{0.0f, 0.42f}, {0.26f, 0.34f}, {0.05f, 0.25f}}, 12);
  globes.lathe(T(base + glm::vec3(0, 4.1f, 0)), {{0.f, 0.f}, {0.16f, 0.02f}, {0.21f, 0.18f}, {0.18f, 0.3f}, {0.f, 0.32f}}, 14);
  s.points.push_back({base + glm::vec3(0, 4.3f, 0), kelvinToLinearRGB(2600.f), 700.f, 16.f});
}

struct NeonSpec { glm::vec3 color; float nits; };

// Mid-century storefront: shop glass, awning, neon, lit upper windows.
void storefront(SceneData& s, glm::vec3 base, float w, float h, float depth, float yaw, glm::vec3 wall, int seed,
                MeshBuilder& windowsLit, MeshBuilder& windowsDark, MeshBuilder& glassShop) {
  glm::mat4 X = TRy(base, yaw);  // local: front face at z=0 facing +z, x across, y up
  MeshBuilder body;
  body.box(X * T({0, h * 0.5f, -depth * 0.5f}), {w * 0.5f, h * 0.5f, depth * 0.5f});
  body.box(X * T({0, h + 0.15f, 0.1f - depth * 0.5f}), {w * 0.5f + 0.15f, 0.15f, depth * 0.5f + 0.1f});  // cornice
  Material wm = Material::standard(wall, 0.8f);
  s.add(body.build(), wm, glm::mat4(1), false);  // backdrop: don't shadow the plaza

  // shop window band
  glassShop.box(X * T({0, 1.6f, 0.02f}), {w * 0.5f - 0.6f, 1.25f, 0.02f});
  MeshBuilder trim;
  trim.box(X * T({0, 3.0f, 0.06f}), {w * 0.5f - 0.4f, 0.12f, 0.06f});
  trim.box(X * T({0, 0.15f, 0.06f}), {w * 0.5f - 0.4f, 0.15f, 0.06f});
  s.add(trim.build(), Material::standard({0.8f, 0.8f, 0.82f}, 0.2f, 1.f));  // chrome trim

  // striped awning
  glm::vec3 awn[] = {{0.75f, 0.12f, 0.1f}, {0.1f, 0.45f, 0.42f}, {0.85f, 0.6f, 0.15f}, {0.15f, 0.25f, 0.6f}};
  glm::vec3 ac = awn[seed % 4];
  MeshBuilder a1, a2;
  int stripes = std::max(4, (int)(w / 0.6f));
  for (int i = 0; i < stripes; ++i) {
    float x = -w * 0.5f + 0.5f + (w - 1.f) * (i + 0.5f) / stripes;
    MeshBuilder& b = (i % 2) ? a1 : a2;
    b.box(X * glm::rotate(T({x, 3.45f, 0.75f}), glm::radians(25.f), {1, 0, 0}),
          {(w - 1.f) * 0.5f / stripes, 0.03f, 0.8f});
  }
  s.add(a1.build(), Material::velvet(ac, 0.3f));
  s.add(a2.build(), Material::velvet({0.85f, 0.82f, 0.75f}, 0.3f));

  // neon sign: a ring + bar "script" in a period colour
  NeonSpec neon[] = {{{1.f, 0.15f, 0.45f}, 3500.f}, {{0.15f, 0.9f, 1.f}, 3000.f}, {{1.f, 0.35f, 0.08f}, 3500.f},
                     {{0.4f, 1.f, 0.3f}, 2500.f}, {{1.f, 0.85f, 0.3f}, 3000.f}};
  NeonSpec n = neon[seed % 5];
  if (seed % 3 != 2) {
    MeshBuilder tube;
    float y = std::min(h - 1.2f, 4.6f + h1(seed) * 1.2f);
    float len = std::min(w * 0.35f, 3.f);
    tube.box(X * T({0, y, 0.18f}), {len, 0.045f, 0.045f});
    tube.box(X * T({0, y + 0.55f, 0.18f}), {len * 0.75f, 0.045f, 0.045f});
    tube.lathe(X * glm::rotate(T({-len - 0.55f, y + 0.27f, 0.18f}), glm::radians(90.f), {1, 0, 0}),
               {{0.42f, -0.04f}, {0.46f, 0.f}, {0.42f, 0.04f}, {0.38f, 0.f}, {0.42f, -0.04f}}, 24);
    s.add(tube.build(), Material::emissiveLight(n.color, n.nits), glm::mat4(1), false);
    MeshBuilder backer;
    backer.box(X * T({0, y + 0.27f, 0.08f}), {len + 1.1f, 0.6f, 0.04f});
    s.add(backer.build(), Material::standard({0.02f, 0.02f, 0.025f}, 0.6f));
    glm::vec3 p = glm::vec3(X * glm::vec4(0, y + 0.3f, 1.2f, 1));
    s.points.push_back({p, n.color, 120.f, 9.f});
  }

  // upper windows
  int floors = std::max(1, (int)((h - 4.2f) / 3.f));
  int cols = std::max(1, (int)((w - 1.f) / 1.8f));
  for (int f = 0; f < floors; ++f)
    for (int c = 0; c < cols; ++c) {
      float x = -w * 0.5f + 0.5f + (w - 1.f) * (c + 0.5f) / cols;
      float y = 5.2f + f * 3.f;
      bool lit = h1(seed * 31.f + f * 7.f + c) > 0.55f;
      (lit ? windowsLit : windowsDark).box(X * T({x, y, 0.02f}), {0.5f, 0.8f, 0.02f});
    }
}

// 1950s sedan, ~5.2 m long, facing +X in local space.
void classicCar(SceneData& s, glm::vec3 p, float yaw, glm::vec3 paint, bool twoTone) {
  glm::mat4 X = TRy(p, yaw);
  MeshBuilder body, roof, chrome, tyres, white, glass, lamps;
  body.box(X * T({0, 0.72f, 0}), {2.6f, 0.32f, 0.98f});
  body.box(X * T({1.7f, 0.98f, 0}), {0.9f, 0.08f, 0.94f});    // hood crown
  body.box(X * T({-1.8f, 0.98f, 0}), {0.8f, 0.08f, 0.94f});   // trunk deck
  for (float sz : {-1.f, 1.f}) {
    body.box(X * glm::rotate(T({-2.25f, 1.12f, sz * 0.82f}), glm::radians(-12.f), {0, 0, 1}), {0.45f, 0.12f, 0.05f});  // fins
    for (float sx : {-1.55f, 1.6f}) body.cylinder(X * glm::rotate(T({sx, 0.62f, sz * 0.88f}), kPi / 2, {1, 0, 0}), 0.48f, 0.48f, 0.22f, 20);
  }
  roof.box(X * T({-0.2f, 1.42f, 0}), {1.05f, 0.05f, 0.82f});
  glass.box(X * T({-0.2f, 1.2f, 0}), {1.15f, 0.2f, 0.86f});
  chrome.box(X * T({2.68f, 0.5f, 0}), {0.08f, 0.1f, 1.0f});
  chrome.box(X * T({-2.68f, 0.5f, 0}), {0.08f, 0.1f, 1.0f});
  chrome.box(X * T({2.62f, 0.78f, 0}), {0.04f, 0.14f, 0.6f});  // grille
  chrome.box(X * T({0, 0.82f, 0.99f}), {2.4f, 0.02f, 0.01f});  // side spear
  chrome.box(X * T({0, 0.82f, -0.99f}), {2.4f, 0.02f, 0.01f});
  for (float sx : {-1.55f, 1.6f})
    for (float sz : {-0.86f, 0.86f}) {
      tyres.cylinder(X * glm::rotate(T({sx, 0.36f, sz}), kPi / 2, {1, 0, 0}), 0.36f, 0.36f, 0.24f, 20);
      white.cylinder(X * glm::rotate(T({sx, 0.36f, sz * 1.14f}), kPi / 2, {1, 0, 0}), 0.24f, 0.24f, 0.02f, 20);
    }
  for (float sz : {-0.7f, 0.7f}) lamps.sphere(X * T({2.62f, 0.82f, sz}), 0.12f, 10, 8);
  Material paintM = Material::standard(paint, 0.3f);
  paintM.clearcoat = 1.f;
  s.add(body.build(), paintM);
  Material roofM = paintM;
  if (twoTone) roofM.albedo = {0.85f, 0.83f, 0.76f};
  s.add(roof.build(), roofM);
  Material g = Material::standard({0.02f, 0.025f, 0.03f}, 0.05f);
  g.clearcoat = 1.f;
  s.add(glass.build(), g);
  s.add(chrome.build(), Material::standard({0.9f, 0.9f, 0.92f}, 0.12f, 1.f));
  s.add(tyres.build(), Material::standard({0.02f, 0.02f, 0.02f}, 0.85f));
  s.add(white.build(), Material::standard({0.8f, 0.8f, 0.78f}, 0.6f));
  s.add(lamps.build(), Material::emissiveLight({1.f, 0.9f, 0.7f}, 400.f), glm::mat4(1), false);
}

void tree(MeshBuilder& trunk, MeshBuilder& leaves, glm::vec3 p, float s) {
  trunk.cylinder(T(p + glm::vec3(0, 1.8f * s, 0)), 0.22f * s, 0.16f * s, 3.6f * s, 10);
  for (int i = 0; i < 6; ++i) {
    float a = i * 1.7f;
    glm::vec3 o{std::cos(a) * 1.1f * s, 4.4f * s + h1(i + p.x) * 1.2f * s, std::sin(a) * 1.1f * s};
    leaves.sphere(T(p + o), (1.5f + h1(i * 3.f + p.z)) * s, 12, 9);
  }
}

}  // namespace

SceneData buildCity() {
  SceneData s;
  Material stone = Material::standard({0.70f, 0.63f, 0.50f}, 0.75f);
  stone.type = MatType::Stone;
  const Material stoneDark = Material::standard({0.48f, 0.42f, 0.33f}, 0.8f);
  const Material gold = Material::gold(0.4f);
  const Material patina = Material::standard({0.22f, 0.42f, 0.34f}, 0.55f, 0.25f);
  const Material bronze = Material::standard({0.06f, 0.09f, 0.07f}, 0.45f, 0.8f);
  Material marbleCol = Material::marble({0.42f, 0.24f, 0.22f});
  Material paving;
  paving.albedo = {0.42f, 0.4f, 0.37f};
  paving.roughness = 0.7f;
  paving.type = MatType::Paving;
  Material asphalt = Material::standard({0.035f, 0.035f, 0.04f}, 0.6f);

  // ---------------------------------------------------------------- sky + ground
  s.add(makeSphere(450.f, 32, 16), [] { Material m; m.type = MatType::Sky; return m; }(), glm::mat4(1), false);
  {
    MeshBuilder g;
    g.plane(T({0, -0.01f, 40.f}), 260.f, 220.f, 0.5f);
    s.add(g.build(), asphalt, glm::mat4(1), false);
    MeshBuilder p;
    p.plane(T({0, 0.f, 18.5f}), 56.f, 27.f, 1.f);         // plaza flagstones
    p.plane(T({0, 0.f, 56.f}), 120.f, 20.f, 1.f);        // far sidewalk
    for (float sx : {-1.f, 1.f}) p.plane(T({sx * 43.f, 0.f, 20.f}), 14.f, 30.f, 1.f);
    s.add(p.build(), paving, glm::mat4(1), true);
    MeshBuilder curb;
    curb.box(T({0, 0.07f, 32.1f}), {28.f, 0.08f, 0.12f});
    curb.box(T({0, 0.07f, 45.9f}), {60.f, 0.08f, 0.12f});
    s.add(curb.build(), stoneDark);
    // lane markings
    MeshBuilder lanes;
    for (float x = -60.f; x < 60.f; x += 6.f) lanes.box(T({x, 0.005f, 39.f}), {1.5f, 0.005f, 0.08f});
    s.add(lanes.build(), Material::standard({0.75f, 0.7f, 0.5f}, 0.6f), glm::mat4(1), false);
  }

  // ---------------------------------------------------------------- Garnier facade
  {
    MeshBuilder steps;
    for (int i = 0; i < 8; ++i) {
      float z = 5.f - (i + 0.5f) * 0.5f;
      steps.box(T({0, (i + 1) * 0.075f, z}), {21.f, (i + 1) * 0.075f, 0.25f});
    }
    steps.box(T({0, 0.6f, 0.5f}), {21.f, 0.6f, 0.5f});  // terrace
    s.add(steps.build(), stone);
  }
  {
    MeshBuilder body;
    body.box(T({0, 13.f, -16.5f}), {20.f, 13.f, 15.5f});                 // main mass behind facade
    const float bays[] = {-15.f, -10.f, -5.f, 0.f, 5.f, 10.f, 15.f};
    // piers of the ground arcade
    for (float x : {-20.f, -12.5f, -7.5f, -2.5f, 2.5f, 7.5f, 12.5f, 20.f}) {
      bool end = std::abs(x) == 20.f;
      float hw = end ? 2.5f : 0.8f;
      float cx = end ? (x > 0 ? 19.2f : -19.2f) : x;  // end piers close the gap to bay ±15
      body.box(T({cx, 5.35f, -0.5f}), {hw, 4.15f, 0.5f});
    }
    // spandrels with real semicircular arches above each bay + mezzanine band
    for (float x : bays) body.archSpandrel(x, 6.2f, 1.7f, 9.5f, 0.f, 0.98f, 24);
    body.box(T({0, 9.9f, -0.4f}), {20.3f, 0.4f, 0.6f});
    // loggia back wall, entablature, attic, side pavilions
    body.box(T({0, 15.2f, -2.6f}), {20.f, 4.9f, 0.3f});
    body.box(T({0, 10.65f, -1.2f}), {20.2f, 0.35f, 1.4f});               // loggia floor
    body.box(T({0, 20.6f, -1.2f}), {20.4f, 0.6f, 1.5f});                 // entablature
    body.box(T({0, 21.5f, -1.0f}), {20.7f, 0.3f, 1.7f});                 // cornice
    body.box(T({0, 24.f, -2.f}), {19.5f, 2.2f, 1.f});                    // attic
    body.box(T({0, 26.35f, -1.9f}), {19.8f, 0.2f, 1.2f});
    for (float sx : {-1.f, 1.f}) body.box(T({sx * 18.5f, 15.f, -0.6f}), {2.f, 5.2f, 0.8f});
    s.add(body.build(), stone);

    // lit lobby glimpsed through every arch; the centre bay holds the entrance doors
    MeshBuilder lobby, doors, transoms;
    for (float x : bays) {
      lobby.box(T({x, 4.55f, -0.99f}), {1.72f, 3.4f, 0.02f});
      transoms.box(T({x, 6.2f, -0.9f}), {1.7f, 0.06f, 0.06f});
      transoms.box(T({x, 6.2f + 0.85f, -0.9f}), {0.04f, 0.85f, 0.04f});
      if (x == 0.f) {
        doors.box(T({-0.8f, 3.1f, -0.9f}), {0.75f, 1.9f, 0.05f});
        doors.box(T({0.8f, 3.1f, -0.9f}), {0.75f, 1.9f, 0.05f});
      }
    }
    s.add(transoms.build(), Material::standard({0.08f, 0.06f, 0.03f}, 0.4f, 0.8f));
    Material lobbyGlow;
    lobbyGlow.type = MatType::Glass;
    lobbyGlow.albedo = {0.04f, 0.04f, 0.04f};
    lobbyGlow.roughness = 0.05f;
    lobbyGlow.emissive = kelvinToLinearRGB(2600.f);
    lobbyGlow.emissiveNits = 1400.f;
    s.add(lobby.build(), lobbyGlow, glm::mat4(1), false);
    Material doorM = Material::wood({0.18f, 0.07f, 0.03f}, 2.f);
    s.add(doors.build(), doorM);
    s.points.push_back({{0, 4.f, 1.5f}, kelvinToLinearRGB(2600.f), 260.f, 9.f});

    // loggia: paired monolithic columns with gilded capitals, lit windows behind
    MeshBuilder cols, caps, windows;
    for (float x : {-12.5f, -7.5f, -2.5f, 2.5f, 7.5f, 12.5f, -17.f, 17.f})
      for (float dx : {-0.55f, 0.55f}) {
        cols.cylinder(T({x + dx, 15.3f, -0.4f}), 0.36f, 0.32f, 8.6f, 18);
        caps.box(T({x + dx, 19.85f, -0.4f}), {0.45f, 0.25f, 0.45f});
        caps.box(T({x + dx, 11.0f, -0.4f}), {0.45f, 0.18f, 0.45f});
      }
    for (float x : bays) windows.box(T({x, 15.f, -2.28f}), {1.3f, 3.2f, 0.02f});
    s.add(cols.build(), marbleCol);
    s.add(caps.build(), gold);
    Material win = lobbyGlow;
    win.emissiveNits = 520.f;
    s.add(windows.build(), win, glm::mat4(1), false);
    // gilded busts in oculi between the paired columns
    MeshBuilder oculi, busts;
    for (float x : {-12.5f, -7.5f, -2.5f, 2.5f, 7.5f, 12.5f}) {
      oculi.cylinder(glm::rotate(T({x, 18.1f, -0.15f}), kPi / 2, {1, 0, 0}), 0.6f, 0.6f, 0.05f, 24);
      busts.sphere(glm::scale(T({x, 18.1f, 0.05f}), {1.f, 1.25f, 0.8f}), 0.35f, 14, 10);
    }
    s.add(oculi.build(), Material::standard({0.05f, 0.08f, 0.06f}, 0.5f, 0.5f));
    s.add(busts.build(), gold);
    // gilded frieze band + attic figures
    MeshBuilder frieze;
    frieze.box(T({0, 20.65f, 0.31f}), {16.f, 0.18f, 0.02f});
    gildedFigure(frieze, {-18.5f, 26.55f, -1.5f}, 1.4f);
    gildedFigure(frieze, {18.5f, 26.55f, -1.5f}, 1.4f);
    s.add(frieze.build(), gold);
  }
  // dome + drum + stage-house roof
  {
    MeshBuilder drum;
    drum.cylinder(T({0, 28.5f, -17.f}), 11.5f, 11.5f, 4.f, 48);
    s.add(drum.build(), stone);
    MeshBuilder dome;
    dome.lathe(T({0, 30.5f, -17.f}), {{11.8f, 0.f}, {11.2f, 2.5f}, {9.4f, 5.f}, {6.2f, 7.f}, {2.6f, 8.2f}, {0.f, 8.4f}}, 48);
    s.add(dome.build(), patina);
    MeshBuilder crown;
    gildedFigure(crown, {0, 38.8f, -17.f}, 1.2f);
    crown.arcSlab(0, -17.f, 11.6f, 12.f, -kPi, kPi, 30.6f, 0.3f, 64);
    s.add(crown.build(), gold);
    MeshBuilder stageHouse;
    stageHouse.box(T({0, 19.f, -42.f}), {16.f, 19.f, 10.f});
    s.add(stageHouse.build(), stone);
    MeshBuilder roof;
    for (float sx : {-1.f, 1.f})
      roof.box(glm::rotate(T({sx * 8.f, 42.f, -42.f}), sx * glm::radians(-27.f), {0, 0, 1}), {9.f, 0.3f, 10.2f});
    s.add(roof.build(), patina);
    MeshBuilder pegasus;
    gildedFigure(pegasus, {-15.f, 38.f, -32.5f}, 1.3f);
    gildedFigure(pegasus, {15.f, 38.f, -32.5f}, 1.3f);
    gildedFigure(pegasus, {0, 46.f, -32.5f}, 1.4f);
    s.add(pegasus.build(), gold);
  }
  // rostral lamp clusters on the terrace (bronze + five globes)
  {
    MeshBuilder metal, globes;
    for (float x : {-14.f, -6.f, 6.f, 14.f}) {
      glm::vec3 b{x, 1.2f, 2.2f};
      metal.box(T(b + glm::vec3(0, 0.6f, 0)), {0.45f, 0.6f, 0.45f});
      metal.cylinder(T(b + glm::vec3(0, 2.6f, 0)), 0.16f, 0.12f, 2.8f, 12);
      for (int i = 0; i < 5; ++i) {
        glm::vec3 g = i == 0 ? glm::vec3(0, 4.35f, 0)
                             : glm::vec3(std::cos(i * kPi / 2) * 0.55f, 3.9f, std::sin(i * kPi / 2) * 0.55f);
        if (i) metal.box(T(b + g * glm::vec3(0.5f, 1, 0.5f) + glm::vec3(0, -0.1f, 0)), {0.3f, 0.03f, 0.03f});
        globes.sphere(T(b + g), 0.24f, 14, 10);
      }
      s.points.push_back({b + glm::vec3(0, 4.1f, 0.4f), kelvinToLinearRGB(2700.f), 900.f, 18.f});
    }
    s.add(metal.build(), bronze);
    s.add(globes.build(), Material::emissiveLight(kelvinToLinearRGB(2700.f), 9000.f), glm::mat4(1), false);
  }

  // ---------------------------------------------------------------- streetlamps, trees, kiosks
  {
    MeshBuilder metal, globes, trunks, leaves;
    for (float x = -25.f; x <= 25.f; x += 12.5f) addStreetlamp(s, metal, globes, {x, 0, 31.5f});
    for (float sx : {-1.f, 1.f})
      for (float z : {12.f, 24.f}) addStreetlamp(s, metal, globes, {sx * 27.5f, 0, z});
    s.add(metal.build(), Material::standard({0.03f, 0.06f, 0.045f}, 0.5f, 0.6f));
    s.add(globes.build(), Material::emissiveLight(kelvinToLinearRGB(2600.f), 7000.f), glm::mat4(1), false);
    for (float x : {-31.f, -18.f, 18.f, 31.f}) tree(trunks, leaves, {x, 0, 30.5f}, 1.f);
    for (float x : {-44.f, -22.f, 0.f, 22.f, 44.f}) tree(trunks, leaves, {x + 6.f, 0, 47.5f}, 0.9f);
    s.add(trunks.build(), Material::standard({0.12f, 0.1f, 0.08f}, 0.9f));
    s.add(leaves.build(), Material::velvet({0.05f, 0.09f, 0.03f}, 0.25f));
    // Morris advertising columns
    MeshBuilder col, posters, cap;
    for (float x : {-22.f, 22.f}) {
      col.cylinder(T({x, 1.6f, 27.f}), 0.75f, 0.75f, 3.2f, 24);
      cap.lathe(T({x, 3.2f, 27.f}), {{0.85f, 0.f}, {0.9f, 0.15f}, {0.6f, 0.5f}, {0.2f, 0.9f}, {0.f, 1.1f}}, 24);
      posters.cylinder(T({x, 1.9f, 27.f}), 0.77f, 0.77f, 1.6f, 24, false);
    }
    s.add(col.build(), Material::standard({0.05f, 0.18f, 0.12f}, 0.6f));
    s.add(cap.build(), Material::standard({0.05f, 0.18f, 0.12f}, 0.5f, 0.5f));
    s.add(posters.build(), Material::standard({0.75f, 0.45f, 0.2f}, 0.8f));
  }

  // ---------------------------------------------------------------- storefront rows
  {
    MeshBuilder lit, dark, glass;
    glm::vec3 walls[] = {{0.75f, 0.55f, 0.25f}, {0.25f, 0.5f, 0.48f}, {0.78f, 0.45f, 0.38f}, {0.82f, 0.78f, 0.65f},
                         {0.5f, 0.22f, 0.15f}, {0.55f, 0.6f, 0.5f}, {0.35f, 0.3f, 0.45f}};
    float x = -58.f;
    int seed = 1;
    while (x < 58.f) {
      float w = 9.f + h1(seed) * 6.f;
      float h = 9.f + h1(seed * 2.f) * 10.f;
      storefront(s, {x + w * 0.5f, 0, 46.5f}, w - 0.3f, h, 12.f, 180.f, walls[seed % 7], seed, lit, dark, glass);
      x += w;
      ++seed;
    }
    for (float sx : {-1.f, 1.f}) {
      float z = 4.f;
      while (z < 34.f) {
        float w = 8.f + h1(seed) * 4.f;
        if (z + w > 34.f) w = 34.f - z;
        if (w < 4.f) break;
        float h = 10.f + h1(seed * 3.f) * 8.f;
        storefront(s, {sx * 36.5f, 0, z + w * 0.5f}, w - 0.3f, h, 10.f, sx > 0 ? -90.f : 90.f, walls[seed % 7], seed,
                   lit, dark, glass);
        z += w;
        ++seed;
      }
    }
    Material wl;
    wl.type = MatType::Glass;
    wl.albedo = {0.03f, 0.03f, 0.03f};
    wl.roughness = 0.08f;
    wl.emissive = kelvinToLinearRGB(2900.f);
    wl.emissiveNits = 70.f;
    s.add(lit.build(), wl, glm::mat4(1), false);
    Material wd = wl;
    wd.emissiveNits = 0.f;
    s.add(dark.build(), wd, glm::mat4(1), false);
    Material shop = wl;
    shop.emissiveNits = 260.f;
    shop.emissive = kelvinToLinearRGB(3400.f);
    s.add(glass.build(), shop, glm::mat4(1), false);
  }

  // ---------------------------------------------------------------- period cars
  classicCar(s, {-16.f, 0, 35.6f}, 0.f, {0.1f, 0.55f, 0.55f}, true);
  classicCar(s, {-6.f, 0, 35.6f}, 0.f, {0.6f, 0.04f, 0.05f}, false);
  classicCar(s, {9.f, 0, 35.6f}, 0.f, {0.85f, 0.78f, 0.55f}, true);
  classicCar(s, {22.f, 0, 42.4f}, 180.f, {0.02f, 0.02f, 0.025f}, false);
  classicCar(s, {-28.f, 0, 42.4f}, 180.f, {0.45f, 0.7f, 0.55f}, true);
  return s;
}

}  // namespace og

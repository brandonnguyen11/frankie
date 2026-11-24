// Frame graph:
//   shadow maps (8 spot layers / sun) -> depth+normal prepass -> SSAO (half res)
//   -> forward PBR lighting (HDR) -> raymarched beams (+blur, additive)
//   -> thin-lens DOF (film camera) -> bloom mip chain -> exposure + AgX
//   -> FXAA -> final RGBA8 target -> present / async readback for recording.
#include "render/Renderer.h"
#include "core/AssetPath.h"
#include "scene/World.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace og {

namespace {

constexpr int kMaxLights = 32;
constexpr int kBloomLevels = 6;

struct Frustum {
  glm::vec4 planes[6];
  explicit Frustum(const glm::mat4& m) {
    glm::mat4 t = glm::transpose(m);
    planes[0] = t[3] + t[0];
    planes[1] = t[3] - t[0];
    planes[2] = t[3] + t[1];
    planes[3] = t[3] - t[1];
    planes[4] = t[3] + t[2];
    planes[5] = t[3] - t[2];
    for (auto& p : planes) p /= glm::length(glm::vec3(p));
  }
  bool sphereVisible(const glm::vec3& c, float r) const {
    for (auto& p : planes)
      if (glm::dot(glm::vec3(p), c) + p.w < -r) return false;
    return true;
  }
};

struct EnvParams {
  bool outdoor = false;
  glm::vec3 sunDir{0, 1, 0};
  glm::vec3 sunColor{0.f};
  glm::vec3 skyAmbient{0.f}, groundAmbient{0.f};
  float skyBrightness = 1.f, dusk = 1.f;
  glm::vec3 fogColor{0.f};
  float fogDensity = 0.f;
  float ev100 = 8.f;
  glm::vec3 ambientScatter{0.f};
  glm::vec3 lobePos[2] = {glm::vec3(0.f), glm::vec3(0.f)};
  glm::vec3 lobeColor[2] = {glm::vec3(0.f), glm::vec3(0.f)};
};

float emissiveGroupScale(const World& w, LightGroup g) {
  if (w.outdoors()) return 1.f;
  switch (g) {
    case LightGroup::House: return w.rig.houseLights;
    case LightGroup::Chandelier: return w.rig.chandelier;
    default: return 1.f;
  }
}

EnvParams environment(const World& w) {
  EnvParams e;
  e.outdoor = w.outdoors();
  if (e.outdoor) {
    float t = std::clamp(w.timeOfDay, 0.f, 1.f);
    float el = glm::radians(glm::mix(38.f, -1.f, t));
    float az = glm::radians(35.f);
    e.sunDir = glm::normalize(glm::vec3(std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el)));
    float lux = 70000.f * std::pow(0.012f, t) * glm::smoothstep(-0.02f, 0.035f, el);
    e.sunColor = kelvinToLinearRGB(glm::mix(5400.f, 2300.f, t)) * lux;
    float sky = 4000.f * std::pow(0.008f, t);
    e.dusk = glm::smoothstep(0.35f, 0.85f, t);
    e.skyBrightness = sky / 124.f;
    glm::vec3 skyTint = glm::mix(glm::vec3(0.55f, 0.7f, 1.0f), glm::vec3(0.45f, 0.5f, 0.85f), e.dusk);
    e.skyAmbient = skyTint * sky;
    e.groundAmbient = glm::mix(glm::vec3(0.5f, 0.45f, 0.4f), glm::vec3(0.55f, 0.35f, 0.25f), e.dusk) *
                      (sky * 0.22f + lux * 0.012f);
    e.fogColor = glm::mix(glm::vec3(0.75f, 0.82f, 0.95f) * 330.f, glm::vec3(1.0f, 0.52f, 0.26f) * 320.f, e.dusk) *
                 e.skyBrightness * 0.55f;
    e.fogDensity = 0.0022f + 0.002f * e.dusk;
    e.ev100 = std::log2(std::max(sky * 1.2f + lux * 0.04f, 4.f) * 100.f / 12.5f) - 1.f;
  } else {
    const auto& rig = w.rig;
    float spill = 0.f;
    for (auto& L : rig.lights)
      if (L.enabled) spill += L.intensityKcd;
    spill = spill * 1000.f / 400.f * 0.02f;  // stage light bounced into the house
    float house = rig.houseLights, chand = rig.chandelier;
    glm::vec3 warm = kelvinToLinearRGB(2600.f);
    e.skyAmbient = warm * (0.5f + 32.f * house + 5.f * chand) + glm::vec3(1.0f, 0.85f, 0.7f) * spill * 0.06f;
    e.groundAmbient = glm::vec3(0.8f, 0.25f, 0.18f) * (0.35f + 14.f * house + 2.f * chand) +
                      glm::vec3(0.9f, 0.6f, 0.4f) * spill * 0.05f;
    e.fogColor = e.skyAmbient * 0.25f;
    e.fogDensity = 0.0015f + rig.haze * 0.004f;
    e.ev100 = w.houseEV;
    e.ambientScatter = e.skyAmbient * 0.12f * rig.haze;
    e.lobePos[0] = {0.f, 12.8f, 5.5f};
    e.lobeColor[0] = kelvinToLinearRGB(2700.f) * (2500.f * chand + 300.f * house);
    e.lobePos[1] = {0.f, 3.f, -6.f};
    e.lobeColor[1] = glm::vec3(1.0f, 0.85f, 0.7f) * spill * 1.2f;
    e.sunDir = {0, 1, 0};
  }
  return e;
}

}  // namespace

bool Renderer::init() {
  bool ok = true;
  auto sp = [](const char* f) { return shaderPath(f); };
  ok &= pbr_.load(sp("scene.vert"), sp("pbr.frag"));
  ok &= prepass_.load(sp("scene.vert"), sp("prepass.frag"));
  ok &= shadow_.load(sp("shadow.vert"), sp("shadow.frag"));
  ok &= ssao_.load(sp("fullscreen.vert"), sp("ssao.frag"));
  ok &= blur_.load(sp("fullscreen.vert"), sp("blur.frag"));
  ok &= volume_.load(sp("fullscreen.vert"), sp("volume.frag"));
  ok &= bloomDown_.load(sp("fullscreen.vert"), sp("bloom_down.frag"));
  ok &= bloomUp_.load(sp("fullscreen.vert"), sp("bloom_up.frag"));
  ok &= dof_.load(sp("fullscreen.vert"), sp("dof.frag"));
  ok &= composite_.load(sp("fullscreen.vert"), sp("composite.frag"));
  ok &= fxaa_.load(sp("fullscreen.vert"), sp("fxaa.frag"));
  ok &= blit_.load(sp("fullscreen.vert"), sp("blit.frag"));
  if (!ok) return false;

  glGenVertexArrays(1, &emptyVao_);
  auto solid = [](unsigned char v) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    unsigned char px[4] = {v, v, v, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
  };
  whiteTex_ = solid(255);
  blackTex_ = solid(0);

  if (!spotShadows_.create(1024, 8)) return false;
  if (!sunShadow_.create(4096)) return false;

  std::mt19937 rng(7);
  std::uniform_real_distribution<float> U(0.f, 1.f);
  for (int i = 0; i < 16; ++i) {
    glm::vec3 s{U(rng) * 2 - 1, U(rng) * 2 - 1, U(rng)};
    s = glm::normalize(s) * U(rng);
    float k = (float)i / 16.f;
    s *= glm::mix(0.1f, 1.f, k * k);
    ssaoKernel_.push_back(glm::vec4(s, 0));
  }

  // stage-light fixture: barrel along +Z with a yoke, plus an emissive lens
  MeshBuilder body;
  body.cylinder(glm::rotate(glm::mat4(1), glm::radians(90.f), {1, 0, 0}), 0.16f, 0.13f, 0.55f, 16);
  body.box(glm::translate(glm::mat4(1), {0, 0.19f, -0.05f}), {0.2f, 0.02f, 0.05f});
  for (float x : {-0.19f, 0.19f}) body.box(glm::translate(glm::mat4(1), {x, 0.07f, -0.05f}), {0.015f, 0.13f, 0.04f});
  fixtureBody_ = body.build();
  MeshBuilder lens;
  lens.cylinder(glm::translate(glm::mat4(1), {0, 0, 0.28f}) * glm::rotate(glm::mat4(1), glm::radians(90.f), {1, 0, 0}),
                0.125f, 0.125f, 0.01f, 16);
  fixtureLens_ = lens.build();
  mountMarker_ = makeSphere(0.12f, 10, 8);

  glGenBuffers(kPbo, pbo_);
  return true;
}

void Renderer::shutdown() {
  if (emptyVao_) glDeleteVertexArrays(1, &emptyVao_);
  if (whiteTex_) glDeleteTextures(1, &whiteTex_);
  if (blackTex_) glDeleteTextures(1, &blackTex_);
  glDeleteBuffers(kPbo, pbo_);
  emptyVao_ = whiteTex_ = blackTex_ = 0;
  fixtureBody_.reset();
  fixtureLens_.reset();
  mountMarker_.reset();
}

void Renderer::allocate(int w, int h, int volScale) {
  if (w == w_ && h == h_ && volScale == volScale_ && gbuf_.fbo) return;
  w_ = w;
  h_ = h;
  volScale_ = volScale;
  gbuf_.create(w, h, {GL_RGBA16F}, true);
  hdr_.create(w, h, {GL_RGBA16F}, false, gbuf_.depth);
  hdr2_.create(w, h, {GL_RGBA16F}, false);
  ssaoRaw_.create(std::max(1, w / 2), std::max(1, h / 2), {GL_R8}, false);
  ssaoBlur_.create(std::max(1, w / 2), std::max(1, h / 2), {GL_R8}, false);
  int vw = std::max(1, w / volScale), vh = std::max(1, h / volScale);
  vol_.create(vw, vh, {GL_RGBA16F}, false);
  volBlur_.create(vw, vh, {GL_RGBA16F}, false);
  bloom_.clear();
  bloom_.resize(kBloomLevels);
  int bw = w, bh = h;
  for (int i = 0; i < kBloomLevels; ++i) {
    bw = std::max(1, bw / 2);
    bh = std::max(1, bh / 2);
    bloom_[i].create(bw, bh, {GL_R11F_G11F_B10F}, false);
  }
  ldr_.create(w, h, {GL_RGBA8}, false);
  final_.create(w, h, {GL_RGBA8}, false);
}

void Renderer::drawFullscreen() {
  glBindVertexArray(emptyVao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
}

glm::mat4 Renderer::spotMatrix(const glm::vec3& pos, const glm::vec3& dir, float cosOuter, float range) const {
  float fov = 2.f * std::acos(std::clamp(cosOuter, -1.f, 1.f)) + glm::radians(4.f);
  fov = std::min(fov, glm::radians(150.f));
  glm::vec3 up = std::abs(dir.y) > 0.95f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
  return glm::perspective(fov, 1.f, 0.15f, range) * glm::lookAt(pos, pos + dir, up);
}

void Renderer::setMaterial(const Shader& sh, const SceneObject& o, World& world) {
  const Material& m = o.mat;
  sh.setInt("uMatType", (int)m.type);
  sh.setVec3("uAlbedo", m.albedo);
  sh.setFloat("uRoughness", m.roughness);
  sh.setFloat("uMetalness", m.metalness);
  sh.setVec3("uEmissive", m.emissive * m.emissiveNits * emissiveGroupScale(world, o.group));
  sh.setFloat("uSheen", m.sheen);
  sh.setFloat("uAnisotropy", m.anisotropy);
  sh.setFloat("uTranslucency", m.translucency);
  sh.setFloat("uClearcoat", m.clearcoat);
  sh.setFloat("uUVScale", m.uvScale);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m.albedoTex ? m.albedoTex : whiteTex_);
  sh.setInt("uHasAlbedoTex", m.albedoTex ? 1 : 0);
}

void Renderer::drawObjects(const Shader& sh, const std::vector<const SceneObject*>& objs, bool shading,
                           const glm::mat4& viewProj, bool cull, World& world) {
  Frustum fr(viewProj);
  for (const SceneObject* o : objs) {
    if (!o->mesh) continue;
    bool inst = o->mesh->instanceCount() > 0;
    if (cull && !inst) {
      glm::vec3 c = glm::vec3(o->model * glm::vec4((o->mesh->boundsMin + o->mesh->boundsMax) * 0.5f, 1));
      float s = std::max({glm::length(glm::vec3(o->model[0])), glm::length(glm::vec3(o->model[1])),
                          glm::length(glm::vec3(o->model[2]))});
      float r = glm::length(o->mesh->boundsMax - o->mesh->boundsMin) * 0.5f * s;
      if (!fr.sphereVisible(c, r)) { ++stats_.culled; continue; }
    }
    sh.setMat4("uModel", o->model);
    sh.setInt("uInstanced", inst ? 1 : 0);
    if (shading) {
      sh.setMat3("uNormalMat", glm::transpose(glm::inverse(glm::mat3(o->model))));
      setMaterial(sh, *o, world);
    } else {
      sh.setMat3("uNormalMat", glm::transpose(glm::inverse(glm::mat3(o->model))));
    }
    o->mesh->draw();
    ++stats_.drawCalls;
  }
}

void Renderer::render(World& world, const CameraView& cam, int w, int h) {
  stats_ = {};
  ++frame_;
  int volScale = (world.quality == Quality::High) ? 1 : 2;
  allocate(w, h, volScale);
  EnvParams env = environment(world);
  bool outdoor = env.outdoor;

  // ---------------------------------------------------------------- draw lists
  std::vector<SceneObject> dyn;
  world.collectDraw(dyn);
  if (!outdoor) {
    // visible lighting fixtures (lens glows with the light's output)
    for (size_t i = 0; i < world.rig.lights.size(); ++i) {
      const auto& L = world.rig.lights[i];
      glm::vec3 z = glm::normalize(L.direction);
      glm::vec3 x = glm::normalize(glm::cross(std::abs(z.y) > 0.95f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0), z));
      glm::vec3 y = glm::cross(z, x);
      glm::mat4 m(glm::vec4(x, 0), glm::vec4(y, 0), glm::vec4(z, 0), glm::vec4(L.position, 1));
      SceneObject b;
      b.mesh = fixtureBody_;
      b.mat = Material::standard({0.02f, 0.02f, 0.02f}, 0.45f, 0.6f);
      b.model = m;
      b.castsShadow = false;
      dyn.push_back(b);
      SceneObject lens;
      lens.mesh = fixtureLens_;
      float nits = L.enabled ? 40.f * L.intensityKcd : 0.f;
      lens.mat = Material::emissiveLight(L.color(), nits);
      if (world.localPlayer.role == Role::LightingDesigner && (int)i == world.rig.selected)
        lens.mat = Material::emissiveLight({0.2f, 1.f, 0.4f}, 4000.f);
      lens.model = m;
      lens.castsShadow = false;
      dyn.push_back(lens);
    }
    if (world.localPlayer.role == Role::LightingDesigner && world.lightView != LightView::FromLight) {
      for (auto& mp : world.rig.mounts) {
        SceneObject mk;
        mk.mesh = mountMarker_;
        mk.mat = Material::emissiveLight({0.3f, 0.6f, 1.f}, 600.f);
        mk.model = glm::translate(glm::mat4(1), mp.position);
        mk.castsShadow = false;
        dyn.push_back(mk);
      }
    }
  }
  std::vector<const SceneObject*> all;
  const SceneData& scene = world.activeScene();
  all.reserve(scene.objects.size() + dyn.size());
  for (auto& o : scene.objects) all.push_back(&o);
  for (auto& o : dyn) all.push_back(&o);
  std::vector<const SceneObject*> casters;
  for (auto* o : all)
    if (o->castsShadow) casters.push_back(o);

  // ---------------------------------------------------------------- lights
  GpuLight lights[kMaxLights];
  glm::mat4 shadowMats[8];
  int nl = 0, layers = 0;
  int maxShadow = world.quality == Quality::Low ? 4 : 8;
  struct ShadowJob { int layer; glm::mat4 m; };
  std::vector<ShadowJob> jobs;
  bool sunShadowed = false;
  glm::mat4 sunMat(1.f);
  if (outdoor) {
    if (glm::length(env.sunColor) > 1.f) {
      sunShadowed = true;
      glm::vec3 c{0, 5, 12};
      glm::mat4 v = glm::lookAt(c + env.sunDir * 120.f, c, glm::vec3(0, 1, 0));
      sunMat = glm::ortho(-62.f, 62.f, -62.f, 62.f, 1.f, 300.f) * v;
      lights[nl++] = {{0, 0, 0, 2}, {-env.sunDir, 0}, {env.sunColor, 8.f}, {0, 0, 0, 0}};
    }
  } else {
    for (auto& L : world.rig.lights) {
      if (!L.enabled || nl >= kMaxLights) continue;
      float layer = -1.f;
      glm::vec3 dir = glm::normalize(L.direction);
      if (L.castsShadow && layers < maxShadow) {
        shadowMats[layers] = spotMatrix(L.position, dir, L.cosOuter(), L.range());
        jobs.push_back({layers, shadowMats[layers]});
        layer = (float)layers++;
      }
      float tanO = std::tan(std::acos(L.cosOuter()));
      lights[nl++] = {{L.position, 0.f}, {dir, L.range()}, {L.color() * L.intensityKcd * 1000.f, layer},
                      {L.cosOuter(), L.cosInner(), (float)L.gobo, tanO}};
    }
  }
  for (auto& p : scene.points) {
    if (nl >= kMaxLights) break;
    float s = emissiveGroupScale(world, p.group);
    if (s * p.intensityCd < 1.f) continue;
    lights[nl++] = {{p.position, 1.f}, {0, -1, 0, p.radius}, {p.color * p.intensityCd * s, -1.f}, {0, 0, 0, 0}};
  }
  stats_.lights = nl;
  stats_.shadowMaps = (int)jobs.size() + (sunShadowed ? 1 : 0);

  auto uploadLights = [&](const Shader& sh) {
    glm::vec4 a[kMaxLights], b[kMaxLights], c[kMaxLights], d[kMaxLights];
    for (int i = 0; i < nl; ++i) { a[i] = lights[i].posType; b[i] = lights[i].dirRange; c[i] = lights[i].color; d[i] = lights[i].cone; }
    sh.setInt("uNumLights", nl);
    sh.setVec4Array("uLightPosType", a, nl);
    sh.setVec4Array("uLightDirRange", b, nl);
    sh.setVec4Array("uLightColor", c, nl);
    sh.setVec4Array("uLightCone", d, nl);
    sh.setMat4Array("uShadowMatrix", shadowMats, 8);
    sh.setMat4("uSunMatrix", sunMat);
    sh.setFloat("uShadowTexel", 1.f / spotShadows_.size);
    sh.setFloat("uSunTexel", 1.f / sunShadow_.size);
    sh.setFloat("uTime", world.time);
    sh.setInt("uShadowArray", 2);
    sh.setInt("uSunShadow", 3);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, spotShadows_.tex);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, sunShadow_.tex);
  };

  // ---------------------------------------------------------------- shadow passes
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);
  glDisable(GL_BLEND);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(1.6f, 3.f);
  shadow_.use();
  for (auto& j : jobs) {
    spotShadows_.bindLayer(j.layer);
    glClear(GL_DEPTH_BUFFER_BIT);
    shadow_.setMat4("uLightViewProj", j.m);
    drawObjects(shadow_, casters, false, j.m, true, world);
  }
  if (sunShadowed) {
    sunShadow_.bind();
    glClear(GL_DEPTH_BUFFER_BIT);
    shadow_.setMat4("uLightViewProj", sunMat);
    std::vector<const SceneObject*> sunCasters;
    for (auto* o : casters)
      if (o->mat.type != MatType::Sky) sunCasters.push_back(o);
    drawObjects(shadow_, sunCasters, false, sunMat, true, world);
  }
  glDisable(GL_POLYGON_OFFSET_FILL);

  // ---------------------------------------------------------------- prepass
  glm::mat4 viewProj = cam.proj * cam.view;
  gbuf_.bind();
  glClearColor(0.5f, 0.5f, 1.f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  prepass_.use();
  prepass_.setMat4("uViewProj", viewProj);
  prepass_.setMat4("uView", cam.view);
  drawObjects(prepass_, all, false, viewProj, true, world);

  // ---------------------------------------------------------------- SSAO
  GLuint aoTex = whiteTex_;
  if (world.quality != Quality::Low) {
    glDisable(GL_DEPTH_TEST);
    ssaoRaw_.bind();
    ssao_.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gbuf_.depth);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gbuf_.colors[0]);
    ssao_.setInt("uDepth", 0);
    ssao_.setInt("uNormal", 1);
    ssao_.setMat4("uProj", cam.proj);
    ssao_.setMat4("uInvProj", glm::inverse(cam.proj));
    ssao_.setVec4Array("uKernel", ssaoKernel_.data(), 16);
    ssao_.setFloat("uRadius", outdoor ? 0.9f : 0.6f);
    drawFullscreen();
    // two-pass depth-aware blur
    blur_.use();
    blur_.setInt("uSrc", 0);
    blur_.setInt("uDepth", 1);
    blur_.setFloat("uDepthSharp", 8.f);
    blur_.setFloat("uNear", cam.nearP);
    blur_.setFloat("uFar", cam.farP);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gbuf_.depth);
    ssaoBlur_.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ssaoRaw_.colors[0]);
    blur_.setVec2("uDir", {1.f / ssaoRaw_.width, 0.f});
    drawFullscreen();
    ssaoRaw_.bind();
    glBindTexture(GL_TEXTURE_2D, ssaoBlur_.colors[0]);
    blur_.setVec2("uDir", {0.f, 1.f / ssaoRaw_.height});
    drawFullscreen();
    aoTex = ssaoRaw_.colors[0];
    glEnable(GL_DEPTH_TEST);
  }

  // ---------------------------------------------------------------- forward lighting
  hdr_.bind();
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);
  pbr_.use();
  pbr_.setMat4("uViewProj", viewProj);
  pbr_.setVec3("uCamPos", cam.position);
  pbr_.setVec2("uViewport", {(float)w, (float)h});
  pbr_.setInt("uOutdoor", outdoor ? 1 : 0);
  pbr_.setVec3("uSkyAmbient", env.skyAmbient);
  pbr_.setVec3("uGroundAmbient", env.groundAmbient);
  pbr_.setVec3("uSunDir", env.sunDir);
  pbr_.setFloat("uSkyBrightness", env.skyBrightness);
  pbr_.setFloat("uDusk", env.dusk);
  pbr_.setVec3("uFogColor", env.fogColor);
  pbr_.setFloat("uFogDensity", env.fogDensity);
  pbr_.setVec3("uLobePos[0]", env.lobePos[0]);
  pbr_.setVec3("uLobePos[1]", env.lobePos[1]);
  pbr_.setVec3("uLobeColor[0]", env.lobeColor[0]);
  pbr_.setVec3("uLobeColor[1]", env.lobeColor[1]);
  pbr_.setInt("uAlbedoTex", 0);
  pbr_.setInt("uSSAO", 1);
  uploadLights(pbr_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, aoTex);
  drawObjects(pbr_, all, true, viewProj, true, world);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);

  // ---------------------------------------------------------------- volumetric beams
  glDisable(GL_DEPTH_TEST);
  if (!outdoor && world.rig.haze > 0.001f) {
    vol_.bind();
    glClear(GL_COLOR_BUFFER_BIT);
    volume_.use();
    uploadLights(volume_);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, gbuf_.depth);
    volume_.setInt("uDepth", 4);
    volume_.setMat4("uInvViewProj", glm::inverse(viewProj));
    volume_.setVec3("uCamPos", cam.position);
    volume_.setFloat("uHaze", world.rig.haze);
    volume_.setInt("uSteps", world.quality == Quality::Low ? 14 : 24);
    volume_.setFloat("uAnisoG", 0.35f);
    volume_.setVec3("uAmbientScatter", env.ambientScatter);
    volume_.setFloat("uMaxDist", 60.f);
    volume_.setInt("uFrame", frame_);
    drawFullscreen();
    blur_.use();
    blur_.setInt("uSrc", 0);
    blur_.setInt("uDepth", 1);
    blur_.setFloat("uDepthSharp", 2.f);
    blur_.setFloat("uNear", cam.nearP);
    blur_.setFloat("uFar", cam.farP);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gbuf_.depth);
    volBlur_.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, vol_.colors[0]);
    blur_.setVec2("uDir", {1.f / vol_.width, 0.f});
    drawFullscreen();
    vol_.bind();
    glBindTexture(GL_TEXTURE_2D, volBlur_.colors[0]);
    blur_.setVec2("uDir", {0.f, 1.f / vol_.height});
    drawFullscreen();
    // add into the HDR buffer
    hdr_.bind();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    blit_.use();
    blit_.setInt("uSrc", 0);
    glBindTexture(GL_TEXTURE_2D, vol_.colors[0]);
    drawFullscreen();
    glDisable(GL_BLEND);
  }

  // ---------------------------------------------------------------- depth of field
  GLuint sceneTex = hdr_.colors[0];
  if (cam.film && world.quality != Quality::Low) {
    hdr2_.bind();
    dof_.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdr_.colors[0]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gbuf_.depth);
    dof_.setInt("uScene", 0);
    dof_.setInt("uDepth", 1);
    dof_.setFloat("uFocalMm", cam.focalMm);
    dof_.setFloat("uFStop", cam.fStop);
    dof_.setFloat("uFocusDist", cam.focusDist);
    dof_.setFloat("uNear", cam.nearP);
    dof_.setFloat("uFar", cam.farP);
    dof_.setVec2("uTexel", {1.f / w, 1.f / h});
    dof_.setFloat("uMaxCoC", h / 1080.f * 22.f);
    drawFullscreen();
    sceneTex = hdr2_.colors[0];
  }

  // ---------------------------------------------------------------- bloom
  bloomDown_.use();
  bloomDown_.setInt("uSrc", 0);
  glActiveTexture(GL_TEXTURE0);
  GLuint src = sceneTex;
  int sw = w, sh = h;
  for (int i = 0; i < kBloomLevels; ++i) {
    bloom_[i].bind();
    glBindTexture(GL_TEXTURE_2D, src);
    bloomDown_.setVec2("uSrcTexel", {1.f / sw, 1.f / sh});
    bloomDown_.setInt("uFirst", i == 0 ? 1 : 0);
    drawFullscreen();
    src = bloom_[i].colors[0];
    sw = bloom_[i].width;
    sh = bloom_[i].height;
  }
  bloomUp_.use();
  bloomUp_.setInt("uSrc", 0);
  bloomUp_.setFloat("uRadius", 1.f);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  for (int i = kBloomLevels - 1; i > 0; --i) {
    bloom_[i - 1].bind();
    glBindTexture(GL_TEXTURE_2D, bloom_[i].colors[0]);
    bloomUp_.setVec2("uSrcTexel", {1.f / bloom_[i].width, 1.f / bloom_[i].height});
    drawFullscreen();
  }
  glDisable(GL_BLEND);

  // ---------------------------------------------------------------- composite + FXAA
  ldr_.bind();
  composite_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, sceneTex);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, bloom_[0].colors[0]);
  composite_.setInt("uScene", 0);
  composite_.setInt("uBloom", 1);
  float ev = env.ev100 + cam.evOffset;
  composite_.setFloat("uExposure", 1.f / (1.2f * std::pow(2.f, ev)));
  // bloom levels were summed (kBloomLevels contributions), normalise strength
  composite_.setFloat("uBloomStrength", (world.quality == Quality::Low ? 0.03f : 0.045f) / (float)kBloomLevels);
  composite_.setInt("uToneMap", world.toneMap);
  composite_.setInt("uFilmLook", cam.film ? 1 : 0);
  composite_.setFloat("uFade", world.fade);
  composite_.setFloat("uTime", world.time);
  drawFullscreen();

  final_.bind();
  fxaa_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, ldr_.colors[0]);
  fxaa_.setInt("uSrc", 0);
  fxaa_.setVec2("uTexel", {1.f / w, 1.f / h});
  fxaa_.setInt("uEnabled", world.fxaa ? 1 : 0);
  drawFullscreen();

  for (int u = 0; u < 5; ++u) {
    glActiveTexture(GL_TEXTURE0 + u);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  glActiveTexture(GL_TEXTURE0);
  glUseProgram(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::present(int screenW, int screenH) {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, screenW, screenH);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  if (!final_.fbo) return;
  float sa = (float)screenW / std::max(1, screenH), fa = (float)final_.width / std::max(1, final_.height);
  int vw = screenW, vh = screenH;
  if (sa > fa) vw = (int)(screenH * fa); else vh = (int)(screenW / fa);
  glViewport((screenW - vw) / 2, (screenH - vh) / 2, vw, vh);
  glDisable(GL_DEPTH_TEST);
  blit_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, final_.colors[0]);
  blit_.setInt("uSrc", 0);
  drawFullscreen();
  glViewport(0, 0, screenW, screenH);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::queueReadback() {
  int i = pboHead_;
  pboHead_ = (pboHead_ + 1) % kPbo;
  glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[i]);
  size_t bytes = (size_t)final_.width * final_.height * 4;
  if (pboW_[i] != final_.width || pboH_[i] != final_.height) {
    glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
    pboW_[i] = final_.width;
    pboH_[i] = final_.height;
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, final_.fbo);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, final_.width, final_.height, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  pboPending_[i] = true;
}

bool Renderer::fetchReadback(std::vector<unsigned char>& out, int& w, int& h) {
  // oldest pending buffer = the one that will be written next
  int i = pboHead_;
  if (!pboPending_[i]) return false;
  glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[i]);
  size_t bytes = (size_t)pboW_[i] * pboH_[i] * 4;
  void* p = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT);
  bool ok = p != nullptr;
  if (ok) {
    out.assign((unsigned char*)p, (unsigned char*)p + bytes);
    w = pboW_[i];
    h = pboH_[i];
    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
  }
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  pboPending_[i] = false;
  return ok;
}

void Renderer::flushReadbacks(std::vector<std::vector<unsigned char>>& out) {
  for (int k = 0; k < kPbo; ++k) {
    std::vector<unsigned char> f;
    int w, h;
    if (fetchReadback(f, w, h)) out.push_back(std::move(f));
    pboHead_ = (pboHead_ + 1) % kPbo;
  }
}

bool writePng(const std::string& path, int w, int h, const std::vector<unsigned char>& rgba) {
  stbi_flip_vertically_on_write(1);
  return stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
}

bool Renderer::saveScreenshot(const std::string& path) {
  if (!final_.fbo) return false;
  int w = final_.width, h = final_.height;
  std::vector<unsigned char> px((size_t)w * h * 4);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, final_.fbo);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  stbi_flip_vertically_on_write(1);
  return stbi_write_png(path.c_str(), w, h, 4, px.data(), w * 4) != 0;
}

}  // namespace og

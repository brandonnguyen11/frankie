#pragma once
#include "render/Framebuffer.h"
#include "render/Mesh.h"
#include "render/Shader.h"
#include "scene/Primitives.h"
#include <glm/glm.hpp>
#include <vector>

namespace og {

struct World;

struct CameraView {
  glm::mat4 view{1.f}, proj{1.f};
  glm::vec3 position{0.f};
  float nearP = 0.1f, farP = 600.f;
  bool film = false;        // film camera: DOF, grain, vignette
  float focalMm = 50.f, fStop = 2.8f, focusDist = 8.f;
  float evOffset = 0.f;
};

struct RenderStats {
  int drawCalls = 0;
  int culled = 0;
  int lights = 0;
  int shadowMaps = 0;
  float gpuMs = 0.f;
};

class Renderer {
 public:
  bool init();
  void shutdown();
  // Render the world into the internal final target of size w x h.
  void render(World& world, const CameraView& cam, int w, int h);
  // Draw the final target to the default framebuffer, letterboxed.
  void present(int screenW, int screenH);
  // Asynchronous readback (PBO ring). Returns true and fills `out` (BGRA,
  // bottom-up rows) with the frame queued `latency` renders ago.
  void queueReadback();
  bool fetchReadback(std::vector<unsigned char>& out, int& w, int& h);
  void flushReadbacks(std::vector<std::vector<unsigned char>>& out);
  // PNG screenshot of the final target.
  bool saveScreenshot(const std::string& path);

  const RenderStats& stats() const { return stats_; }

 private:
  struct GpuLight {
    glm::vec4 posType, dirRange, color, cone;
  };

  Shader pbr_, prepass_, shadow_, ssao_, blur_, volume_, bloomDown_, bloomUp_, dof_, composite_, fxaa_, blit_;
  GLuint emptyVao_ = 0, whiteTex_ = 0, blackTex_ = 0;
  int w_ = 0, h_ = 0;
  RenderTarget gbuf_;     // view normals + depth (owns depth)
  RenderTarget hdr_;      // lit HDR colour (shares depth)
  RenderTarget hdr2_;     // DOF output
  RenderTarget ssaoRaw_, ssaoBlur_;
  RenderTarget vol_, volBlur_;
  std::vector<RenderTarget> bloom_;
  RenderTarget ldr_, final_;
  ShadowArray spotShadows_;
  ShadowMap2D sunShadow_;
  std::vector<glm::vec4> ssaoKernel_;
  MeshPtr fixtureBody_, fixtureLens_, mountMarker_;
  int frame_ = 0;
  int volScale_ = 1;
  RenderStats stats_;

  // readback ring
  static constexpr int kPbo = 3;
  GLuint pbo_[kPbo] = {0, 0, 0};
  int pboW_[kPbo] = {0}, pboH_[kPbo] = {0};
  bool pboPending_[kPbo] = {false};
  int pboHead_ = 0;

  void allocate(int w, int h, int volScale);
  void drawFullscreen();
  void drawObjects(const Shader& sh, const std::vector<const SceneObject*>& objs, bool shading,
                   const glm::mat4& viewProj, bool cull, World& world);
  void setMaterial(const Shader& sh, const SceneObject& o, World& world);
  glm::mat4 spotMatrix(const glm::vec3& pos, const glm::vec3& dir, float cosOuter, float range) const;
};

}  // namespace og

#pragma once
#include <glad/gl.h>
#include <vector>

namespace og {

// Off-screen render target with N colour attachments and an optional depth
// texture (owned, or shared with another target so passes reuse one Z-buffer).
struct RenderTarget {
  GLuint fbo = 0;
  std::vector<GLuint> colors;
  GLuint depth = 0;
  bool ownsDepth = false;
  int width = 0, height = 0;

  bool create(int w, int h, const std::vector<GLenum>& formats, bool withDepth, GLuint sharedDepth = 0);
  void bind() const;
  void destroy();
  ~RenderTarget() { destroy(); }
};

// Depth texture array: one 1024^2 layer per shadow-casting spotlight.
// Sampled as sampler2DArrayShadow (hardware compare + PCF).
struct ShadowArray {
  GLuint fbo = 0, tex = 0;
  int size = 1024, layers = 8;
  bool create(int sz, int count);
  void bindLayer(int layer) const;
  void destroy();
  ~ShadowArray() { destroy(); }
};

// Single depth map for the directional sun on the exterior.
struct ShadowMap2D {
  GLuint fbo = 0, tex = 0;
  int size = 2048;
  bool create(int sz);
  void bind() const;
  void destroy();
  ~ShadowMap2D() { destroy(); }
};

GLuint createTexture2D(int w, int h, GLenum internalFormat, GLenum filter = GL_LINEAR);

}  // namespace og

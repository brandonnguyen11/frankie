#include "render/Framebuffer.h"
#include <iostream>

namespace og {

namespace {
void formatFor(GLenum internal, GLenum& fmt, GLenum& type) {
  switch (internal) {
    case GL_R8: fmt = GL_RED; type = GL_UNSIGNED_BYTE; break;
    case GL_R16F: fmt = GL_RED; type = GL_HALF_FLOAT; break;
    case GL_RG16F: fmt = GL_RG; type = GL_HALF_FLOAT; break;
    case GL_RGB16F: fmt = GL_RGB; type = GL_HALF_FLOAT; break;
    case GL_R11F_G11F_B10F: fmt = GL_RGB; type = GL_HALF_FLOAT; break;
    case GL_RGBA16F: fmt = GL_RGBA; type = GL_HALF_FLOAT; break;
    case GL_RGBA8: default: fmt = GL_RGBA; type = GL_UNSIGNED_BYTE; break;
  }
}
}  // namespace

GLuint createTexture2D(int w, int h, GLenum internal, GLenum filter) {
  GLenum fmt, type;
  formatFor(internal, fmt, type);
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, fmt, type, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}

bool RenderTarget::create(int w, int h, const std::vector<GLenum>& formats, bool withDepth, GLuint sharedDepth) {
  destroy();
  width = w;
  height = h;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  std::vector<GLenum> bufs;
  for (size_t i = 0; i < formats.size(); ++i) {
    GLuint t = createTexture2D(w, h, formats[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + (GLenum)i, GL_TEXTURE_2D, t, 0);
    colors.push_back(t);
    bufs.push_back(GL_COLOR_ATTACHMENT0 + (GLenum)i);
  }
  if (sharedDepth) {
    depth = sharedDepth;
    ownsDepth = false;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
  } else if (withDepth) {
    glGenTextures(1, &depth);
    glBindTexture(GL_TEXTURE_2D, depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    ownsDepth = true;
  }
  if (bufs.empty()) {
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
  } else {
    glDrawBuffers((GLsizei)bufs.size(), bufs.data());
  }
  GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (st != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "RenderTarget incomplete: 0x" << std::hex << st << std::dec << "\n";
    return false;
  }
  return true;
}

void RenderTarget::bind() const {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, width, height);
}

void RenderTarget::destroy() {
  for (auto t : colors) glDeleteTextures(1, &t);
  colors.clear();
  if (depth && ownsDepth) glDeleteTextures(1, &depth);
  depth = 0;
  ownsDepth = false;
  if (fbo) glDeleteFramebuffers(1, &fbo);
  fbo = 0;
}

bool ShadowArray::create(int sz, int count) {
  destroy();
  size = sz;
  layers = count;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, sz, sz, count, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  float border[] = {1, 1, 1, 1};
  glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, tex, 0, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (!ok) std::cerr << "ShadowArray incomplete\n";
  return ok;
}

void ShadowArray::bindLayer(int layer) const {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, tex, 0, layer);
  glViewport(0, 0, size, size);
}

void ShadowArray::destroy() {
  if (tex) glDeleteTextures(1, &tex);
  if (fbo) glDeleteFramebuffers(1, &fbo);
  tex = fbo = 0;
}

bool ShadowMap2D::create(int sz) {
  destroy();
  size = sz;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, sz, sz, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  float border[] = {1, 1, 1, 1};
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return ok;
}

void ShadowMap2D::bind() const {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, size, size);
}

void ShadowMap2D::destroy() {
  if (tex) glDeleteTextures(1, &tex);
  if (fbo) glDeleteFramebuffers(1, &fbo);
  tex = fbo = 0;
}

}  // namespace og

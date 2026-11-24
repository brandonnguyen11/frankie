#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace og {

// GLSL program loaded from disk. Supports `#include "file.glsl"` (resolved
// relative to the shaders directory) so lighting code is shared across passes.
class Shader {
 public:
  Shader() = default;
  ~Shader() { destroy(); }
  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;

  bool load(const std::string& vertPath, const std::string& fragPath, const std::string& defines = "");
  void use() const;
  void destroy();

  void setInt(const std::string& name, int v) const;
  void setFloat(const std::string& name, float v) const;
  void setVec2(const std::string& name, const glm::vec2& v) const;
  void setVec3(const std::string& name, const glm::vec3& v) const;
  void setVec4(const std::string& name, const glm::vec4& v) const;
  void setMat3(const std::string& name, const glm::mat3& m) const;
  void setMat4(const std::string& name, const glm::mat4& m) const;
  void setMat4Array(const std::string& name, const glm::mat4* m, int count) const;
  void setVec4Array(const std::string& name, const glm::vec4* v, int count) const;

  GLuint id() const { return id_; }

 private:
  GLint loc(const std::string& name) const;
  GLuint id_ = 0;
  mutable std::unordered_map<std::string, GLint> cache_;
};

}  // namespace og

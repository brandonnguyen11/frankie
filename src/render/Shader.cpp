#include "render/Shader.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace og {
namespace {

std::string readFile(const std::string& path) {
  std::ifstream f(path);
  if (!f) return {};
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Expand #include "x" lines recursively (depth-limited).
std::string preprocess(const std::string& path, int depth = 0) {
  std::string src = readFile(path);
  if (src.empty()) {
    std::cerr << "Failed to open shader: " << path << "\n";
    return {};
  }
  if (depth > 4) return src;
  std::filesystem::path dir = std::filesystem::path(path).parent_path();
  std::istringstream in(src);
  std::ostringstream out;
  std::string line;
  while (std::getline(in, line)) {
    auto p = line.find("#include");
    if (p != std::string::npos && line.find_first_not_of(" \t") == p) {
      auto q0 = line.find('"');
      auto q1 = line.rfind('"');
      if (q0 != std::string::npos && q1 > q0) {
        out << preprocess((dir / line.substr(q0 + 1, q1 - q0 - 1)).string(), depth + 1) << "\n";
        continue;
      }
    }
    out << line << "\n";
  }
  return out.str();
}

// Insert defines right after the #version line.
std::string injectDefines(const std::string& src, const std::string& defines) {
  if (defines.empty()) return src;
  auto nl = src.find('\n');
  if (nl == std::string::npos) return src;
  return src.substr(0, nl + 1) + defines + "\n" + src.substr(nl + 1);
}

GLuint compile(GLenum type, const std::string& src, const std::string& path, bool& ok) {
  GLuint s = glCreateShader(type);
  const char* c = src.c_str();
  glShaderSource(s, 1, &c, nullptr);
  glCompileShader(s);
  GLint status = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &status);
  if (!status) {
    GLint len = 0;
    glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
    std::vector<char> log(std::max(len, 1));
    glGetShaderInfoLog(s, len, nullptr, log.data());
    std::cerr << "Shader compile error (" << path << "):\n" << log.data() << "\n";
    ok = false;
  }
  return s;
}

}  // namespace

bool Shader::load(const std::string& vertPath, const std::string& fragPath, const std::string& defines) {
  destroy();
  std::string vs = injectDefines(preprocess(vertPath), defines);
  std::string fs = injectDefines(preprocess(fragPath), defines);
  if (vs.empty() || fs.empty()) return false;
  bool ok = true;
  GLuint v = compile(GL_VERTEX_SHADER, vs, vertPath, ok);
  GLuint f = compile(GL_FRAGMENT_SHADER, fs, fragPath, ok);
  id_ = glCreateProgram();
  glAttachShader(id_, v);
  glAttachShader(id_, f);
  glLinkProgram(id_);
  GLint linked = 0;
  glGetProgramiv(id_, GL_LINK_STATUS, &linked);
  if (!linked) {
    GLint len = 0;
    glGetProgramiv(id_, GL_INFO_LOG_LENGTH, &len);
    std::vector<char> log(std::max(len, 1));
    glGetProgramInfoLog(id_, len, nullptr, log.data());
    std::cerr << "Shader link error (" << vertPath << " + " << fragPath << "):\n" << log.data() << "\n";
    ok = false;
  }
  glDeleteShader(v);
  glDeleteShader(f);
  return ok;
}

void Shader::use() const { glUseProgram(id_); }

void Shader::destroy() {
  if (id_) glDeleteProgram(id_);
  id_ = 0;
  cache_.clear();
}

GLint Shader::loc(const std::string& name) const {
  auto it = cache_.find(name);
  if (it != cache_.end()) return it->second;
  GLint l = glGetUniformLocation(id_, name.c_str());
  cache_[name] = l;
  return l;
}

void Shader::setInt(const std::string& n, int v) const { glUniform1i(loc(n), v); }
void Shader::setFloat(const std::string& n, float v) const { glUniform1f(loc(n), v); }
void Shader::setVec2(const std::string& n, const glm::vec2& v) const { glUniform2fv(loc(n), 1, &v[0]); }
void Shader::setVec3(const std::string& n, const glm::vec3& v) const { glUniform3fv(loc(n), 1, &v[0]); }
void Shader::setVec4(const std::string& n, const glm::vec4& v) const { glUniform4fv(loc(n), 1, &v[0]); }
void Shader::setMat3(const std::string& n, const glm::mat3& m) const { glUniformMatrix3fv(loc(n), 1, GL_FALSE, &m[0][0]); }
void Shader::setMat4(const std::string& n, const glm::mat4& m) const { glUniformMatrix4fv(loc(n), 1, GL_FALSE, &m[0][0]); }
void Shader::setMat4Array(const std::string& n, const glm::mat4* m, int c) const {
  if (c > 0) glUniformMatrix4fv(loc(n), c, GL_FALSE, &m[0][0][0]);
}
void Shader::setVec4Array(const std::string& n, const glm::vec4* v, int c) const {
  if (c > 0) glUniform4fv(loc(n), c, &v[0][0]);
}

}  // namespace og

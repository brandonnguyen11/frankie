#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace og {

struct Vertex {
  glm::vec3 position{0.f};
  glm::vec3 normal{0, 1, 0};
  glm::vec2 uv{0.f};
  glm::vec3 color{1.f};
};

// Shading model selector, mirrored in shaders/common.glsl (MAT_* constants).
enum class MatType : int {
  Standard = 0,  // Cook-Torrance GGX, optional clear coat
  Velvet = 1,    // Charlie sheen lobe (seats, curtains)
  Gold = 2,      // anisotropic GGX metal with leaf-patch roughness
  FrogSkin = 3,  // wrap diffuse + thin-surface translucency + wet coat
  Emissive = 4,  // bulbs, neon, windows (radiance in nits)
  Marble = 5,    // veined stone under polished clear coat
  Suit = 6,      // wool weave + fuzz sheen
  Wood = 7,      // plank floors / stage deck
  Sky = 8,       // procedural dusk sky dome (unlit)
  Paving = 9,    // plaza flagstones / asphalt
  Glass = 10,    // lit shop windows: reflective with emissive interior
  Stone = 11,    // dressed ashlar blocks (facade)
};

struct Material {
  glm::vec3 albedo{0.8f};
  float roughness = 0.5f;
  float metalness = 0.f;
  glm::vec3 emissive{0.f};   // linear colour
  float emissiveNits = 0.f;  // luminance of emissive, cd/m^2
  float sheen = 0.f;
  float anisotropy = 0.f;
  float translucency = 0.f;
  float clearcoat = 0.f;
  float uvScale = 1.f;
  MatType type = MatType::Standard;
  GLuint albedoTex = 0;  // optional (glTF imports)

  static Material standard(glm::vec3 c, float rough, float metal = 0.f) {
    Material m; m.albedo = c; m.roughness = rough; m.metalness = metal; return m;
  }
  static Material velvet(glm::vec3 c, float sheen = 0.9f) {
    Material m; m.albedo = c; m.roughness = 0.85f; m.sheen = sheen; m.type = MatType::Velvet; return m;
  }
  static Material gold(float aniso = 0.6f) {
    Material m; m.albedo = {1.0f, 0.77f, 0.34f}; m.roughness = 0.28f; m.metalness = 1.f;
    m.anisotropy = aniso; m.type = MatType::Gold; return m;
  }
  static Material emissiveLight(glm::vec3 c, float nits) {
    Material m; m.albedo = c; m.emissive = c; m.emissiveNits = nits; m.type = MatType::Emissive; return m;
  }
  static Material marble(glm::vec3 c) {
    Material m; m.albedo = c; m.roughness = 0.35f; m.clearcoat = 1.f; m.type = MatType::Marble; return m;
  }
  static Material wood(glm::vec3 c, float uvScale = 1.f) {
    Material m; m.albedo = c; m.roughness = 0.45f; m.clearcoat = 0.4f; m.uvScale = uvScale;
    m.type = MatType::Wood; return m;
  }
};

class Mesh {
 public:
  Mesh() = default;
  ~Mesh();
  Mesh(const Mesh&) = delete;
  Mesh& operator=(const Mesh&) = delete;

  void upload(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
  void setInstances(const std::vector<glm::mat4>& transforms);
  void draw() const;
  void destroy();
  int instanceCount() const { return instanceCount_; }

  // CPU copies kept for bounds and for baking into composite meshes
  std::vector<Vertex> cpuVerts;
  std::vector<unsigned int> cpuIdx;
  glm::vec3 boundsMin{0.f}, boundsMax{0.f};

 private:
  GLuint vao_ = 0, vbo_ = 0, ebo_ = 0, ivbo_ = 0;
  GLsizei indexCount_ = 0;
  int instanceCount_ = 0;
};

using MeshPtr = std::shared_ptr<Mesh>;

// Geometry builders (CPU side). Append into a MeshBuilder then upload once.
struct MeshBuilder {
  std::vector<Vertex> v;
  std::vector<unsigned int> idx;
  glm::vec3 color{1.f};

  void box(const glm::mat4& xf, glm::vec3 halfExtents);
  void cylinder(const glm::mat4& xf, float rBottom, float rTop, float height, int seg, bool caps = true);
  void sphere(const glm::mat4& xf, float r, int slices = 16, int stacks = 12);
  void plane(const glm::mat4& xf, float w, float d, float uvTile = 1.f);
  // Ring/arc band in XZ plane, extruded upward: used for horseshoe tiers.
  void arcWall(float cx, float cz, float radius, float a0, float a1, float y0, float y1, int seg, bool inward);
  void arcSlab(float cx, float cz, float r0, float r1, float a0, float a1, float y, float thick, int seg);
  void lathe(const glm::mat4& xf, const std::vector<glm::vec2>& profile, int seg);
  // Wall panel [cx-halfW, cx+halfW] x [ySpring, yTop] with a semicircular arch cut out
  // of its bottom edge, extruded from zFront back by depth (includes the arch soffit).
  void archSpandrel(float cx, float ySpring, float halfW, float yTop, float zFront, float depth, int seg = 24);
  void append(const MeshBuilder& other, const glm::mat4& xf);
  MeshPtr build() const;
};

MeshPtr makeBox(glm::vec3 halfExtents);
MeshPtr makeSphere(float r, int slices = 24, int stacks = 16);
MeshPtr makeCylinder(float r, float h, int seg = 24);
MeshPtr makePlane(float w, float d, float uvTile = 1.f);

}  // namespace og

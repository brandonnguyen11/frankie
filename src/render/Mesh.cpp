#include "render/Mesh.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstddef>

namespace og {

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kTau = 6.28318530717959f;

glm::mat3 normalMatrix(const glm::mat4& xf) { return glm::transpose(glm::inverse(glm::mat3(xf))); }
}  // namespace

Mesh::~Mesh() { destroy(); }

void Mesh::upload(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
  destroy();
  cpuVerts = vertices;
  cpuIdx = indices;
  if (!vertices.empty()) {
    boundsMin = boundsMax = vertices[0].position;
    for (auto& v : vertices) {
      boundsMin = glm::min(boundsMin, v.position);
      boundsMax = glm::max(boundsMax, v.position);
    }
  }
  indexCount_ = static_cast<GLsizei>(indices.size());
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
  glBindVertexArray(0);
}

void Mesh::setInstances(const std::vector<glm::mat4>& transforms) {
  instanceCount_ = (int)transforms.size();
  glBindVertexArray(vao_);
  if (!ivbo_) glGenBuffers(1, &ivbo_);
  glBindBuffer(GL_ARRAY_BUFFER, ivbo_);
  glBufferData(GL_ARRAY_BUFFER, transforms.size() * sizeof(glm::mat4), transforms.data(), GL_STATIC_DRAW);
  for (int c = 0; c < 4; ++c) {
    glEnableVertexAttribArray(4 + c);
    glVertexAttribPointer(4 + c, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(sizeof(glm::vec4) * c));
    glVertexAttribDivisor(4 + c, 1);
  }
  glBindVertexArray(0);
}

void Mesh::draw() const {
  if (!vao_) return;
  glBindVertexArray(vao_);
  if (instanceCount_ > 0)
    glDrawElementsInstanced(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr, instanceCount_);
  else
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

void Mesh::destroy() {
  if (ivbo_) glDeleteBuffers(1, &ivbo_);
  if (ebo_) glDeleteBuffers(1, &ebo_);
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  vao_ = vbo_ = ebo_ = ivbo_ = 0;
  indexCount_ = 0;
  instanceCount_ = 0;
}

// ---------------------------------------------------------------- builders

void MeshBuilder::box(const glm::mat4& xf, glm::vec3 he) {
  glm::mat3 nm = normalMatrix(xf);
  // world-ish scale for UVs so procedural patterns don't stretch
  glm::vec3 s{glm::length(glm::vec3(xf[0])) * he.x, glm::length(glm::vec3(xf[1])) * he.y,
              glm::length(glm::vec3(xf[2])) * he.z};
  struct Face { glm::vec3 n, u, v; float su, sv; };
  const Face faces[6] = {
      {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}, s.z, s.y}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, s.z, s.y},
      {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}, s.x, s.z}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, s.x, s.z},
      {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, s.x, s.y}, {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, s.x, s.y},
  };
  for (auto& f : faces) {
    unsigned base = (unsigned)v.size();
    glm::vec3 c = f.n * he;
    glm::vec3 du = f.u * glm::abs(glm::dot(f.u, he));
    glm::vec3 dv = f.v * glm::abs(glm::dot(f.v, he));
    glm::vec3 corners[4] = {c - du - dv, c + du - dv, c + du + dv, c - du + dv};
    glm::vec2 uvs[4] = {{0, 0}, {2 * f.su, 0}, {2 * f.su, 2 * f.sv}, {0, 2 * f.sv}};
    for (int i = 0; i < 4; ++i)
      v.push_back({glm::vec3(xf * glm::vec4(corners[i], 1)), glm::normalize(nm * f.n), uvs[i], color});
    idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  }
}

void MeshBuilder::cylinder(const glm::mat4& xf, float r0, float r1, float h, int seg, bool caps) {
  glm::mat3 nm = normalMatrix(xf);
  float hy = h * 0.5f;
  float slope = (r0 - r1) / h;
  unsigned base = (unsigned)v.size();
  for (int i = 0; i <= seg; ++i) {
    float a = (float)i / seg * kTau;
    glm::vec3 d{std::cos(a), 0, std::sin(a)};
    glm::vec3 n = glm::normalize(glm::vec3(d.x, slope, d.z));
    float u = (float)i / seg * kTau * std::max(r0, r1);
    v.push_back({glm::vec3(xf * glm::vec4(d * r0 + glm::vec3(0, -hy, 0), 1)), glm::normalize(nm * n), {u, 0}, color});
    v.push_back({glm::vec3(xf * glm::vec4(d * r1 + glm::vec3(0, hy, 0), 1)), glm::normalize(nm * n), {u, h}, color});
  }
  for (int i = 0; i < seg; ++i) {
    unsigned a = base + i * 2;
    idx.insert(idx.end(), {a, a + 1, a + 3, a, a + 3, a + 2});
  }
  if (!caps) return;
  for (int top = 0; top < 2; ++top) {
    float y = top ? hy : -hy;
    float r = top ? r1 : r0;
    if (r <= 0.f) continue;
    glm::vec3 n{0, top ? 1.f : -1.f, 0};
    unsigned c = (unsigned)v.size();
    v.push_back({glm::vec3(xf * glm::vec4(0, y, 0, 1)), glm::normalize(nm * n), {0, 0}, color});
    for (int i = 0; i <= seg; ++i) {
      float a = (float)i / seg * kTau;
      glm::vec3 p{std::cos(a) * r, y, std::sin(a) * r};
      v.push_back({glm::vec3(xf * glm::vec4(p, 1)), glm::normalize(nm * n), {p.x, p.z}, color});
    }
    for (int i = 0; i < seg; ++i) {
      if (top) idx.insert(idx.end(), {c, c + 2 + i, c + 1 + i});
      else idx.insert(idx.end(), {c, c + 1 + i, c + 2 + i});
    }
  }
}

void MeshBuilder::sphere(const glm::mat4& xf, float r, int slices, int stacks) {
  glm::mat3 nm = normalMatrix(xf);
  unsigned base = (unsigned)v.size();
  for (int y = 0; y <= stacks; ++y) {
    float va = (float)y / stacks * kPi;
    for (int x = 0; x <= slices; ++x) {
      float ha = (float)x / slices * kTau;
      glm::vec3 n{std::sin(va) * std::cos(ha), std::cos(va), std::sin(va) * std::sin(ha)};
      v.push_back({glm::vec3(xf * glm::vec4(n * r, 1)), glm::normalize(nm * n),
                   {(float)x / slices * kTau * r, va * r}, color});
    }
  }
  for (int y = 0; y < stacks; ++y)
    for (int x = 0; x < slices; ++x) {
      unsigned i0 = base + y * (slices + 1) + x, i1 = i0 + slices + 1;
      idx.insert(idx.end(), {i0, i0 + 1, i1, i0 + 1, i1 + 1, i1});
    }
}

void MeshBuilder::plane(const glm::mat4& xf, float w, float d, float uvTile) {
  glm::mat3 nm = normalMatrix(xf);
  float hx = w * 0.5f, hz = d * 0.5f;
  unsigned b = (unsigned)v.size();
  glm::vec3 n = glm::normalize(nm * glm::vec3(0, 1, 0));
  glm::vec3 p[4] = {{-hx, 0, -hz}, {-hx, 0, hz}, {hx, 0, hz}, {hx, 0, -hz}};
  for (auto& q : p)
    v.push_back({glm::vec3(xf * glm::vec4(q, 1)), n, glm::vec2(q.x + hx, q.z + hz) * uvTile, color});
  idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
}

void MeshBuilder::arcWall(float cx, float cz, float r, float a0, float a1, float y0, float y1, int seg, bool inward) {
  unsigned base = (unsigned)v.size();
  for (int i = 0; i <= seg; ++i) {
    float a = a0 + (a1 - a0) * i / seg;
    glm::vec3 d{std::sin(a), 0, std::cos(a)};
    glm::vec3 n = inward ? -d : d;
    glm::vec3 p{cx + d.x * r, 0, cz + d.z * r};
    float u = a * r;
    v.push_back({p + glm::vec3(0, y0, 0), n, {u, y0}, color});
    v.push_back({p + glm::vec3(0, y1, 0), n, {u, y1}, color});
  }
  for (int i = 0; i < seg; ++i) {
    unsigned a = base + i * 2;
    if (inward) idx.insert(idx.end(), {a, a + 1, a + 3, a, a + 3, a + 2});
    else idx.insert(idx.end(), {a, a + 3, a + 1, a, a + 2, a + 3});
  }
}

void MeshBuilder::arcSlab(float cx, float cz, float r0, float r1, float a0, float a1, float y, float thick, int seg) {
  for (int face = 0; face < 2; ++face) {
    float yy = face == 0 ? y : y - thick;
    glm::vec3 n{0, face == 0 ? 1.f : -1.f, 0};
    unsigned base = (unsigned)v.size();
    for (int i = 0; i <= seg; ++i) {
      float a = a0 + (a1 - a0) * i / seg;
      glm::vec3 d{std::sin(a), 0, std::cos(a)};
      glm::vec3 p0{cx + d.x * r0, yy, cz + d.z * r0};
      glm::vec3 p1{cx + d.x * r1, yy, cz + d.z * r1};
      v.push_back({p0, n, {p0.x, p0.z}, color});
      v.push_back({p1, n, {p1.x, p1.z}, color});
    }
    for (int i = 0; i < seg; ++i) {
      unsigned a = base + i * 2;
      idx.insert(idx.end(), {a, a + 1, a + 3, a, a + 3, a + 2});
    }
  }
  arcWall(cx, cz, r0, a0, a1, y - thick, y, seg, true);
  arcWall(cx, cz, r1, a0, a1, y - thick, y, seg, false);
}

void MeshBuilder::lathe(const glm::mat4& xf, const std::vector<glm::vec2>& prof, int seg) {
  glm::mat3 nm = normalMatrix(xf);
  int n = (int)prof.size();
  if (n < 2) return;
  // profile normals in (r,y) plane
  std::vector<glm::vec2> pn(n);
  for (int i = 0; i < n; ++i) {
    glm::vec2 t = prof[std::min(i + 1, n - 1)] - prof[std::max(i - 1, 0)];
    pn[i] = glm::normalize(glm::vec2(t.y, -t.x));
  }
  unsigned base = (unsigned)v.size();
  for (int s = 0; s <= seg; ++s) {
    float a = (float)s / seg * kTau;
    float ca = std::cos(a), sa = std::sin(a);
    for (int i = 0; i < n; ++i) {
      glm::vec3 p{prof[i].x * ca, prof[i].y, prof[i].x * sa};
      glm::vec3 nn{pn[i].x * ca, pn[i].y, pn[i].x * sa};
      v.push_back({glm::vec3(xf * glm::vec4(p, 1)), glm::normalize(nm * nn), {(float)s / seg * 4.f, prof[i].y}, color});
    }
  }
  for (int s = 0; s < seg; ++s)
    for (int i = 0; i < n - 1; ++i) {
      unsigned a = base + s * n + i, b = a + n;
      idx.insert(idx.end(), {a, b, a + 1, a + 1, b, b + 1});
    }
}

void MeshBuilder::archSpandrel(float cx, float ys, float hw, float yt, float zf, float depth, int seg) {
  // front and back faces: quad strip between the arch curve and the panel outline
  for (int face = 0; face < 2; ++face) {
    float z = face == 0 ? zf : zf - depth;
    glm::vec3 n{0, 0, face == 0 ? 1.f : -1.f};
    unsigned base = (unsigned)v.size();
    for (int i = 0; i <= seg; ++i) {
      float th = kPi * i / seg;  // 0 = right springing point, pi = left
      glm::vec2 d{std::cos(th), std::sin(th)};
      glm::vec2 a = glm::vec2(cx, ys) + d * hw;
      float tx = std::abs(d.x) > 1e-4f ? hw / std::abs(d.x) : 1e9f;
      float ty = d.y > 1e-4f ? (yt - ys) / d.y : 1e9f;
      glm::vec2 o = glm::vec2(cx, ys) + d * std::min(tx, ty);
      // snap the outline to the panel corners so the edge stays straight
      if (i > 0 && i < seg) o = glm::clamp(o, glm::vec2(cx - hw, ys), glm::vec2(cx + hw, yt));
      v.push_back({{a.x, a.y, z}, n, {a.x, a.y}, color});
      v.push_back({{o.x, o.y, z}, n, {o.x, o.y}, color});
    }
    for (int i = 0; i < seg; ++i) {
      unsigned q = base + i * 2;
      idx.insert(idx.end(), {q, q + 1, q + 3, q, q + 3, q + 2});
    }
    // corner fill triangles so the top corners are covered
    unsigned c = (unsigned)v.size();
    v.push_back({{cx + hw, yt, z}, n, {cx + hw, yt}, color});
    v.push_back({{cx - hw, yt, z}, n, {cx - hw, yt}, color});
    v.push_back({{cx, yt, z}, n, {cx, yt}, color});
    v.push_back({{cx + hw, ys, z}, n, {cx + hw, ys}, color});
    v.push_back({{cx - hw, ys, z}, n, {cx - hw, ys}, color});
    float r = hw * 0.7071f;
    v.push_back({{cx + r, ys + r, z}, n, {0, 0}, color});
    v.push_back({{cx - r, ys + r, z}, n, {0, 0}, color});
    idx.insert(idx.end(), {c + 3, c, c + 5, c + 5, c, c + 2, c + 4, c + 6, c + 1, c + 6, c + 2, c + 1});
  }
  // soffit: the curved underside of the arch
  unsigned base = (unsigned)v.size();
  for (int i = 0; i <= seg; ++i) {
    float th = kPi * i / seg;
    glm::vec3 d{std::cos(th), std::sin(th), 0};
    glm::vec3 a{cx + d.x * hw, ys + d.y * hw, 0};
    v.push_back({{a.x, a.y, zf}, -d, {th * hw, 0}, color});
    v.push_back({{a.x, a.y, zf - depth}, -d, {th * hw, depth}, color});
  }
  for (int i = 0; i < seg; ++i) {
    unsigned q = base + i * 2;
    idx.insert(idx.end(), {q, q + 1, q + 3, q, q + 3, q + 2});
  }
}

void MeshBuilder::append(const MeshBuilder& o, const glm::mat4& xf) {
  glm::mat3 nm = normalMatrix(xf);
  unsigned base = (unsigned)v.size();
  for (auto vert : o.v) {
    vert.position = glm::vec3(xf * glm::vec4(vert.position, 1));
    vert.normal = glm::normalize(nm * vert.normal);
    v.push_back(vert);
  }
  for (auto i : o.idx) idx.push_back(base + i);
}

MeshPtr MeshBuilder::build() const {
  auto m = std::make_shared<Mesh>();
  m->upload(v, idx);
  return m;
}

MeshPtr makeBox(glm::vec3 he) { MeshBuilder b; b.box(glm::mat4(1), he); return b.build(); }
MeshPtr makeSphere(float r, int sl, int st) { MeshBuilder b; b.sphere(glm::mat4(1), r, sl, st); return b.build(); }
MeshPtr makeCylinder(float r, float h, int seg) { MeshBuilder b; b.cylinder(glm::mat4(1), r, r, h, seg); return b.build(); }
MeshPtr makePlane(float w, float d, float t) { MeshBuilder b; b.plane(glm::mat4(1), w, d, t); return b.build(); }

}  // namespace og

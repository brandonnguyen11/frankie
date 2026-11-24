// Optional glTF/OBJ/FBX import via Assimp. Licensed models dropped into
// assets/models/ replace the procedural stand-ins (see README "Using models").
// Materials are mapped onto the custom shading models by name so imported
// geometry still gets velvet, gold and marble shading.
#include "scene/Primitives.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace og {
namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}
bool has(const std::string& s, std::initializer_list<const char*> keys) {
  for (auto k : keys)
    if (s.find(k) != std::string::npos) return true;
  return false;
}

GLuint uploadRGBA(const unsigned char* px, int w, int h) {
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameterf(GL_TEXTURE_2D, 0x84FE /*GL_TEXTURE_MAX_ANISOTROPY*/, 8.f);
  return t;
}

GLuint loadTexture(const aiScene* sc, const aiMaterial* m, const std::filesystem::path& dir,
                   std::unordered_map<std::string, GLuint>& cache) {
  aiString path;
  if (m->GetTexture(aiTextureType_BASE_COLOR, 0, &path) != AI_SUCCESS &&
      m->GetTexture(aiTextureType_DIFFUSE, 0, &path) != AI_SUCCESS)
    return 0;
  std::string key = path.C_Str();
  if (auto it = cache.find(key); it != cache.end()) return it->second;
  int w = 0, h = 0, n = 0;
  unsigned char* px = nullptr;
  if (const aiTexture* emb = sc->GetEmbeddedTexture(path.C_Str())) {
    if (emb->mHeight == 0)
      px = stbi_load_from_memory((const unsigned char*)emb->pcData, (int)emb->mWidth, &w, &h, &n, 4);
  } else {
    px = stbi_load((dir / key).string().c_str(), &w, &h, &n, 4);
  }
  GLuint t = 0;
  if (px) {
    t = uploadRGBA(px, w, h);
    stbi_image_free(px);
  }
  cache[key] = t;
  return t;
}

Material classify(const aiMaterial* m) {
  aiString nameAi;
  m->Get(AI_MATKEY_NAME, nameAi);
  std::string name = lower(nameAi.C_Str());
  aiColor4D base(0.8f, 0.8f, 0.8f, 1.f);
  if (m->Get(AI_MATKEY_BASE_COLOR, base) != AI_SUCCESS) m->Get(AI_MATKEY_COLOR_DIFFUSE, base);
  float rough = 0.6f, metal = 0.f;
  m->Get(AI_MATKEY_ROUGHNESS_FACTOR, rough);
  m->Get(AI_MATKEY_METALLIC_FACTOR, metal);
  glm::vec3 c{base.r, base.g, base.b};

  if (has(name, {"velvet", "seat", "curtain", "drape", "fabric", "cushion"})) return Material::velvet(c, 0.85f);
  if (has(name, {"gold", "gilt", "gilded", "brass"})) return Material::gold(0.6f);
  if (has(name, {"marble"})) return Material::marble(c);
  if (has(name, {"wood", "floor", "parquet"})) return Material::wood(c);
  if (has(name, {"glass", "crystal"})) {
    Material g = Material::standard(c, 0.05f);
    g.clearcoat = 1.f;
    return g;
  }
  aiColor3D em(0, 0, 0);
  if (m->Get(AI_MATKEY_COLOR_EMISSIVE, em) == AI_SUCCESS && (em.r + em.g + em.b) > 0.05f)
    return Material::emissiveLight({em.r, em.g, em.b}, 3000.f);
  return Material::standard(c, std::clamp(rough, 0.04f, 1.f), std::clamp(metal, 0.f, 1.f));
}

void walk(const aiNode* node, const aiScene* sc, const glm::mat4& parent, const std::vector<Material>& mats,
          SceneData& out) {
  aiMatrix4x4 a = node->mTransformation;
  glm::mat4 local{a.a1, a.b1, a.c1, a.d1, a.a2, a.b2, a.c2, a.d2, a.a3, a.b3, a.c3, a.d3, a.a4, a.b4, a.c4, a.d4};
  glm::mat4 xf = parent * local;
  for (unsigned i = 0; i < node->mNumMeshes; ++i) {
    const aiMesh* am = sc->mMeshes[node->mMeshes[i]];
    MeshBuilder b;
    for (unsigned v = 0; v < am->mNumVertices; ++v) {
      Vertex vx;
      vx.position = {am->mVertices[v].x, am->mVertices[v].y, am->mVertices[v].z};
      if (am->mNormals) vx.normal = {am->mNormals[v].x, am->mNormals[v].y, am->mNormals[v].z};
      if (am->mTextureCoords[0]) vx.uv = {am->mTextureCoords[0][v].x, am->mTextureCoords[0][v].y};
      if (am->mColors[0]) vx.color = {am->mColors[0][v].r, am->mColors[0][v].g, am->mColors[0][v].b};
      b.v.push_back(vx);
    }
    for (unsigned f = 0; f < am->mNumFaces; ++f)
      if (am->mFaces[f].mNumIndices == 3)
        for (int k = 0; k < 3; ++k) b.idx.push_back(am->mFaces[f].mIndices[k]);
    MeshBuilder baked;
    baked.append(b, xf);
    out.add(baked.build(), mats[am->mMaterialIndex]);
  }
  for (unsigned c = 0; c < node->mNumChildren; ++c) walk(node->mChildren[c], sc, xf, mats, out);
}

}  // namespace

bool loadModelInto(const std::string& path, SceneData& out, const glm::mat4& xf) {
  if (!std::filesystem::exists(path)) return false;
  Assimp::Importer imp;
  const aiScene* sc = imp.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                             aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality |
                                             aiProcess_OptimizeMeshes | aiProcess_FlipUVs);
  if (!sc || !sc->mRootNode) {
    std::cerr << "Model load failed: " << path << " — " << imp.GetErrorString() << "\n";
    return false;
  }
  std::filesystem::path dir = std::filesystem::path(path).parent_path();
  std::unordered_map<std::string, GLuint> texCache;
  std::vector<Material> mats;
  for (unsigned i = 0; i < sc->mNumMaterials; ++i) {
    Material m = classify(sc->mMaterials[i]);
    m.albedoTex = loadTexture(sc, sc->mMaterials[i], dir, texCache);
    if (m.albedoTex) m.albedo = glm::vec3(1.f);
    mats.push_back(m);
  }
  size_t before = out.objects.size();
  walk(sc->mRootNode, sc, xf, mats, out);
  std::cout << "Loaded model " << path << " (" << out.objects.size() - before << " meshes)\n";
  return true;
}

}  // namespace og

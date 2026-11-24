#version 410 core
// Shared by the depth/normal prepass and the forward lighting pass, so
// gl_Position is invariant and the main pass can depth-test with GL_EQUAL.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aColor;
layout(location = 4) in mat4 aInstance;  // per-instance model (seats, bulbs)

uniform mat4 uModel;
uniform mat3 uNormalMat;
uniform mat4 uViewProj;
uniform int uInstanced;

out vec3 vWorld;
out vec3 vNormal;
out vec2 vUV;
out vec3 vColor;
out vec3 vLocal;

invariant gl_Position;

void main() {
  mat4 M = uModel;
  mat3 NM = uNormalMat;
  if (uInstanced == 1) {
    M = uModel * aInstance;
    NM = mat3(M);  // instances are rigid (rotation + translation)
  }
  vec4 w = M * vec4(aPos, 1.0);
  vWorld = w.xyz;
  vNormal = NM * aNormal;
  vUV = aUV;
  vColor = aColor;
  vLocal = aPos;
  gl_Position = uViewProj * w;
}

#version 410 core
// Depth + view-space normal prepass. Feeds SSAO and lets the expensive
// lighting pass shade each pixel exactly once.
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;
in vec3 vColor;
in vec3 vLocal;
uniform mat4 uView;
layout(location = 0) out vec4 outNormal;
void main() {
  vec3 n = normalize(mat3(uView) * normalize(vNormal));
  outNormal = vec4(n * 0.5 + 0.5, 1.0);
}

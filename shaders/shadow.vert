#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 4) in mat4 aInstance;
uniform mat4 uLightViewProj;
uniform mat4 uModel;
uniform int uInstanced;
void main() {
  mat4 M = uInstanced == 1 ? uModel * aInstance : uModel;
  gl_Position = uLightViewProj * M * vec4(aPos, 1.0);
}

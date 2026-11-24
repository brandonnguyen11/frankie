#version 410 core
// Separable depth-aware Gaussian (used for SSAO and volumetric buffers).
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uSrc;
uniform sampler2D uDepth;
uniform vec2 uDir;       // texel step * direction
uniform float uDepthSharp;
uniform float uNear;
uniform float uFar;

float linearZ(float d) {
  float z = d * 2.0 - 1.0;
  return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}

void main() {
  const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
  float z0 = linearZ(texture(uDepth, vUV).r);
  vec4 sum = texture(uSrc, vUV) * w[0];
  float wsum = w[0];
  for (int i = 1; i < 5; ++i) {
    for (int s = -1; s <= 1; s += 2) {
      vec2 uv = vUV + uDir * float(i * s);
      float z = linearZ(texture(uDepth, uv).r);
      float ww = w[i] * exp(-abs(z - z0) * uDepthSharp / max(z0, 0.1));
      sum += texture(uSrc, uv) * ww;
      wsum += ww;
    }
  }
  FragColor = sum / wsum;
}

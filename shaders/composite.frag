#version 410 core
// HDR -> display: exposure (EV100), physically-based bloom mix, AgX or ACES
// tone mapping, film-camera vignette + grain, scene-transition fade.
// Output is sRGB-encoded with luma in alpha for FXAA.
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uExposure;      // 1 / (1.2 * 2^EV100)
uniform float uBloomStrength;
uniform int uToneMap;         // 0 AgX, 1 ACES
uniform int uFilmLook;
uniform float uFade;
uniform float uTime;

// AgX (Sobotka), polynomial fit by Benjamin Wrensch
vec3 agxContrast(vec3 x) {
  vec3 x2 = x * x, x4 = x2 * x2;
  return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}
vec3 agx(vec3 v) {
  const mat3 m = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992,
                      0.878468636469772, 0.0784336, 0.0792237451477643, 0.0791661274605434, 0.879142973793104);
  const float lo = -12.47393, hi = 4.026069;
  v = m * v;
  v = clamp(log2(max(v, vec3(1e-10))), lo, hi);
  v = (v - lo) / (hi - lo);
  return agxContrast(v);
}
vec3 agxLook(vec3 v) {
  // mild "punchy" look: richer saturation for the red-and-gold house
  float luma = dot(v, vec3(0.2126, 0.7152, 0.0722));
  v = pow(max(v, 0.0), vec3(1.1));
  return luma + 1.25 * (v - luma);
}
vec3 agxEotf(vec3 v) {
  const mat3 mi = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368,
                       1.15190312990417, -0.0980434501171241, -0.0990297440797205, -0.0989611768448433,
                       1.15107367264116);
  return mi * v;  // display-encoded (≈ sRGB transfer)
}
vec3 acesFitted(vec3 x) {
  const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
vec3 linearToSrgb(vec3 c) {
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
  vec3 hdr = texture(uScene, vUV).rgb;
  vec3 bloom = texture(uBloom, vUV).rgb;
  hdr = mix(hdr, bloom, uBloomStrength);
  hdr *= uExposure;

  if (uFilmLook == 1) {
    vec2 q = vUV - 0.5;
    hdr *= mix(1.0, smoothstep(0.85, 0.2, length(q * vec2(1.0, 0.75))), 0.55);
  }

  vec3 col;
  if (uToneMap == 0) col = clamp(agxEotf(agxLook(agx(hdr))), 0.0, 1.0);
  else col = linearToSrgb(acesFitted(hdr * 0.8));

  if (uFilmLook == 1) {
    float g = hash(vUV * vec2(1920.0, 1080.0) + fract(uTime) * 91.7) - 0.5;
    float l = dot(col, vec3(0.299, 0.587, 0.114));
    col += g * 0.045 * (1.0 - l * 0.6);
  }
  col *= 1.0 - clamp(uFade, 0.0, 1.0);
  float luma = dot(col, vec3(0.299, 0.587, 0.114));
  FragColor = vec4(clamp(col, 0.0, 1.0), luma);
}

#version 410 core
// Thin-lens depth of field for the film camera (C3).
// Circle of confusion: c = |A f (z - S)| / (z (S - f)), A = f / N,
// converted from metres on a 36x24 mm sensor to pixels, then a golden-angle
// gather that only accepts samples whose own blur reaches this pixel.
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform float uFocalMm;
uniform float uFStop;
uniform float uFocusDist;
uniform float uNear;
uniform float uFar;
uniform vec2 uTexel;
uniform float uMaxCoC;

float linearZ(float d) {
  float z = d * 2.0 - 1.0;
  return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}

float cocPx(vec2 uv) {
  float z = linearZ(texture(uDepth, uv).r);
  float f = uFocalMm * 0.001;
  float S = max(uFocusDist, f * 1.5);
  float A = f / uFStop;
  float c = abs(A * f * (z - S)) / max(z * (S - f), 1e-5);  // metres on the sensor
  float px = c / 0.024 / uTexel.y;                          // 24 mm sensor height
  return min(px, uMaxCoC);
}

void main() {
  float c0 = cocPx(vUV);
  vec3 center = texture(uScene, vUV).rgb;
  if (c0 < 0.5) { FragColor = vec4(center, 1.0); return; }
  const int N = 48;
  const float GA = 2.39996323;
  vec3 sum = center;
  float wsum = 1.0;
  for (int i = 1; i < N; ++i) {
    float r = sqrt(float(i) / float(N)) * c0;
    float a = float(i) * GA;
    vec2 uv = vUV + vec2(cos(a), sin(a)) * r * uTexel;
    float cs = cocPx(uv);
    // a sample contributes only if its own blur disc covers this pixel, so a
    // sharp subject never smears onto the defocused background behind it
    float w = smoothstep(r - 1.5, r + 0.5, cs);
    sum += texture(uScene, uv).rgb * w;
    wsum += w;
  }
  FragColor = vec4(sum / wsum, 1.0);
}

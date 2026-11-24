#version 410 core
// FXAA (after Lottes, "FXAA 3.11" quality preset, compacted).
// Expects sRGB colour with luma in alpha.
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uSrc;
uniform vec2 uTexel;
uniform int uEnabled;

float L(vec2 uv) { return texture(uSrc, uv).a; }

void main() {
  vec4 c = texture(uSrc, vUV);
  if (uEnabled == 0) { FragColor = vec4(c.rgb, 1.0); return; }
  float lM = c.a;
  float lN = L(vUV + vec2(0, uTexel.y)), lS = L(vUV - vec2(0, uTexel.y));
  float lE = L(vUV + vec2(uTexel.x, 0)), lW = L(vUV - vec2(uTexel.x, 0));
  float mx = max(lM, max(max(lN, lS), max(lE, lW)));
  float mn = min(lM, min(min(lN, lS), min(lE, lW)));
  float range = mx - mn;
  if (range < max(0.0312, mx * 0.125)) { FragColor = vec4(c.rgb, 1.0); return; }

  float lNW = L(vUV + vec2(-uTexel.x, uTexel.y)), lNE = L(vUV + uTexel);
  float lSW = L(vUV - uTexel), lSE = L(vUV + vec2(uTexel.x, -uTexel.y));
  float edgeH = abs(lNW + lNE - 2.0 * lN) + 2.0 * abs(lW + lE - 2.0 * lM) + abs(lSW + lSE - 2.0 * lS);
  float edgeV = abs(lNW + lSW - 2.0 * lW) + 2.0 * abs(lN + lS - 2.0 * lM) + abs(lNE + lSE - 2.0 * lE);
  bool horiz = edgeH >= edgeV;

  float l1 = horiz ? lS : lW, l2 = horiz ? lN : lE;
  float g1 = abs(l1 - lM), g2 = abs(l2 - lM);
  bool steep1 = g1 >= g2;
  float gScaled = 0.25 * max(g1, g2);
  float stepLen = horiz ? uTexel.y : uTexel.x;
  float lLocal;
  if (steep1) { stepLen = -stepLen; lLocal = 0.5 * (l1 + lM); }
  else lLocal = 0.5 * (l2 + lM);
  vec2 uv = vUV;
  if (horiz) uv.y += stepLen * 0.5; else uv.x += stepLen * 0.5;

  vec2 off = horiz ? vec2(uTexel.x, 0) : vec2(0, uTexel.y);
  vec2 uv1 = uv - off, uv2 = uv + off;
  float e1 = L(uv1) - lLocal, e2 = L(uv2) - lLocal;
  bool r1 = abs(e1) >= gScaled, r2 = abs(e2) >= gScaled;
  const float Q[10] = float[](1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 4.0, 8.0);
  for (int i = 0; i < 10 && !(r1 && r2); ++i) {
    if (!r1) { uv1 -= off * Q[i]; e1 = L(uv1) - lLocal; r1 = abs(e1) >= gScaled; }
    if (!r2) { uv2 += off * Q[i]; e2 = L(uv2) - lLocal; r2 = abs(e2) >= gScaled; }
  }
  float d1 = horiz ? vUV.x - uv1.x : vUV.y - uv1.y;
  float d2 = horiz ? uv2.x - vUV.x : uv2.y - vUV.y;
  bool closer1 = d1 < d2;
  float dMin = min(d1, d2);
  float edgeLen = d1 + d2;
  float pixOff = -dMin / edgeLen + 0.5;
  bool lumaLess = lM < lLocal;
  bool correct = ((closer1 ? e1 : e2) < 0.0) != lumaLess;
  float finalOff = correct ? pixOff : 0.0;
  float avg = (1.0 / 12.0) * (2.0 * (lN + lS + lE + lW) + lNW + lNE + lSW + lSE);
  float sub = clamp(abs(avg - lM) / range, 0.0, 1.0);
  sub = (-2.0 * sub + 3.0) * sub * sub;
  finalOff = max(finalOff, sub * sub * 0.75);
  vec2 fuv = vUV;
  if (horiz) fuv.y += finalOff * stepLen; else fuv.x += finalOff * stepLen;
  FragColor = vec4(texture(uSrc, fuv).rgb, 1.0);
}

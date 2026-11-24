#version 410 core
// Volumetric spotlight beams. For each pixel the view ray is clipped to the
// scene depth and analytically intersected with every spotlight cone; only
// that segment is raymarched through animated 3D-noise haze. Each sample is
// lit with inverse-square falloff, the light's gobo, its shadow map (so frogs
// cut shafts out of the beam) and a Henyey-Greenstein phase function.
#include "common.glsl"

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uDepth;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform float uHaze;      // 0..1 from the lighting desk
uniform int uSteps;
uniform float uAnisoG;    // HG asymmetry, forward scattering
uniform vec3 uAmbientScatter;  // house light scattered by haze (nits)
uniform float uMaxDist;
uniform int uFrame;

float phaseHG(float cosT, float g) {
  float g2 = g * g;
  return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * cosT, 1e-4), 1.5));
}

bool insideCone(vec3 p, vec3 apex, vec3 axis, float cosA) {
  vec3 d = p - apex;
  float l = length(d);
  return dot(d, axis) > l * cosA;
}

// Single interval where the ray is inside the (convex, forward) cone, clipped to [0,tmax].
bool coneInterval(vec3 ro, vec3 rd, vec3 apex, vec3 axis, float cosA, float tmax, out float t0, out float t1) {
  vec3 co = ro - apex;
  float dv = dot(rd, axis), cv = dot(co, axis);
  float c2 = cosA * cosA;
  float a = dv * dv - c2;
  float b = 2.0 * (dv * cv - dot(rd, co) * c2);
  float c = cv * cv - dot(co, co) * c2;
  float ts[4];
  int n = 0;
  ts[n++] = 0.0;
  float disc = b * b - 4.0 * a * c;
  if (disc > 0.0 && abs(a) > 1e-6) {
    float sq = sqrt(disc);
    float r1 = (-b - sq) / (2.0 * a), r2 = (-b + sq) / (2.0 * a);
    if (r1 > r2) { float tmp = r1; r1 = r2; r2 = tmp; }
    if (r1 > 0.0 && r1 < tmax) ts[n++] = r1;
    if (r2 > 0.0 && r2 < tmax) ts[n++] = r2;
  }
  ts[n++] = tmax;
  t0 = 1e9;
  t1 = -1.0;
  for (int i = 0; i < n - 1; ++i) {
    float m = 0.5 * (ts[i] + ts[i + 1]);
    if (ts[i + 1] - ts[i] > 1e-4 && insideCone(ro + rd * m, apex, axis, cosA)) {
      t0 = min(t0, ts[i]);
      t1 = max(t1, ts[i + 1]);
    }
  }
  return t1 > t0;
}

float hazeDensity(vec3 p) {
  vec3 wind = vec3(0.12, 0.03, 0.05) * uTime;
  float n = fbm(p * 0.45 + wind);
  float n2 = vnoise(p * 1.7 - wind * 1.6);
  // haze pools low and thins toward the flies
  float height = exp(-max(p.y - 1.0, 0.0) * 0.08);
  return (0.35 + 1.1 * n * n + 0.25 * n2) * height;
}

void main() {
  float d = texture(uDepth, vUV).r;
  vec4 wp = uInvViewProj * vec4(vUV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  vec3 P = wp.xyz / wp.w;
  vec3 ro = uCamPos;
  vec3 rd = P - ro;
  float sceneT = length(rd);
  rd /= sceneT;
  float tmax = min(sceneT, uMaxDist);

  float sigma = uHaze * 0.11;  // scattering coefficient per metre
  float jitter = fract(ign(gl_FragCoord.xy) + float(uFrame % 16) * 0.618034);
  vec3 inscatter = vec3(0.0);

  for (int i = 0; i < MAX_LIGHTS; ++i) {
    if (i >= uNumLights) break;
    if (int(uLightPosType[i].w) != LIGHT_SPOT) continue;
    vec3 apex = uLightPosType[i].xyz;
    vec3 axis = uLightDirRange[i].xyz;
    float range = uLightDirRange[i].w;
    float t0, t1;
    if (!coneInterval(ro, rd, apex, axis, uLightCone[i].x, tmax, t0, t1)) continue;
    // clip to the light's range sphere
    vec3 oc = ro - apex;
    float bq = dot(oc, rd), cq = dot(oc, oc) - range * range;
    float hq = bq * bq - cq;
    if (hq <= 0.0) continue;
    hq = sqrt(hq);
    t0 = max(t0, -bq - hq);
    t1 = min(t1, -bq + hq);
    if (t1 <= t0) continue;

    int layer = int(uLightColor[i].w);
    int gob = int(uLightCone[i].z);
    float stepLen = (t1 - t0) / float(uSteps);
    vec3 acc = vec3(0.0);
    for (int k = 0; k < 64; ++k) {
      if (k >= uSteps) break;
      float t = t0 + (float(k) + jitter) * stepLen;
      vec3 p = ro + rd * t;
      vec3 toL = apex - p;
      float dl = length(toL);
      vec3 L = toL / dl;
      float cd = dot(-L, axis);
      float cone = smoothstep(uLightCone[i].x, uLightCone[i].y, cd);
      if (cone <= 0.0) continue;
      float vis = 1.0;
      if (layer >= 0 && layer < MAX_SHADOWS) {
        vec4 lp = uShadowMatrix[layer] * vec4(p, 1.0);
        vec3 c = lp.xyz / lp.w * 0.5 + 0.5;
        vis = texture(uShadowArray, vec4(c.xy, float(layer), c.z - 0.0005));
      }
      if (gob > 0) vis *= gobo(gob, spotUV(p, i));
      float E = distanceAtten(dl, range) * cone * cone * vis;
      // scattering angle between light travel (-L) and the path to the eye (-rd)
      acc += vec3(E * hazeDensity(p) * phaseHG(dot(L, rd), uAnisoG));
    }
    inscatter += acc * stepLen * uLightColor[i].rgb;
  }
  inscatter *= sigma;

  // ambient house glow scattered by the haze along the whole ray
  float amb = (1.0 - exp(-tmax * sigma * 0.6));
  vec3 color = inscatter + uAmbientScatter * amb;
  FragColor = vec4(color, 1.0);
}

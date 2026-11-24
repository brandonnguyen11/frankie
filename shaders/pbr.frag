#version 410 core
// Forward lighting pass: physically based materials under spot/point/sun
// lights, hemisphere "probe" ambient with SSAO, house haze / aerial fog.
#include "common.glsl"

in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;
in vec3 vColor;
in vec3 vLocal;

out vec4 FragColor;

uniform vec3 uCamPos;
uniform vec2 uViewport;

// material
uniform int uMatType;
uniform vec3 uAlbedo;
uniform float uRoughness;
uniform float uMetalness;
uniform vec3 uEmissive;      // colour * nits * group dimmer
uniform float uSheen;
uniform float uAnisotropy;
uniform float uTranslucency;
uniform float uClearcoat;
uniform float uUVScale;
uniform int uHasAlbedoTex;
uniform sampler2D uAlbedoTex;

// environment
uniform int uOutdoor;
uniform vec3 uSkyAmbient;     // nits, light arriving from above
uniform vec3 uGroundAmbient;  // nits, light arriving from below
uniform vec3 uSunDir;         // toward the sun
uniform float uSkyBrightness;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform sampler2D uSSAO;
// Indoor "light probe": two bright reflection lobes (chandelier, lit stage) on
// top of the hemisphere so gold and lacquer have something to mirror.
uniform vec3 uLobePos[2];
uniform vec3 uLobeColor[2];  // nits

uniform float uDusk;          // 0 afternoon palette .. 1 dusk palette

vec3 skyRadiance(vec3 d) {
  float y = d.y;
  // palettes are in nits at uSkyBrightness == 1 (golden dusk)
  vec3 zenith = mix(vec3(0.20, 0.38, 0.95) * 140.0, vec3(0.10, 0.17, 0.42) * 60.0, uDusk);
  vec3 horizon = mix(vec3(0.72, 0.80, 0.95) * 260.0, vec3(1.0, 0.52, 0.26) * 320.0, uDusk);
  vec3 band = mix(vec3(0.50, 0.65, 0.95) * 200.0, vec3(0.85, 0.35, 0.32) * 160.0, uDusk);
  float h = clamp(y, 0.0, 1.0);
  vec3 col = mix(horizon, band, smoothstep(0.02, 0.12, h));
  col = mix(col, zenith, smoothstep(0.08, 0.55, h));
  float sd = max(dot(d, uSunDir), 0.0);
  vec3 glowCol = mix(vec3(1.0, 0.9, 0.75), vec3(1.0, 0.55, 0.25), uDusk);
  col += glowCol * (pow(sd, 6.0) * 500.0 + pow(sd, 64.0) * 2500.0);
  col += vec3(1.0, 0.8, 0.55) * smoothstep(0.9995, 0.9998, sd) * 40000.0 * step(0.0, uSunDir.y);
  // below the horizon: city haze
  col = mix(col, vec3(0.35, 0.22, 0.18) * 60.0, smoothstep(0.0, -0.08, y));
  // faint high clouds
  if (y > 0.02) {
    vec2 uv = d.xz / (y + 0.15) * 2.0 + vec2(uTime * 0.004, 0.0);
    float c = smoothstep(0.55, 0.85, fbm(vec3(uv, 3.0)));
    col = mix(col, horizon * 0.7 + band * 0.4, c * 0.45 * (1.0 - h));
  }
  return col * uSkyBrightness;
}

vec3 envRadiance(vec3 R, float rough, vec3 P) {
  vec3 hemi = mix(uGroundAmbient, uSkyAmbient, clamp(R.y * 0.5 + 0.5, 0.0, 1.0));
  if (uOutdoor == 1) {
    vec3 sky = R.y > -0.02 ? skyRadiance(R) : uGroundAmbient;
    return mix(sky, hemi, clamp(rough * 1.2, 0.0, 1.0));
  }
  // Phong-normalised lobes: sharp for polished gold, wide for rough surfaces
  float a = max(rough * rough, 0.02);
  float k = 2.0 / (a * a) - 2.0;
  vec3 env = hemi;
  for (int i = 0; i < 2; ++i) {
    vec3 d = normalize(uLobePos[i] - P);
    float c = max(dot(R, d), 0.0);
    env += uLobeColor[i] * pow(c, k) * min((k + 2.0) / 60.0, 6.0);
  }
  return env;
}

void main() {
  vec3 N = normalize(vNormal);
  vec3 V = normalize(uCamPos - vWorld);
  float dist = length(uCamPos - vWorld);

  if (uMatType == MAT_SKY) {
    FragColor = vec4(skyRadiance(-V), 1.0);
    return;
  }

  Surface s;
  s.P = vWorld;
  s.N = N;
  s.V = V;
  s.albedo = uAlbedo * vColor;
  if (uHasAlbedoTex == 1) s.albedo *= texture(uAlbedoTex, vUV).rgb;
  s.rough = uRoughness;
  s.metal = uMetalness;
  s.aniso = 0.0;
  s.sheen = uSheen;
  s.sheenColor = vec3(0.0);
  s.sheenRough = 0.5;
  s.translucency = uTranslucency;
  s.clearcoat = uClearcoat;
  s.type = uMatType;
  vec3 up = abs(N.y) > 0.95 ? vec3(1, 0, 0) : vec3(0, 1, 0);
  s.T = normalize(cross(up, N));
  s.B = cross(N, s.T);

  vec3 emissive = uEmissive;

  // ---------------- procedural material detail
  if (uMatType == MAT_EMISSIVE) {
    // bulbs/neon: hot core, slightly dimmer silhouette edge
    float NoV = clamp(dot(N, V), 0.0, 1.0);
    FragColor = vec4(emissive * (0.55 + 0.45 * NoV), 1.0);
    return;
  } else if (uMatType == MAT_VELVET) {
    float pile = fbm(vWorld * 35.0);
    s.albedo *= 0.85 + 0.3 * pile;
    s.sheenColor = clamp(s.albedo * 3.2 + 0.02, 0.0, 1.0);
    s.sheenRough = 0.42;
    s.rough = 0.9;
  } else if (uMatType == MAT_SUIT) {
    vec3 q = vLocal * 70.0;
    float weave = 0.5 + 0.5 * sin(q.x + q.y) * sin(q.y - q.z);
    s.albedo *= 0.82 + 0.3 * weave;
    s.sheenColor = clamp(s.albedo * 2.0 + 0.1, 0.0, 1.0);
    s.sheenRough = 0.6;
  } else if (uMatType == MAT_GOLD) {
    // burnished gilding: broad, soft variation in polish plus darker tarnish in recesses
    float polish = fbm(vWorld * 2.2);
    float tarnish = fbm(vWorld * 5.0 + 11.0);
    s.rough = 0.24 + (polish - 0.5) * 0.12;
    s.albedo *= mix(0.86, 1.03, smoothstep(0.25, 0.75, tarnish));
    s.aniso = uAnisotropy;
  } else if (uMatType == MAT_FROG) {
    float spots = vnoise(vLocal * 6.0 + 3.0);
    s.albedo = mix(s.albedo, s.albedo * vec3(0.4, 0.5, 0.3), smoothstep(0.58, 0.72, spots));
  } else if (uMatType == MAT_MARBLE) {
    float t = fbm(vWorld * 0.9);
    float v = abs(sin((vWorld.x * 0.7 + vWorld.y * 1.3 + vWorld.z * 0.5) * 2.0 + t * 7.0));
    float vein = 1.0 - smoothstep(0.0, 0.1, v);
    s.albedo = mix(s.albedo * (0.85 + 0.3 * t), vec3(0.82, 0.76, 0.7), vein * 0.7);
  } else if (uMatType == MAT_WOOD) {
    vec2 uv = vUV * uUVScale;
    float plankW = 0.14;
    float row = floor(uv.y / plankW);
    float u = uv.x + hash12(vec2(row, 3.1)) * 4.0;
    float plankLen = 2.2;
    float seg = floor(u / plankLen);
    float grain = fbm(vec3(u * 0.8, (uv.y - row * plankW) * 30.0, row + seg * 7.0));
    float tint = 0.75 + 0.5 * hash12(vec2(row, seg));
    float gap = smoothstep(0.0, 0.006, min(fract(uv.y / plankW) * plankW, (1.0 - fract(uv.y / plankW)) * plankW));
    gap *= smoothstep(0.0, 0.01, min(fract(u / plankLen) * plankLen, (1.0 - fract(u / plankLen)) * plankLen));
    s.albedo *= tint * (0.7 + 0.6 * grain) * mix(0.35, 1.0, gap);
    s.rough = mix(0.65, uRoughness, gap) + (grain - 0.5) * 0.1;
  } else if (uMatType == MAT_STONE) {
    // ashlar: running-bond blocks on whichever wall plane we're on, with recessed joints
    vec3 w = vWorld;
    vec2 p = abs(N.x) > 0.7 ? w.zy : (abs(N.y) > 0.7 ? w.xz : w.xy);
    float row = floor(p.y / 0.55);
    float x = p.x + mod(row, 2.0) * 0.6;
    vec2 cell = vec2(floor(x / 1.2), row);
    vec2 f = vec2(fract(x / 1.2) * 1.2, fract(p.y / 0.55) * 0.55);
    float edge = min(min(f.x, 1.2 - f.x), min(f.y, 0.55 - f.y));
    float joint = smoothstep(0.0, 0.02, edge);
    float grime = fbm(w * vec3(0.6, 1.8, 0.6));
    s.albedo *= (0.88 + 0.2 * hash12(cell)) * mix(0.55, 1.0, joint) * (0.8 + 0.3 * grime);
    s.rough = mix(0.9, uRoughness, joint);
  } else if (uMatType == MAT_PAVING) {
    vec2 p = vWorld.xz;
    float rowH = 0.7;
    float row = floor(p.y / rowH);
    float x = p.x + mod(row, 2.0) * 0.55;
    vec2 cell = vec2(floor(x / 1.1), row);
    vec2 f = vec2(fract(x / 1.1) * 1.1, fract(p.y / rowH) * rowH);
    float edge = min(min(f.x, 1.1 - f.x), min(f.y, rowH - f.y));
    float grout = smoothstep(0.0, 0.025, edge);
    float stoneVar = 0.8 + 0.35 * hash12(cell);
    float wear = fbm(vec3(p * 3.0, 0.0));
    s.albedo *= stoneVar * mix(0.4, 1.0, grout) * (0.85 + 0.3 * wear);
    // after-rain sheen: puddles collect in low spots and grout
    float puddle = smoothstep(0.52, 0.62, fbm(vec3(p * 0.18, 5.0)));
    float wet = max(puddle, (1.0 - grout) * 0.6);
    s.albedo *= mix(1.0, 0.55, wet);
    s.rough = mix(0.75, 0.06, puddle);
    s.clearcoat = puddle;
  }

  // ---------------- direct lighting
  vec3 Lo = vec3(0.0);
  bool twoSided = uMatType == MAT_FROG || uMatType == MAT_VELVET;
  for (int i = 0; i < MAX_LIGHTS; ++i) {
    if (i >= uNumLights) break;
    vec3 L, E;
    if (!evalLight(i, vWorld, N, L, E, twoSided)) continue;
    Lo += shadeLight(s, L, E);
  }

  // ---------------- ambient ("light probe" hemisphere) with SSAO
  float ao = texture(uSSAO, gl_FragCoord.xy / uViewport).r;
  float NoV = clamp(dot(N, V), 1e-4, 1.0);
  vec3 f0 = mix(vec3(0.04), s.albedo, s.metal);
  vec3 irr = mix(uGroundAmbient, uSkyAmbient, clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
  vec3 R = reflect(-V, N);
  vec3 env = envRadiance(R, s.rough, vWorld);
  vec3 ambient = s.albedo * (1.0 - s.metal) * irr * ao;
  float specAO = clamp(pow(NoV + ao, exp2(-16.0 * s.rough - 1.0)) - 1.0 + ao, 0.0, 1.0);
  ambient += env * EnvBRDFApprox(f0, s.rough, NoV) * specAO;
  if (s.sheen > 0.0) ambient += s.sheenColor * s.sheen * irr * 0.18 * ao * (1.0 - NoV * 0.5);
  if (s.clearcoat > 0.0) {
    float Fc = 0.04 + 0.96 * pow(1.0 - NoV, 5.0);
    ambient = ambient * (1.0 - Fc * s.clearcoat) + envRadiance(R, 0.05, vWorld) * Fc * s.clearcoat * specAO;
  }

  vec3 color = Lo + ambient;
  if (uMatType == MAT_GLASS) {
    float F = 0.04 + 0.96 * pow(1.0 - NoV, 5.0);
    // interiors read as rooms: brighter toward ceiling fixtures, darker at the floor
    float room = uOutdoor == 1 ? 0.3 + 0.9 * smoothstep(1.3, 6.8, vWorld.y) * (0.75 + 0.5 * vnoise(vWorld * 1.3)) : 1.0;
    color += emissive * room * (1.0 - F);
  } else {
    color += emissive;
  }

  // ---------------- participating media
  float fog = 1.0 - exp(-dist * uFogDensity);
  color = mix(color, uFogColor, fog);
  FragColor = vec4(color, 1.0);
}

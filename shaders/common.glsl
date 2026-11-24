// Shared lighting library for Opéra Grenouille.
// Units: spot/point intensity in candela, sun in lux, emissive/sky in nits.
// Exposure is applied in composite.frag, so everything here is physical radiance.

const float PI = 3.14159265359;
#define MAX_LIGHTS 32
#define MAX_SHADOWS 8

#define MAT_STANDARD 0
#define MAT_VELVET 1
#define MAT_GOLD 2
#define MAT_FROG 3
#define MAT_EMISSIVE 4
#define MAT_MARBLE 5
#define MAT_SUIT 6
#define MAT_WOOD 7
#define MAT_SKY 8
#define MAT_PAVING 9
#define MAT_GLASS 10
#define MAT_STONE 11

#define LIGHT_SPOT 0
#define LIGHT_POINT 1
#define LIGHT_SUN 2

uniform int uNumLights;
uniform vec4 uLightPosType[MAX_LIGHTS];   // xyz position, w type
uniform vec4 uLightDirRange[MAX_LIGHTS];  // xyz direction light travels, w range
uniform vec4 uLightColor[MAX_LIGHTS];     // rgb colour * intensity, w shadow layer (-1 none, 8 = sun)
uniform vec4 uLightCone[MAX_LIGHTS];      // x cos outer, y cos inner, z gobo id, w tan(outer)
uniform mat4 uShadowMatrix[MAX_SHADOWS];
uniform sampler2DArrayShadow uShadowArray;
uniform sampler2DShadow uSunShadow;
uniform mat4 uSunMatrix;
uniform float uShadowTexel;   // 1 / spot shadow resolution
uniform float uSunTexel;
uniform float uTime;

// ------------------------------------------------------------------ noise
float hash13(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.zyx + 31.32);
  return fract((p.x + p.y) * p.z);
}
float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec3 p) {
  vec3 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(hash13(i), hash13(i + vec3(1, 0, 0)), f.x),
                 mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), f.x), f.y),
             mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), f.x),
                 mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float fbm(vec3 p) {
  float a = 0.5, s = 0.0;
  for (int i = 0; i < 4; ++i) { s += a * vnoise(p); p = p * 2.03 + 17.1; a *= 0.5; }
  return s;
}
// Interleaved gradient noise (Jimenez 2014) for per-pixel rotations/jitter.
float ign(vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); }

// ------------------------------------------------------------------ BRDF terms
float D_GGX(float NoH, float a) {
  float a2 = a * a;
  float d = NoH * NoH * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d);
}
float V_SmithGGXCorrelated(float NoV, float NoL, float a) {
  float a2 = a * a;
  float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
  float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
  return 0.5 / max(gv + gl, 1e-5);
}
vec3 F_Schlick(float VoH, vec3 f0) { return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0); }

// Anisotropic GGX (Burley 2012) for brushed gold leaf
float D_GGXAniso(float NoH, float ToH, float BoH, float at, float ab) {
  float a2 = at * ab;
  vec3 d = vec3(ab * ToH, at * BoH, a2 * NoH);
  float d2 = dot(d, d);
  float b2 = a2 / max(d2, 1e-7);
  return a2 * b2 * b2 / PI;
}
float V_SmithGGXAniso(float at, float ab, float ToV, float BoV, float ToL, float BoL, float NoV, float NoL) {
  float lv = NoL * length(vec3(at * ToV, ab * BoV, NoV));
  float ll = NoV * length(vec3(at * ToL, ab * BoL, NoL));
  return 0.5 / max(lv + ll, 1e-5);
}

// Charlie sheen (Estevez & Kulla 2017) + Neubelt visibility: velvet, wool
float D_Charlie(float rough, float NoH) {
  float inv = 1.0 / max(rough, 0.07);
  float sin2 = max(1.0 - NoH * NoH, 0.0078125);
  return (2.0 + inv) * pow(sin2, inv * 0.5) / (2.0 * PI);
}
float V_Neubelt(float NoV, float NoL) { return 1.0 / (4.0 * (NoL + NoV - NoL * NoV) + 1e-4); }

// Karis' analytic approximation of the split-sum environment BRDF
vec3 EnvBRDFApprox(vec3 f0, float rough, float NoV) {
  const vec4 c0 = vec4(-1, -0.0275, -0.572, 0.022);
  const vec4 c1 = vec4(1, 0.0425, 1.04, -0.04);
  vec4 r = rough * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
  vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
  return f0 * AB.x + AB.y;
}

// ------------------------------------------------------------------ surfaces
struct Surface {
  vec3 P, N, V, T, B;
  vec3 albedo;
  float rough;
  float metal;
  float aniso;
  float sheen;
  vec3 sheenColor;
  float sheenRough;
  float translucency;
  float clearcoat;
  int type;
};

// Smooth spotlight falloff with a windowed inverse-square (Karis 2013).
float distanceAtten(float d, float range) {
  float r = d / max(range, 1e-3);
  float w = clamp(1.0 - r * r * r * r, 0.0, 1.0);
  return w * w / (d * d + 0.01);
}

// Procedural gobo patterns in the light's normalised cone coordinates.
float gobo(int id, vec2 uv) {
  if (id == 1) {  // leaf breakup
    float n = fbm(vec3(uv * 3.2, 1.7));
    return smoothstep(0.42, 0.55, n);
  }
  if (id == 2) {  // window panes
    vec2 g = abs(fract(uv * vec2(1.6, 2.2) + 0.5) - 0.5);
    return smoothstep(0.03, 0.06, min(g.x, g.y)) * step(length(uv), 0.95);
  }
  if (id == 3) {  // stars
    vec2 c = floor(uv * 5.0);
    vec2 f = fract(uv * 5.0) - 0.5 - (vec2(hash12(c), hash12(c + 7.0)) - 0.5) * 0.5;
    return smoothstep(0.16, 0.1, length(f)) + 0.08;
  }
  return 1.0;
}

vec2 spotUV(vec3 P, int i) {
  vec3 dir = uLightDirRange[i].xyz;
  vec3 up = abs(dir.y) > 0.95 ? vec3(1, 0, 0) : vec3(0, 1, 0);
  vec3 rt = normalize(cross(dir, up));
  vec3 u = cross(rt, dir);
  vec3 d = P - uLightPosType[i].xyz;
  float z = max(dot(d, dir), 1e-3);
  return vec2(dot(d, rt), dot(d, u)) / (z * uLightCone[i].w);
}

const vec2 kPoisson[12] = vec2[](
  vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696, 0.457), vec2(-0.203, 0.621),
  vec2(0.962, -0.195), vec2(0.473, -0.480), vec2(0.519, 0.767), vec2(0.185, -0.893),
  vec2(0.507, 0.064), vec2(0.896, 0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598));

// PCF with a per-pixel rotated Poisson disk (hardware 2x2 compare per tap).
float spotShadow(int layer, vec3 P, vec3 N, vec3 L, float softness) {
  // normal offset scales with the slope so grazing surfaces don't acne
  float NoL = clamp(dot(N, L), 0.0, 1.0);
  vec3 Po = P + N * (0.015 + 0.03 * (1.0 - NoL));
  vec4 lp = uShadowMatrix[layer] * vec4(Po, 1.0);
  vec3 c = lp.xyz / lp.w * 0.5 + 0.5;
  if (c.z >= 1.0) return 1.0;
  float ang = ign(gl_FragCoord.xy) * 6.2831853;
  mat2 rot = mat2(cos(ang), sin(ang), -sin(ang), cos(ang));
  float radius = uShadowTexel * (1.5 + 2.5 * softness);
  float s = 0.0;
  for (int k = 0; k < 12; ++k) {
    vec2 o = rot * kPoisson[k] * radius;
    s += texture(uShadowArray, vec4(c.xy + o, float(layer), c.z - 0.0004));
  }
  return s / 12.0;
}

float sunShadow(vec3 P, vec3 N, vec3 L) {
  float NoL = clamp(dot(N, L), 0.0, 1.0);
  vec3 Po = P + N * (0.05 + 0.1 * (1.0 - NoL));
  vec3 c = (uSunMatrix * vec4(Po, 1.0)).xyz * 0.5 + 0.5;
  if (any(lessThan(c.xy, vec2(0.0))) || any(greaterThan(c.xy, vec2(1.0))) || c.z >= 1.0) return 1.0;
  float ang = ign(gl_FragCoord.xy) * 6.2831853;
  mat2 rot = mat2(cos(ang), sin(ang), -sin(ang), cos(ang));
  float s = 0.0;
  for (int k = 0; k < 12; ++k)
    s += texture(uSunShadow, vec3(c.xy + rot * kPoisson[k] * uSunTexel * 1.8, c.z - 0.0006));
  return s / 12.0;
}

// Returns illuminance-weighted direction info for light i at surface point.
// E is illuminance colour (lux * colour); L points from P toward the light.
bool evalLight(int i, vec3 P, vec3 N, out vec3 L, out vec3 E, bool twoSided) {
  int type = int(uLightPosType[i].w);
  vec3 col = uLightColor[i].rgb;
  int layer = int(uLightColor[i].w);
  if (type == LIGHT_SUN) {
    L = -uLightDirRange[i].xyz;
    E = col;
    if (layer == 8) E *= sunShadow(P, N, L);
    return true;
  }
  vec3 toL = uLightPosType[i].xyz - P;
  float d = length(toL);
  L = toL / max(d, 1e-4);
  float att = distanceAtten(d, uLightDirRange[i].w);
  if (att <= 0.0) return false;
  if (type == LIGHT_SPOT) {
    float cd = dot(-L, uLightDirRange[i].xyz);
    float cone = smoothstep(uLightCone[i].x, uLightCone[i].y, cd);
    if (cone <= 0.0) return false;
    att *= cone * cone;
    int g = int(uLightCone[i].z);
    if (g > 0) att *= gobo(g, spotUV(P, i));
    if (layer >= 0 && layer < MAX_SHADOWS) {
      if (dot(N, L) <= 0.0 && !twoSided) return false;
      att *= spotShadow(layer, P, N, L, 0.6);
    }
  }
  E = col * att;
  return true;
}

// Direct lighting for one light: returns outgoing radiance.
vec3 shadeLight(Surface s, vec3 L, vec3 E) {
  vec3 N = s.N, V = s.V;
  vec3 H = normalize(V + L);
  float NoV = clamp(abs(dot(N, V)), 1e-4, 1.0);
  float NoLraw = dot(N, L);
  float NoL = clamp(NoLraw, 0.0, 1.0);
  float NoH = clamp(dot(N, H), 0.0, 1.0);
  float VoH = clamp(dot(V, H), 0.0, 1.0);
  float a = max(s.rough * s.rough, 0.002);
  vec3 f0 = mix(vec3(0.04), s.albedo, s.metal);
  vec3 diffuseColor = s.albedo * (1.0 - s.metal);
  vec3 F = F_Schlick(VoH, f0);

  vec3 spec;
  if (s.type == MAT_GOLD && s.aniso > 0.0) {
    float at = max(a * (1.0 + s.aniso), 0.002), ab = max(a * (1.0 - s.aniso), 0.002);
    float D = D_GGXAniso(NoH, dot(s.T, H), dot(s.B, H), at, ab);
    float Vis = V_SmithGGXAniso(at, ab, dot(s.T, V), dot(s.B, V), dot(s.T, L), dot(s.B, L), NoV, NoL);
    spec = D * Vis * F;
  } else {
    spec = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F;
  }

  float diffuseNoL = NoL;
  vec3 extra = vec3(0.0);
  if (s.type == MAT_FROG) {
    // wrap lighting softens the terminator; transmission makes thin edges glow
    float w = 0.5;
    diffuseNoL = clamp((NoLraw + w) / ((1.0 + w) * (1.0 + w)), 0.0, 1.0);
    vec3 Lt = L + N * 0.35;
    float back = pow(clamp(dot(V, -Lt), 0.0, 1.0), 4.0) * s.translucency;
    float rim = pow(1.0 - NoV, 3.0) * clamp(-NoLraw + 0.4, 0.0, 1.0) * s.translucency;
    extra += (back + rim * 0.6) * vec3(0.55, 0.85, 0.25) * s.albedo * 2.2;
  }
  vec3 Fd = diffuseColor / PI * (1.0 - F) * diffuseNoL;

  vec3 Fs = spec * NoL;
  if (s.sheen > 0.0) {
    float Ds = D_Charlie(s.sheenRough, NoH);
    float Vs = V_Neubelt(NoV, NoL);
    Fs += s.sheenColor * Ds * Vs * NoL * s.sheen;
    // velvet: energy goes into the sheen, not the facing diffuse
    Fd *= 1.0 - 0.5 * s.sheen;
  }

  vec3 Lo = Fd + Fs + extra;
  if (s.clearcoat > 0.0) {
    float ac = 0.05 * 0.05;
    float Fc = 0.04 + 0.96 * pow(1.0 - VoH, 5.0);
    float coat = D_GGX(NoH, ac) * V_SmithGGXCorrelated(NoV, NoL, ac) * Fc * s.clearcoat;
    Lo = Lo * (1.0 - Fc * s.clearcoat) + vec3(coat * NoL);
  }
  return Lo * E;
}

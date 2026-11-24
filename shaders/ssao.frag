#version 410 core
// Normal-oriented hemisphere SSAO (Crytek / LearnOpenGL style) at half res.
in vec2 vUV;
out float FragAO;

uniform sampler2D uDepth;
uniform sampler2D uNormal;   // view-space normal * 0.5 + 0.5
uniform mat4 uProj;
uniform mat4 uInvProj;
uniform vec4 uKernel[16];
uniform float uRadius;
uniform vec2 uNoiseScale;

float ign(vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); }

vec3 viewPos(vec2 uv) {
  float d = texture(uDepth, uv).r;
  vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  return p.xyz / p.w;
}

void main() {
  float d = texture(uDepth, vUV).r;
  if (d >= 1.0) { FragAO = 1.0; return; }
  vec3 P = viewPos(vUV);
  vec3 N = normalize(texture(uNormal, vUV).xyz * 2.0 - 1.0);
  float a = ign(gl_FragCoord.xy) * 6.2831853;
  vec3 rnd = vec3(cos(a), sin(a), 0.0);
  vec3 T = normalize(rnd - N * dot(rnd, N));
  vec3 B = cross(N, T);
  mat3 TBN = mat3(T, B, N);
  float occ = 0.0;
  for (int i = 0; i < 16; ++i) {
    vec3 sp = P + TBN * uKernel[i].xyz * uRadius;
    vec4 o = uProj * vec4(sp, 1.0);
    vec2 suv = o.xy / o.w * 0.5 + 0.5;
    if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) continue;
    float sceneZ = viewPos(suv).z;
    float range = smoothstep(0.0, 1.0, uRadius / max(abs(P.z - sceneZ), 1e-3));
    occ += (sceneZ >= sp.z + 0.025 ? 1.0 : 0.0) * range;
  }
  FragAO = pow(clamp(1.0 - occ / 16.0, 0.0, 1.0), 1.6);
}

#version 410 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uSrc;
void main() { FragColor = vec4(texture(uSrc, vUV).rgb, 1.0); }

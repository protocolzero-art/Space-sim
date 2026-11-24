#version 460

layout(location = 0) in vec2 vQuadUv;
layout(location = 1) in vec3 vStarColor;
layout(location = 2) in float vIsBlackHole;

layout(location = 0) out vec4 outColor;

void main() {
    float r = length(vQuadUv);
    if (r > 1.0) discard;

    if (vIsBlackHole > 0.5) {
        float ring = exp(-pow((r - 0.48) * 9.0, 2.0));
        outColor = vec4(vStarColor * ring * 1.8, ring);
    } else {
        float core = exp(-r * r * 14.0);
        float halo = exp(-r * 3.8) * 0.42;
        vec3 col = vStarColor * (core * 2.2 + halo);
        outColor = vec4(col, clamp(core + halo, 0.0, 1.0));
    }
}

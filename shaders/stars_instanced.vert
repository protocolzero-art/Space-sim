#version 460

struct Body {
    vec4 posMass;
    vec4 velTemp;
    vec4 accRadius;
    vec4 meta;
};

layout(std430, set = 0, binding = 0) readonly buffer BodyBuffer {
    Body bodies[];
};

layout(push_constant) uniform StarPC {
    mat4 viewProj;
    vec4 camRight;
    vec4 camUp;
} pc;

layout(location = 0) out vec2 vQuadUv;
layout(location = 1) out vec3 vStarColor;
layout(location = 2) out float vIsBlackHole;

vec3 blackbodyRGB(float tempK) {
    float t = clamp(tempK, 1500.0, 38000.0) / 100.0;
    vec3 c;
    c.r = (t <= 66.0) ? 1.0 : clamp(1.292936 * pow(t - 60.0, -0.1332047), 0.0, 1.0);
    c.g = (t <= 66.0) ? clamp(0.3900815 * log(t) - 0.6318414, 0.0, 1.0)
                      : clamp(1.1298908 * pow(t - 60.0, -0.0755148), 0.0, 1.0);
    c.b = (t >= 66.0) ? 1.0 : ((t <= 19.0) ? 0.0 : clamp(0.5432067 * log(t - 10.0) - 1.196254, 0.0, 1.0));
    return pow(c, vec3(2.2));
}

const vec2 QUAD[6] = vec2[](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0),
    vec2(-1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0)
);

void main() {
    Body b = bodies[gl_InstanceIndex];
    vec2 corner = QUAD[gl_VertexIndex];
    vQuadUv = corner;

    float bodyClass = b.meta.x;
    vIsBlackHole = (bodyClass >= 4.5 && bodyClass <= 6.5) ? 1.0 : 0.0;

    float size = clamp(b.accRadius.w * 1.45, 0.14, 3.8);
    vec3 worldPos = b.posMass.xyz
                  + pc.camRight.xyz * (corner.x * size)
                  + pc.camUp.xyz    * (corner.y * size);

    float lumBoost = clamp(log(1.0 + max(b.meta.w, 0.1)) * 0.45 + 0.65, 0.6, 3.2);
    vStarColor = (vIsBlackHole > 0.5)
        ? vec3(1.0, 0.72, 0.38) * 2.4
        : blackbodyRGB(b.velTemp.w) * lumBoost;

    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
}

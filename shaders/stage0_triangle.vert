#version 460

layout(location = 0) out vec3 vColor;

const vec2 POS[3] = vec2[](
    vec2( 0.0, -0.55),
    vec2( 0.55, 0.45),
    vec2(-0.55, 0.45)
);

const vec3 COL[3] = vec3[](
    vec3(0.15, 0.78, 1.00),
    vec3(1.00, 0.48, 0.16),
    vec3(0.65, 0.25, 1.00)
);

void main() {
    vColor = COL[gl_VertexIndex];
    gl_Position = vec4(POS[gl_VertexIndex], 0.0, 1.0);
}

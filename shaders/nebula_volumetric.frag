#version 460

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D blackHoleHdrTex;
layout(set = 0, binding = 1) uniform NebulaUBO {
    mat4 invViewProj;
    vec4 camPosTime;
    vec4 nebulaParams;
} ubo;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(hash(i + vec3(0,0,0)), hash(i + vec3(1,0,0)), f.x),
            mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
        mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
            mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y),
        f.z
    );
}

float nebulaDensity(vec3 p, float time, float turb) {
    vec3 q = vec3(
        noise(p * 0.08 + vec3(0.0, time * 0.015, 0.0)),
        noise(p * 0.08 + vec3(5.2, 1.3, time * 0.012)),
        noise(p * 0.08 + vec3(2.8, 7.1, 4.4))
    );
    vec3 warped = p * 0.065 + turb * (q - 0.5) * 4.5;
    float d = 0.0;
    float amp = 0.55;
    for (int i = 0; i < 4; ++i) {
        d += amp * noise(warped);
        warped = warped * 2.04 + vec3(3.1, -2.7, 1.9);
        amp *= 0.48;
    }
    float radialFade = exp(-dot(p, p) * 0.00045);
    return max(0.0, (d - 0.42) * 2.6 * radialFade);
}

float henyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(0.001, 1.0 + g2 - 2.0 * g * cosTheta), 1.5));
}

void main() {
    vec3 baseSpace = texture(blackHoleHdrTex, vUv).rgb;
    if (ubo.nebulaParams.x <= 0.01) {
        vec3 acesBase = (baseSpace * (2.51 * baseSpace + 0.03)) / (baseSpace * (2.43 * baseSpace + 0.59) + 0.14);
        outColor = vec4(pow(clamp(acesBase, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
        return;
    }

    vec2 ndc = vUv * 2.0 - 1.0;
    vec4 farP = ubo.invViewProj * vec4(ndc, 1.0, 1.0);
    vec3 ro = ubo.camPosTime.xyz;
    vec3 rd = normalize(farP.xyz / farP.w - ro);

    int steps = int(ubo.nebulaParams.w);
    float stepSize = 2.6;
    float t = 4.0 + hash(vec3(gl_FragCoord.xy, ubo.camPosTime.w)) * stepSize;

    vec3 lightPos = vec3(0.0, 2.0, 0.0);
    vec3 accum = vec3(0.0);
    float trans = 1.0;

    const vec3 H_ALPHA = vec3(0.95, 0.16, 0.34);
    const vec3 O_III   = vec3(0.08, 0.78, 0.92);
    const vec3 S_II    = vec3(1.00, 0.58, 0.14);

    for (int i = 0; i < steps; ++i) {
        vec3 p = ro + rd * t;
        float dens = nebulaDensity(p, ubo.camPosTime.w, ubo.nebulaParams.z) * ubo.nebulaParams.x;

        if (dens > 0.005) {
            vec3 toLight = lightPos - p;
            float lDist  = length(toLight);
            vec3 lDir    = toLight / max(lDist, 0.001);
            float phase  = henyeyGreenstein(dot(rd, lDir), 0.58);

            float shadowDens = nebulaDensity(p + lDir * 3.5, ubo.camPosTime.w, ubo.nebulaParams.z);
            float shadow     = exp(-shadowDens * 2.4);

            float ionZone = clamp(lDist / 38.0, 0.0, 1.0);
            vec3 emission = mix(O_III, mix(H_ALPHA, S_II, smoothstep(0.45, 1.0, ionZone)), ionZone);

            vec3 scatterLight = emission * (0.28 + 3.8 * phase * shadow / (1.0 + 0.004 * lDist * lDist));
            accum += trans * scatterLight * dens * ubo.nebulaParams.y * stepSize * 0.18;
            trans *= exp(-dens * stepSize * 0.14);
            if (trans < 0.03) break;
        }
        t += stepSize;
    }

    vec3 hdr = baseSpace * trans + accum;
    vec3 aces = (hdr * (2.51 * hdr + 0.03)) / (hdr * (2.43 * hdr + 0.59) + 0.14);
    outColor = vec4(pow(clamp(aces, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}

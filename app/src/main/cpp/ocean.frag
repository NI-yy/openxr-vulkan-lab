#version 450
layout(location = 0) in vec2 uv;
layout(location = 1) flat in int eyeIndex;
layout(location = 0) out vec4 outColor;

struct Eye {
    vec4 orientation;
    vec4 origin;
    // left, right, down, up tangents from XrFovf.
    vec4 fov;
};
layout(push_constant) uniform OceanParams {
    Eye eyes[2];
    vec4 settings; // x: output is an sRGB attachment
} params;

const vec3 sunDirection = normalize(vec3(0.08, 0.055, -1.0));

vec3 rotateByQuaternion(vec3 v, vec4 q) {
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

vec3 sky(vec3 direction, bool showSunDisc) {
    float height = max(direction.y, 0.0);
    float sunAngle = clamp(dot(direction, sunDirection), -1.0, 1.0);
    float haze = exp(-height * 8.0);
    float airMass = 1.0 / (height + 0.08);
    float solarTransmission = exp(-0.055 * airMass);

    // Cheap angular approximations to Rayleigh and forward Mie scattering.
    // The altitude-dependent haze and palette are art directed for sunset;
    // this is not an integration of the full atmosphere.
    float rayleighPhase = 0.75 * (1.0 + sunAngle * sunAngle);
    const float anisotropy = 0.76;
    float mieBase = 1.0 + anisotropy * anisotropy -
                    2.0 * anisotropy * sunAngle;
    float miePhase = (1.0 - anisotropy * anisotropy) /
                     (mieBase * sqrt(mieBase));
    vec3 upper = mix(vec3(0.03, 0.075, 0.22), vec3(0.12, 0.19, 0.38),
                     exp(-height * 2.3));
    vec3 distantHaze = mix(vec3(0.25, 0.14, 0.29),
                           vec3(1.05, 0.29, 0.09),
                           pow(max(sunAngle, 0.0), 2.0));
    vec3 color = mix(upper, distantHaze, haze * 0.78);
    color += vec3(0.065, 0.10, 0.19) * rayleighPhase *
             (1.0 - exp(-0.12 * airMass)) * (1.0 - haze * 0.5);
    color += vec3(1.0, 0.42, 0.12) * miePhase * haze *
             solarTransmission * 0.055;
    color += vec3(0.9, 0.44, 0.20) *
             pow(max(sunAngle, 0.0), 30.0) * haze * 0.16;
    if (showSunDisc)
        color += vec3(1.0, 0.60, 0.28) *
                 smoothstep(0.99965, 0.99982, sunAngle) * 3.2;
    return color;
}

// Suppress wave frequencies whose phase changes too much within one pixel.
// This is especially important near the horizon, where a pixel covers a large
// area of the sea. It avoids the long, flickering stripes of the first version.
float waveVisibility(vec2 frequency, vec2 footprint) {
    float phaseWidth = dot(abs(frequency), footprint);
    return exp(-0.20 * phaseWidth * phaseWidth);
}

void main() {
    Eye eye = params.eyes[eyeIndex];
    vec2 tangent = vec2(mix(eye.fov.x, eye.fov.y, uv.x),
                        mix(eye.fov.w, eye.fov.z, uv.y));
    vec3 direction = normalize(rotateByQuaternion(vec3(tangent, -1.0), eye.orientation));
    vec3 color = sky(direction, true);

    // The LOCAL-space sea lies below the initial eye position. Its world-space
    // coordinates keep the waves stable when the headset moves.
    float distanceToSea = (-1.5 - eye.origin.y) / min(direction.y, -0.001);
    vec2 point = eye.origin.xz + direction.xz * clamp(distanceToSea, 0.0, 2000.0);
    vec2 footprint = max(abs(dFdx(point)), abs(dFdy(point)));
    if (direction.y < -0.001 && distanceToSea > 0.0) {
        const vec2 k0 = vec2(0.72, 0.33);
        const vec2 k1 = vec2(-0.41, 1.17);
        const vec2 k2 = vec2(2.37, -1.36);
        const vec2 k3 = vec2(-3.93, -4.21);
        const vec2 bend0 = vec2(-0.24, 0.42);
        const vec2 bend1 = vec2(0.39, 0.18);
        float b0 = dot(point, bend0);
        float b1 = dot(point, bend1);
        float p0 = dot(point, k0) + 0.90 * sin(b0);
        float p1 = dot(point, k1) + 0.75 * sin(b1);
        float p2 = dot(point, k2) + 0.55 * sin(p0);
        float p3 = dot(point, k3) + 0.50 * sin(p1);
        vec2 dp0 = k0 + 0.90 * cos(b0) * bend0;
        vec2 dp1 = k1 + 0.75 * cos(b1) * bend1;
        float v0 = waveVisibility(k0, footprint);
        float v1 = waveVisibility(k1, footprint);
        float v2 = waveVisibility(k2, footprint);
        float v3 = waveVisibility(k3, footprint);
        vec2 slope = 0.080 * v0 * cos(p0) * dp0 +
                     0.050 * v1 * cos(p1) * dp1 +
                     0.014 * v2 * cos(p2) * (k2 + 0.55 * cos(p0) * dp0) +
                     0.005 * v3 * cos(p3) * (k3 + 0.50 * cos(p1) * dp1);
        slope *= 1.0 - smoothstep(65.0, 550.0, distanceToSea);
        vec3 normal = normalize(vec3(-slope.x, 1.0, -slope.y));
        vec3 reflected = reflect(direction, normal);
        float viewCosine = max(dot(-direction, normal), 0.0);
        float fresnel = 0.02 + 0.98 * pow(1.0 - viewCosine, 5.0);
        vec3 deepWater = vec3(0.013, 0.050, 0.073);
        color = mix(deepWater, sky(reflected, false), 0.10 + 0.86 * fresnel);

        // A rough microfacet highlight produces a continuous sun trail instead
        // of a mirror image of the sun disc in isolated large patches.
        vec3 halfVector = normalize(sunDirection - direction);
        float ndh = max(dot(normal, halfVector), 0.0);
        float roughness = mix(0.19, 0.34, clamp(length(footprint) * 0.18, 0.0, 1.0));
        float alpha2 = roughness * roughness;
        float denominator = ndh * ndh * (alpha2 - 1.0) + 1.0;
        float distribution = alpha2 / (3.14159265 * denominator * denominator);
        color += vec3(1.0, 0.56, 0.23) * distribution * fresnel * 0.32;

        float horizonFade = 1.0 - smoothstep(180.0, 1700.0, distanceToSea);
        color = mix(sky(normalize(vec3(direction.x, 0.003, direction.z)), false),
                    color, horizonFade);
    }

    // A luminance-based shoulder preserves the orange/blue hue in bright haze.
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = color / (1.0 + luminance);
    if (params.settings.x < 0.5) color = pow(color, vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}

#version 460
// Light probes on the cars (not in the original): the cars drawn in viewports
// of their own (CAR: the player's, the rivals near it) are not part of the
// world, and took neither its sun shadows nor anything of its surroundings:
// a car driving into the shade of the trees stayed as bright as in the open.
// A few points over each car (three slices along it: the rear, the middle,
// the front) measure how much of the sun reaches it there and how much of the
// sky is open above it; r3d.frag lights the car with them, blended along its
// length. One texel for each slice: r the sun (0..1), g the sky (0..1), a how
// far it moves the stored value (blended over it: the probes settle over a
// few images instead of flickering as the car passes a post's shadow).
// Traced through the world with ray tracing; without it, the sun from the
// shadow map and the sky left open.
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#ifdef RAY_TRACING
#extension GL_EXT_ray_query : require
#endif

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_probe;

layout(set = 0, binding = 0) uniform usampler2D texture_ram;
layout(set = 0, binding = 1) uniform sampler2D replacements[];
layout(set = 1, binding = 0) uniform sampler2D shadow_map;

#define ATMOSPHERE_BINDING 2
#include "atmosphere.glsl"
#include "r3d_texture.glsl"
#ifdef RAY_TRACING
#define RT_SET 0
#include "raytrace.glsl"
#endif

// How much of the sun reaches a point, from the shadow map (as the world's
// sun_shadow in r3d.frag: a tent over 4x4 texels)
float mapped_sun(vec3 pos) {
    vec4 lc = atm.shadow_matrix * vec4(pos, 1.0);
    vec2 st0 = lc.xy * 0.5 + 0.5;
    if (any(lessThan(st0, vec2(0.0))) || any(greaterThan(st0, vec2(1.0))) || lc.z <= 0.0) return 1.0;
    vec2 size = vec2(1.0 / atm.shadow.y);
    vec2 st = st0 * size - 0.5;
    vec2 b = floor(st), f = st - b;
    float wx[4] = float[4](1.0 - f.x, 1.0, 1.0, f.x);
    float wy[4] = float[4](1.0 - f.y, 1.0, 1.0, f.y);
    float shade = 0.0;
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 2; x++) {
            vec4 d = textureGather(shadow_map, (b + 2.0 * vec2(x, y)) / size, 0);
            vec4 hit = vec4(greaterThan(d, vec4(lc.z + atm.shadow.w)));
            shade += hit.w * wx[2 * x] * wy[2 * y] + hit.z * wx[2 * x + 1] * wy[2 * y] +
                     hit.x * wx[2 * x] * wy[2 * y + 1] + hit.y * wx[2 * x + 1] * wy[2 * y + 1];
        }
    vec2 edge = min(st0, 1.0 - st0);
    return mix(1.0, 1.0 - shade / 9.0, smoothstep(0.0, 0.08, min(edge.x, edge.y)));
}

void main() {
    int texel = int(gl_FragCoord.x);
    int car = texel / CAR_PROBES, slice = texel - car * CAR_PROBES;
    if (car >= int(atm.car_probe.x + 0.5)) discard;
    vec4 box0 = atm.car_box[car * 3], box1 = atm.car_box[car * 3 + 1], box2 = atm.car_box[car * 3 + 2];
    if (box2.y <= 0.0) discard;   // (a place with no car this image: what it holds stays)
    vec3 F = box1.xyz, U = normalize(atm.axes[1].xyz);
    vec3 S = normalize(cross(F, U));
    float half_len = box0.w, half_wid = box1.w, half_hgt = box2.x;
    // The slice: a little in from each end, where most of the bodywork is
    vec3 mid = box0.xyz + F * (float(slice - 1) * 0.6 * half_len);
    // Five points round it: three over the roof, one beside each flank
    vec3 points[5] = vec3[5](mid + U * (0.85 * half_hgt) - S * (0.7 * half_wid),
                             mid + U * (0.85 * half_hgt),
                             mid + U * (0.85 * half_hgt) + S * (0.7 * half_wid),
                             mid + U * (0.1 * half_hgt) - S * (1.05 * half_wid),
                             mid + U * (0.1 * half_hgt) + S * (1.05 * half_wid));
    vec3 L = normalize(atm.sun.xyz);
    float sun = 1.0, sky = 1.0;
    bool shadows = atm.shadow.x > 0.0 && atm.sun.w > 0.0;
#ifdef RAY_TRACING
    if (atm.rt.x > 0.0) {
        // The sun: four rays from each point over the sun's disc (the world
        // near the ground, as the road's own: RT_LOW), turned every image
        if (shadows) {
            vec3 X = normalize(cross(abs(L.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), L));
            vec3 Y = cross(L, X);
            float radius = atm.rt.y > 0.0 ? atm.rt.y : 0.016;
            float lit = 0.0;
            for (int p = 0; p < 5; p++)
                for (int i = 0; i < 4; i++) {
                    float k = float(p * 4 + i);
                    float a = (k * 0.618034 + atm.car_probe.w) * 6.2831853;
                    float r = sqrt((float(i) + 0.5) / 4.0) * radius;
                    vec3 dir = normalize(L + X * (r * cos(a)) + Y * (r * sin(a)));
                    lit += rt_light(points[p] + L * 0.05, dir, 2000.0, RT_LOW);
                }
            sun = mix(1.0, lit / 20.0, atm.shadow.x);
        }
        // The sky: how much of it is open above the roof (the world all
        // round, near and high, within a few car lengths: what stands over it)
        vec3 from = mid + U * (0.95 * half_hgt + 0.05);
        const int SKY = 16;
        float open = 0.0;
        for (int i = 0; i < SKY; i++) {
            float u = (float(i) + 0.5) / float(SKY);
            float a = (float(i) * 0.618034 + atm.car_probe.w) * 6.2831853;
            float r = sqrt(u);
            vec3 dir = normalize(S * (r * cos(a)) + F * (r * sin(a)) + U * sqrt(1.0 - u));
            open += rt_trace(from, dir, 8.0, RT_LOW | RT_HIGH, true) < 0.0 ? 1.0 : 0.0;
        }
        sky = open / float(SKY);
    } else
#endif
    if (shadows) {
        float lit = 0.0;
        for (int p = 0; p < 5; p++) lit += mapped_sun(points[p] + L * 0.05);
        sun = mix(1.0, lit / 5.0, atm.shadow.x);
    }
    out_probe = vec4(sun, sky, 1.0, box2.y);
}

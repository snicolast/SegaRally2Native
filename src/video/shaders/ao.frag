#version 460
// Ambient occlusion (not in the original), horizon based (HBAO): for each
// pixel of the game world, a few directions are walked across the depth
// buffer and the highest point seen above the surface in each tells how much
// of the sky the surrounding geometry hides. Output: 1 = open, 0 = enclosed.
// Built with RAY_TRACING it can instead trace short rays through the world
// (raytrace.glsl), which also sees what is off screen or hidden behind.
// The target's alpha is the occlusion; its colour, the light bounced off the
// world (traced_bounce), black without it.
// Drawn at half resolution: each pixel of the target is the top left pixel of
// a 2x2 block of the full-size image (ao_apply.frag fills in the others).
#extension GL_GOOGLE_include_directive : require
#ifdef RAY_TRACING
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_ray_query : require
#endif
#include "ao_common.glsl"
#ifdef RAY_TRACING
// The 3D pass's descriptor set, as set 1: the texture RAM and the texture pack
// for the cut-out triangles, the acceleration structure and the cut-outs
layout(set = 1, binding = 0) uniform usampler2D texture_ram;
layout(set = 1, binding = 1) uniform sampler2D replacements[];
#define RT_SET 1
#include "raytrace.glsl"
#define ATMOSPHERE_SET 1
#define ATMOSPHERE_BINDING 2
#include "atmosphere.glsl"

// Global illumination (not in the original; GLOBAL ILLUMINATION): the sun's
// light bounced once off the world onto this point. A few rays over the
// hemisphere above the surface (cosine weighted), each bringing back the
// light of the surface it meets (rt_bounce). Each pixel of a block of 4x4
// traces its own few of one spread of rays over the hemisphere, and
// ao_apply.frag puts the block together again
vec3 traced_bounce(vec3 p, vec3 n) {
    int rays = int(ao.gi.y);
    float reach = ao.gi.z;
    vec3 origin = p + n * (0.02 + 0.002 * -p.z);
    vec3 t = normalize(cross(abs(n.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), n));
    vec3 b = cross(n, t);
    vec3 L = normalize(atm.sun.xyz);
    float sun = atm.sun.w * (atm.sunlight.z > 0.0 ? atm.sunlight.x : 1.0);
    float ambient = ao.light.x * (atm.sunlight.z > 0.0 ? atm.sunlight.y : 1.0);
    // The pixel's place in the spread: neighbours far apart in it
    const int ORDER[16] = int[](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    ivec2 cell = ivec2(gl_FragCoord.xy) & 3;
    int first = ORDER[cell.x + 4 * cell.y], all = 16 * rays;
    vec3 sum = vec3(0.0);
    for (int i = 0; i < rays; i++) {
        int k = first + 16 * i;
        float u = (float(k) + 0.5) / float(all);
        float a = (float(k) * 0.618034 + ao.gi.w) * 6.2831853;
        float r = sqrt(u);
        vec3 dir = normalize(t * (r * cos(a)) + b * (r * sin(a)) + n * sqrt(1.0 - u));
        sum += rt_bounce(origin, dir, reach, L, sun, ambient);
    }
    return sum / float(rays);
}

// Short rays over the hemisphere above the surface (cosine weighted, turned
// per pixel): the nearer what they meet, the more it hides. a: how open the
// point is; rgb: with the global illumination, the colour of what hides it
// (the rays that meet something, weighted as they hide), for the colour of
// the shading (main)
vec4 traced_open(vec3 p, vec3 n, float noise) {
    bool coloured = ao.gi.x != 0.0;
    vec3 tint = vec3(0.0);
    float radius = ao.params.x;
    vec3 origin = p + n * (0.02 + 0.002 * -p.z);
    vec3 t = normalize(cross(abs(n.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), n));
    vec3 b = cross(n, t);
    int rays = int(ao.misc.z);
    // Much of the picture is open ground with nothing within the radius: ten
    // of the twenty rays go first, and where none of them meets anything the
    // others are not traced. They are the eight nearest the ground, which
    // reach farthest along it and so meet a wall or a kerb before any other
    // does, and two of the steep ones for what hangs overhead: the picture
    // is the same to within a pixel in a hundred thousand. (One ray in three,
    // seven in all, lost the faint outer edge of the shading at the foot of
    // the walls.) A fractional part in misc.z: all of them always, as before,
    // for comparing
    bool probe = fract(ao.misc.z) == 0.0 && rays >= 9;
    float hidden = 0.0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < rays; i++) {
            if (probe ? (i >= rays - 8 || i == 1 || i == 5) != (pass == 0) : pass == 1) continue;
            float u = (float(i) + 0.5) / float(rays);
            float a = (float(i) * 0.618034 + noise) * 6.2831853;
            float r = sqrt(u);
            vec3 dir = normalize(t * (r * cos(a)) + b * (r * sin(a)) + n * sqrt(1.0 - u));
            float hit;
            vec3 colour = vec3(0.0);
            if (coloured) {
                if (!rt_trace_surface(origin, dir, radius, RT_LOW | RT_HIGH, colour, hit)) hit = -1.0;
            } else {
                hit = rt_trace(origin, dir, radius, RT_LOW | RT_HIGH, false);
            }
            if (hit >= 0.0) {
                float w = (1.0 - hit / radius) * (1.0 - hit / radius);
                hidden += w;
                tint += colour * w;
            }
        }
        if (probe && hidden == 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
    }
    // (Twice the hidden share was too dark next to the screen-space version:
    // the foot of every board and the road under the stands)
    return vec4(hidden > 0.0 ? tint / hidden : vec3(0.0), 1.0 - clamp(hidden / float(rays) * 1.3, 0.0, 1.0));
}
#endif

layout(location = 0) out vec4 out_ao;

void main() {
    vec2 frag = floor(gl_FragCoord.xy) * 2.0 + 0.5;   // in full-size pixels
    vec3 p = ao_view_pos(frag);
    if (p.z >= 0.0) {   // no 3D here (2D backdrop)
        out_ao = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    // Surface normal from the neighbours on the side that continues the surface
    vec3 px0 = ao_view_pos(frag - vec2(1.0, 0.0)), px1 = ao_view_pos(frag + vec2(1.0, 0.0));
    vec3 py0 = ao_view_pos(frag - vec2(0.0, 1.0)), py1 = ao_view_pos(frag + vec2(0.0, 1.0));
    vec3 dx = abs(px1.z - p.z) < abs(p.z - px0.z) ? px1 - p : p - px0;
    vec3 dy = abs(py1.z - p.z) < abs(p.z - py0.z) ? py1 - p : p - py0;
    vec3 n = normalize(cross(dy, dx));
    if (dot(n, p) > 0.0) n = -n;   // towards the camera

    // Rotated per pixel (and per frame with DLAA, which averages it out)
    float noise = fract(52.9829189 * fract(dot(frag, vec2(0.06711056, 0.00583715))) + ao.misc.x);
    // Fogged surfaces lose their shading, so they lose the occlusion too
    float fog = clamp(ao.params.w + (-p.z) * ao.params.z, 0.0, 1.0);
    vec3 bounce = vec3(0.0);
#ifdef RAY_TRACING
    if (ao.gi.y > 0.0) bounce = traced_bounce(p, n) * (1.0 - fog);
    // misc.w 1: traced on the left half only, for comparing (LW_RTSPLIT)
    if (ao.misc.z > 0.0 && (ao.misc.w == 0.0 || frag.x < ao.rect.x + ao.rect.z * 0.5)) {
        // The same spread of rays for every pixel and every image: turned per
        // pixel, as the screen-space version below is, the traced rays left a
        // fine pattern of dots fixed to the screen; turned per image (for DLAA
        // to average), the darkening in corners (the foot of the roadside
        // boards) changed from image to image and flickered
        vec4 traced = traced_open(p, n, 0.0);
        float open = mix(1.0, traced.a, 1.0 - fog);
        // Global illumination: what hides the sky from a corner is itself
        // lit, and throws some of its light into it: the shading takes the
        // colour of what casts it (reddish at the foot of a red board) and is
        // a little lighter, not a grey veil. Half the light hidden comes back
        // in that colour; kept with the bounced light, as the share of the
        // shaded colour it is (ao_apply.frag multiplies the two)
        if (ao.gi.x != 0.0) {
            float shaded = mix(1.0, open, ao.params.y);
            bounce += traced.rgb * (0.5 * ao.params.y * (1.0 - open) / (abs(ao.gi.x) * shaded));
        }
        out_ao = vec4(bounce, open);
        return;
    }
#endif
    float radius = ao.params.x;
    // The radius on screen, limited so close-ups stay cheap and local
    float radius_px = min(radius * abs(ao.proj.y) * 0.5 * ao.rect.w / -p.z, ao.misc.y);
    if (radius_px < 1.5 || ao.params.y <= 0.0) {
        out_ao = vec4(bounce, 1.0);
        return;
    }
    const int DIRECTIONS = 8, STEPS = 4;
    float occlusion = 0.0;
    for (int i = 0; i < DIRECTIONS; i++) {
        float angle = (float(i) + noise) * (6.2831853 / float(DIRECTIONS));
        vec2 dir = vec2(cos(angle), sin(angle));
        float horizon = 0.0;
        for (int s = 0; s < STEPS; s++) {
            float t = (float(s) + 0.5 + 0.5 * fract(noise * 3.7 + float(s) * 0.618)) / float(STEPS) * radius_px;
            vec3 q = ao_view_pos(frag + dir * t);
            if (q.z >= 0.0) continue;
            vec3 h = q - p;
            float d2 = dot(h, h);
            float elevation = dot(n, h) * inversesqrt(d2 + 1e-6);
            float falloff = clamp(1.0 - d2 / (radius * radius), 0.0, 1.0);
            horizon = max(horizon, (elevation - 0.3) * falloff);
        }
        occlusion += horizon;
    }
    float open = 1.0 - clamp(occlusion / float(DIRECTIONS) * 2.4, 0.0, 1.0);
    out_ao = vec4(bounce, mix(1.0, open, 1.0 - fog));
}

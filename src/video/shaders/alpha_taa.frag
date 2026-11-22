#version 450
// The alpha of the 3D image (where the 2D background shows through) with
// DLAA: smoothed over time by a plain temporal filter. A second DLAA over the
// alpha did the same for 1.6 ms at 4K (RTX 5070 Ti), this about 0.25 ms
// (GitHub issue #14). The alpha is drawn with DLAA's sub-pixel
// jitter; each pixel blends the new value into its own history, found where
// the surface was in the previous image (the DLAA motion vectors) and kept
// within the range of the new 3x3 neighbourhood so it cannot ghost.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform sampler2D current;   // jittered alpha (grey view of the 3D image)
layout(set = 0, binding = 1) uniform sampler2D motion;    // pixels to where each surface was in the previous image
layout(set = 0, binding = 2) uniform sampler2D history;   // last image's result

layout(push_constant) uniform Taa {
    vec4 p;   // xy: texel size of the 3D image; z: the share of it drawn, from its top left corner (dynamic
              // resolution), negative: no history (start, resize); w: weight of the new value
} pc;

void main() {
    vec2 t = pc.p.xy;
    float drawn = abs(pc.p.z);
    vec2 at = uv * drawn;   // in the 3D image (uv: in the picture shown)
    float c = texture(current, at).r;
    float lo = c, hi = c;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            float s = texture(current, at + t * vec2(x, y)).r;
            lo = min(lo, s);
            hi = max(hi, s);
        }
    // (motion in pixels of the image drawn: t / drawn is one of them across the picture shown)
    vec2 back = uv + texture(motion, at).rg * t / drawn;
    if (pc.p.z < 0.0 || any(lessThan(back, vec2(0.0))) || any(greaterThan(back, vec2(1.0)))) {
        color = vec4(c);
        return;
    }
    float h = clamp(texture(history, back).r, lo, hi);
    color = vec4(mix(h, c, pc.p.w));
}

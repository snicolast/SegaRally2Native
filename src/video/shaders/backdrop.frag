#version 450
// The menu's backdrop, moving under the menu layer (ui_overlay.cpp): the
// picture covering the window with a slow zoom and drift, specks of dust
// floating up through it in three depths, and now and then a soft band of
// light crossing it diagonally
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform sampler2D pic;
layout(push_constant) uniform Params {
    float t;               // seconds
    float screen_aspect;   // width / height of the window
    float pic_aspect;      // ... of the picture
    float anchor_x;        // the picture's crop when the window is narrower: 0 keeps its left side
    float height;          // window height in pixels
    float blurred;         // 1: the blurred picture behind the settings
} pc;

const float TAU = 6.2831853;

// The picture covering the window (cut at the sides or at the top and bottom)
vec2 cover(vec2 s) {
    float sa = pc.screen_aspect, pa = pc.pic_aspect;
    if (sa > pa) return vec2(s.x, 0.5 + (s.y - 0.5) * pa / sa);
    return vec2(pc.anchor_x * (1.0 - sa / pa) + s.x * sa / pa, s.y);
}

vec4 hash4(vec2 p) {
    vec4 q = fract(vec4(p.xyxy) * vec4(0.1031, 0.1030, 0.0973, 0.1099));
    q += dot(q, q.wzxy + 33.33);
    return fract((q.xxyz + q.yzzw) * q.zywx);
}

// One depth of dust: at most one speck per cell, drifting up and to the
// right, wobbling and twinkling. p is in window heights (x 0..aspect, y 0..1)
float dust(vec2 p, float cells, float speed, float radius, float soft, float seed) {
    float t = pc.t;
    vec2 q = p * cells + vec2(-0.35 * speed * cells * t, speed * cells * t) + seed * 17.0;
    vec2 cell = floor(q), f = fract(q);
    vec4 h = hash4(cell + seed * 31.0);
    if (h.w > 0.42) return 0.0;   // most cells are empty
    vec2 c = 0.22 + 0.56 * h.xy +
             0.14 * vec2(sin(t * (0.35 + 0.3 * h.z) + h.x * TAU), cos(t * (0.27 + 0.25 * h.y) + h.z * TAU));
    float r = radius * (0.6 + 0.8 * h.z);
    float d = length(f - c);
    float a = 1.0 - smoothstep(r * (1.0 - soft), r, d);
    float twinkle = 0.55 + 0.45 * sin(t * (0.8 + 1.6 * h.y) + h.x * 50.0);
    return a * twinkle * (0.5 + 0.5 * h.z);
}

void main() {
    float t = pc.t;
    // Ken Burns: a slow breathing zoom and a drift, never showing past the edges
    float zoom = 1.07 + 0.025 * sin(TAU * t / 38.0);
    vec2 pan = vec2(0.012 * sin(TAU * t / 47.0), 0.008 * cos(TAU * t / 41.0));
    vec2 centre = vec2(0.3, 0.5);   // about the car: its front stays in the window
    vec2 s = centre + (uv - centre) / zoom + pan;
    vec3 c = texture(pic, cover(s)).rgb;

    // The light: a wide soft band crossing from the top left every 11 s (in 5 s),
    // brighter where the picture already is
    vec2 p = vec2(uv.x * pc.screen_aspect, uv.y);
    float diag = (p.x * 0.75 + p.y * 0.55);
    float phase = fract(t / 11.0) / 0.45;
    if (phase < 1.0) {
        float span = pc.screen_aspect * 0.75 + 0.55;
        float pos = mix(-0.5, span + 0.5, phase);
        float band = exp(-pow((diag - pos) / 0.16, 2.0));
        float lum = dot(c, vec3(0.299, 0.587, 0.114));
        c += band * 0.10 * (0.35 + lum) * vec3(1.0, 0.93, 0.80);
    }

    // The dust: far (small, slow), middle, near (big, soft, faster)
    float px = 1.0 / max(pc.height, 1.0);
    float k = 1.0 + pc.blurred;   // behind the settings: bigger and softer, as out of focus
    float far = dust(p, 22.0, 0.008, max(0.05, 2.5 * px * 22.0) * k, 0.7, 1.0);
    float mid = dust(p, 13.0, 0.016, 0.055 * k, 0.75, 2.0);
    float near = dust(p, 7.0, 0.032, 0.075 * k, 0.95, 3.0);
    float lift = 0.55 + 0.45 * smoothstep(0.0, 0.7, uv.y);   // fewer towards the sky
    float motes = (0.16 * far + 0.24 * mid + 0.32 * near) * lift / k;
    c += motes * vec3(1.0, 0.86, 0.62);

    color = vec4(c, 1.0);
}

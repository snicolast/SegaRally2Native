// Optional atmosphere, shared by the 3D pass (in front of the polygons) and
// the composition (in front of the 2D sky). Not part of the original hardware.
//
// Distance haze (--haze): aerial perspective that tints and desaturates the
// scenery towards the colour of the horizon as it gets farther.

#ifndef ATMOSPHERE_SET
#define ATMOSPHERE_SET 0
#endif
layout(set = ATMOSPHERE_SET, binding = ATMOSPHERE_BINDING) uniform Atmosphere {
    vec4 axes[3];    // world x, y (up), z axes in view space
    vec4 camera;     // camera position in the tracked world frame; w = time in seconds
    vec4 sun;        // direction towards the sun in view space; w = sun intensity
    vec4 view;       // projection of the world viewport: x and y scale, x and y offset
    vec4 haze;      // horizon colour; a = haze strength (0 = off)
    vec4 range;      // haze: distance where the tint starts, distance of the farthest scenery, desaturation
    vec4 backdrop;   // 3D target: 1 / width, 1 / height
    vec4 scroll_fog; // the original's scroll fog: colour, a = amount (0 = none)
    vec4 scroll_rect;// its rectangle in the 3D image: x0, y0, x1, y1 (fractions)
    mat4 shadow_matrix;  // sun shadows: view space -> shadow map (xy: -1..1, z: 0 far .. 1 near the sun)
    vec4 shadow;     // strength (0 = off), texel size, normal offset (view units), depth bias
    vec4 soft;       // soft particles: x 1 = on, y: distance over which they fade into what is behind (view units);
                     // z: 1 = HDR lighting (no clipping at white in the 3D image)
    vec4 mist;       // light in the game's fog: x strength (0 = off), y brighter upwards, z drifting banks
    vec4 sunlight;   // clear outdoor scenes (SUN): x sun gain, y ambient gain, z warmth (0 = the game's own light)
    vec4 spot;       // spotlight of the world viewport (spec 7.1): centre as view slopes (x / -z, y / -z), 1 / radii
    vec4 spot_color; // its light colour (0 = none); w: distance where it starts
    vec4 spot_fog;   // its colour on the fog (spot-on-fog colour * fog attenuation * fog colour); w: extent
    vec4 gi;         // global illumination on the player's car (r3d.frag) and on the dust (r3d.vert): y strength (0 = off),
                     // z how many rays for each of the car's pixels, w their reach (view units)
    vec4 mirror;     // w: 1 = ray traced reflections on the cars (r3d.frag)
    vec4 rt;         // ray traced sun shadows: x 1 = on (2: left half only), y size of the sun (tangent of its radius),
                     // z rays per point, w noise seed (changes every image with DLAA)
    vec4 dust_mv;    // DLAA/DLSS: x 1 = the dust writes its own motion vectors where it is dense (r3d.frag),
                     // fully from opacity z, none below y
    // Light probes on the cars drawn in viewports of their own (CAR; car_probe.frag, r3d.frag):
    // x how many cars have them (0 = off), y how much of the probes' sun the cars take,
    // z how much of their sky, w the probes' rays turned this image (0..1)
    vec4 car_probe;
    // Each car's box (CAR_PROBE_CARS of them), three each: [0] its middle (view space), w half its
    // length; [1] its length's direction (unit), w half its width; [2] x half its height, y how
    // far this image's probes move the stored ones (1 = taken as they are: a car just come)
    vec4 car_box[12];
    // x 1: the same cars take the world's sun shadows pixel by pixel too, from the
    // scenery only (r3d.frag), the probes' sun then left to their glint and fill
    vec4 car_shadow;
} atm;
const int CAR_PROBE_CARS = 4, CAR_PROBES = 3;   // (R3DRenderer::CAR_PROBE_CARS, CAR_PROBES)

// Spotlight lobe across the view (spec 7.1) for a view direction given as
// slopes (x / -z, y / -z): the original's ellipse 1 - r^2
float spot_lobe(vec2 slope) {
    vec2 e = (slope - atm.spot.xy) * atm.spot.zw;
    return max(1.0 - dot(e, e), 0.0);
}

// Scroll fog of the original hardware over a colour of the 2D layers behind
// the 3D image, at a position in the 3D image (fractions); light: mist_light()
// in that direction
float scroll_fog_amount(vec2 p) {
    bool inside = all(greaterThanEqual(p, atm.scroll_rect.xy)) && all(lessThan(p, atm.scroll_rect.zw));
    return inside ? atm.scroll_fog.a : 0.0;
}

vec3 scroll_fog(vec3 c, vec2 p, vec3 light) {
    float a = scroll_fog_amount(p);
    if (a > 0.0) c = mix(c, atm.scroll_fog.rgb * light, a);
    return c;
}

float atm_hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float atm_noise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(atm_hash(i + vec3(0, 0, 0)), atm_hash(i + vec3(1, 0, 0)), f.x),
                   mix(atm_hash(i + vec3(0, 1, 0)), atm_hash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(atm_hash(i + vec3(0, 0, 1)), atm_hash(i + vec3(1, 0, 1)), f.x),
                   mix(atm_hash(i + vec3(0, 1, 1)), atm_hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

// Light in the game's own fog (not in the original): instead of one flat
// colour it gets brighter upwards, where the light comes in between the
// treetops, with big banks drifting slowly. It depends on the view direction
// only, so a fogged surface and the fogged sky behind it always match and
// scenery the fog hides stays hidden. Multiplies the fog colour.
vec3 mist_light(vec3 dir_view) {
    if (atm.mist.x <= 0.0) return vec3(1.0);
    vec3 d = vec3(dot(atm.axes[0].xyz, dir_view), dot(atm.axes[1].xyz, dir_view), dot(atm.axes[2].xyz, dir_view));
    d /= max(length(d), 1e-6);
    float up = smoothstep(-0.2, 0.75, d.y);
    // The banks drift slowly
    vec3 drift = vec3(1.0, 0.15, 0.6) * atm.camera.w * 0.012;
    float n = 0.65 * atm_noise(d * 2.5 + drift) + 0.35 * atm_noise(d * 6.0 - drift * 1.7 + vec3(3.1, 7.7, 1.3));
    float m = 1.0 + atm.mist.y * (up - 0.2) + atm.mist.z * (n - 0.5) * 2.0;
    return vec3(mix(1.0, max(m, 0.0), atm.mist.x));
}

// Aerial perspective for a world surface at a view-space point, with its
// colour; game_fog: how much of it the game's own fog already covers
vec3 distance_haze(vec3 c, vec3 view_point, float game_fog) {
    float d = length(view_point);
    float far = atm.range.y;
    // Tint and desaturation grow steadily with distance, past the near scenery
    float k = atm.haze.a * 0.4 * (1.0 - exp(-2.0 * max(d - atm.range.x, 0.0) / far));
    // Where the game's fog already hides the scenery the haze adds nothing:
    // a fully fogged surface keeps the fog colour, like the fogged sky behind
    // it, so far scenery the game moves around (the T-Rex chase loops the
    // forest) cannot show through as a lighter silhouette
    k *= 1.0 - game_fog;
    // Sega Rally 2 draws its sky as a dome of polygons several times as far
    // as the farthest scenery: hazed like scenery, all of it lost a third of
    // its blue, up to the top of the picture (a pale, greyish sky, and with
    // it a picture that looked yellow beside the arcade's). What is that far
    // takes the haze near the horizon only, where the air is thickest
    float beyond = smoothstep(1.5 * far, 3.0 * far, d);
    if (beyond > 0.0) {
        float up = dot(view_point / d, normalize(atm.axes[1].xyz));
        k *= mix(1.0, 1.0 - smoothstep(0.02, 0.22, up), beyond);
    }
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(c, vec3(luma), atm.range.z * k);
    c = mix(c, atm.haze.rgb, k);
    return c;
}

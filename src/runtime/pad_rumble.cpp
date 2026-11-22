// The pads' rumble (pad_rumble.h), once a game frame from runtime.cpp.
//
// In a race it comes from what the player's car does, read from the game's
// RAM (docs/game_ram.md; the same car object as the telemetry):
//   [0x2242B0]  the player's car object
//      +0x00 float x3  position, metres (y up)
//      +0x0C float x3  velocity, metres per game frame (y stays 0)
//      +0x18 s16 x3    pitch, yaw (minus the heading), roll; 65536 = 360 deg
//      +0x64 float x4  wheel speeds, metres per game frame
//   0x22F3A8  float    speed, m/s
//
// Five parts:
//   ground  the surface under the wheels (asphalt, gravel, mud, snow, the
//           Mountain bridge's planks), from the game's own ground type
//   rough   the body's jolts: the frame-to-frame changes of its height,
//           pitch and roll (bumps; the game smooths the body, so small)
//   slide   the car sliding sideways: the angle between where it points and
//           where it goes, at speed
//   hit     a crash or a hard landing: a jump in the acceleration far above
//           what driving does, then fading
//   spin    the wheels spinning faster than the car goes (pulling away)
// The arcade wheel's own vibration (the drive board's, `arcade`) is kept as a
// small part only: it is what made the pads busy on straights and quiet in
// slides, since that wheel goes light when the car slides.
//
// The triggers: the brake trembles while it slows the car, the accelerator
// while the wheels spin, and both carry the rough surface, the slides and the
// hits whatever the pedals do (more with the pedal pressed). Outside a race
// (the selects, replays, the attract) the triggers are still; the pad itself
// keeps the drive board's jolts of the select screens, as the arcade wheel.
//
// SR2_RUMBLE_LOG prints the parts every frame they are not all zero.
#include "pad_rumble.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game_ram.h"
#include "inputs.h"
#include "ppc_rt.h"

std::atomic<float> g_pad_motor[2] = {0.0f, 0.0f};

namespace {

using game_ram::SEQUENCE, game_ram::PHASE, game_ram::G_CAR_STATUS, game_ram::PLAYER_CAR, game_ram::SPEED, game_ram::IN_AIR;
constexpr float HZ = 60.0f;                  // the game's frames a second
constexpr float G = 9.81f;
constexpr float ANGLE = 6.2831853f / 65536.0f;

float rdf(uint32_t a) {
    uint32_t u = rd32(a);
    float f;
    memcpy(&f, &u, 4);
    return std::isfinite(f) ? f : 0.0f;
}

void bump(std::atomic<float>& a, float v) {
    if (v > a.load()) a = v;
}

struct State {
    bool have = false;      // last frame read (two frames for the jolts)
    int frames = 0;
    float y = 0, vy = 0;
    float vx = 0, vz = 0;
    int16_t ang[3] = {};
    int16_t dang[3] = {};
    float rough = 0, hit = 0;
    float hvx[8] = {}, hvz[8] = {};   // the last frames' velocity (m/s), for the hits
    float hspeed[16] = {};            // and speed, for the crashes
};
State s;

}  // namespace

void pad_rumble_frame(float arcade, bool attract) {
    static const bool log = getenv("SR2_RUMBLE_LOG") != nullptr;
    const uint32_t car = rd32(PLAYER_CAR);
    const bool racing = !attract && rd32(SEQUENCE) == 1 && rd32(PHASE) == 1 && car != 0 && car < 0x800000 &&
                        int32_t(rd32(G_CAR_STATUS)) == 1;
    if (!racing) {
        s = State{};
        bump(g_pad_motor[0], arcade);   // the select screens' jolts, as before
        bump(g_pad_motor[1], 0.6f * arcade);
        return;
    }

    float speed = rdf(SPEED);
    if (speed < 0 || speed > 150) speed = 0;
    const float y = rdf(car + 0x04);
    const float vx = rdf(car + 0x0C) * HZ, vz = rdf(car + 0x14) * HZ;
    int16_t ang[3];
    for (int i = 0; i < 3; i++) ang[i] = int16_t(rd16(car + 0x18 + 2 * i));

    float rough_now = 0, hit_now = 0, dv6 = 0;
    float raw_y = 0, raw_p = 0, raw_r = 0;   // (for the log)
    if (s.have) {
        const float vy = (y - s.y) * HZ;
        int16_t dang[3];
        for (int i = 0; i < 3; i++) dang[i] = int16_t(ang[i] - s.ang[i]);
        if (s.frames >= 2) {
            // Jolts: the change of the vertical speed (m/s per frame) and of the
            // pitch and roll rates (degrees per frame, per frame)
            const float jolt_y = std::fabs(vy - s.vy);
            const float jolt_p = std::fabs(float(int16_t(dang[0] - s.dang[0]))) * ANGLE * 57.3f;
            const float jolt_r = std::fabs(float(int16_t(dang[2] - s.dang[2]))) * ANGLE * 57.3f;
            raw_y = jolt_y, raw_p = jolt_p, raw_r = jolt_r;
            rough_now = std::min(1.0f, jolt_y / 0.6f + (jolt_p + jolt_r) / 0.8f);
            // A hit: the velocity changing (speed or direction) far more over
            // the last 6 frames (0.1 s) than grip and brakes can: a wall, a
            // tree, another car, a rock; or a hard landing. Over 6 frames, as
            // the game's velocity jitters from frame to frame (in fast bends
            // one frame alone read 2-4 G). In Sergi's Mountain laps driving
            // changed it by up to ~8 m/s in 0.1 s (p99: hard braking, tight
            // bends); the fence hits by 28-33
            const int old = (s.frames + 2) & 7;   // 6 frames ago
            const float dx = vx - s.hvx[old], dz = vz - s.hvz[old];
            dv6 = s.frames >= 8 ? std::sqrt(dx * dx + dz * dz) : 0.0f;
            const float land = std::max(0.0f, vy - s.vy) * HZ / G;   // pushed up
            hit_now = std::clamp(std::max((dv6 - 8.0f) / 10.0f, (land - 6.0f) / 8.0f), 0.0f, 1.0f);
        }
        s.vy = vy;
        memcpy(s.dang, dang, sizeof dang);
    }
    // Rough rises at once and lingers a few frames, so single jolts are felt
    s.rough = std::max(rough_now, s.rough * 0.75f);
    // Landings: 0x22F394 is 1 while the player's car is in the air (the game
    // then stops the rolling sound and counts the frames, and plays its
    // landing thump on touching down, fn_0003D7F4). A thump as strong as the
    // flight was long: a hop is small, a long jump the full kick
    static int air = 0;
    if (rd32(IN_AIR) == 1) air++;
    else {
        if (air >= 6) hit_now = std::max(hit_now, std::clamp(float(air) / 40.0f, 0.3f, 1.0f));
        air = 0;
    }
    // (Crashes: the game's are soft, 15-28 m/s lost over a few tenths with
    // no jump in one frame; a speed-loss rule did not feel right to Sergi, so
    // for now only the big jolts above and the landings kick)
    // (held ~0.5 s, so a crash is told apart from the gravel's buzz)
    s.hit = std::max(hit_now, s.hit * 0.94f);

    // Slide: where the car points against where it goes
    float slide = 0, side_a = 0, side_b = 0;
    if (speed > 6.0f) {
        const float heading = -float(ang[1]) * ANGLE;
        const float fx = std::sin(heading), fz = std::cos(heading);
        const float v = std::sqrt(vx * vx + vz * vz);
        if (v > 1.0f) {
            // (sin of the slip angle; the side of x is a reading of the code,
            // so both ways are worked out and the smaller taken: the wrong one
            // reads a big slip on any heading but the axes)
            side_a = std::fabs(fx * vz - fz * vx) / v;
            side_b = std::fabs(-fx * vz - fz * vx) / v;
            const float side = std::min(side_a, side_b);
            slide = std::clamp((side - 0.25f) / 0.4f, 0.0f, 1.0f) * std::min(1.0f, speed / 20.0f);
        }
    }

    // Wheelspin: the fastest wheel against the car's speed
    float wheel = 0;
    for (int w = 0; w < 4; w++) wheel = std::max(wheel, std::fabs(rdf(car + 0x64 + 4 * w)) * HZ);
    // (only pulling away with the throttle down: in the air the wheels spin free)
    const float gas_now = std::clamp(float(g_inputs.accel) / 255.0f, 0.0f, 1.0f);
    const float spin = gas_now > 0.5f && speed < 25.0f ? std::clamp((wheel - speed - 2.0f) / 10.0f, 0.0f, 1.0f) : 0.0f;

    // Braking: slowing down, m/s per second
    static float decel = 0;
    static float prev_speed = 0;
    if (s.frames == 0) prev_speed = speed;
    decel += 0.3f * (std::clamp((prev_speed - speed) * HZ, -40.0f, 40.0f) - decel);
    prev_speed = speed;

    s.y = y, s.vx = vx, s.vz = vz;
    s.hvx[s.frames & 7] = vx, s.hvz[s.frames & 7] = vz;
    s.hspeed[s.frames & 15] = speed;
    memcpy(s.ang, ang, sizeof ang);
    s.have = true;
    s.frames++;

    // The ground under each wheel: car +0x46 (+0x52 of the game's task block,
    // which the pointer at 0x2242B0 passes by 0xC), a nibble a wheel (0, 1 the
    // front), the low 4 bits of the attribute of the road polygon it stands on
    // (fn_00051F80); the game reads it for grip (table 0x135504), the tyre
    // sounds and the noise it puts in the arcade wheel's force (table
    // 0x1354CC: 500 asphalt, 10000 gravel, 12000 the roughest). Pairs:
    //   0-1 asphalt   2-3, 12-13 gravel/dirt   4-5 smooth   6-7 roughest (mud)
    //   8-9 hard (concrete, planks)   10-11 snow/ice
    // A reading of the code, the names not checked in a race. Each gives a
    // texture: how hard (heavy motor, buzz motor) and how grainy (random from
    // frame to frame)
    struct Ground { float heavy, buzz, grain; };
    static const Ground GROUND[16] = {
        {0.00f, 0.06f, 0.2f}, {0.00f, 0.06f, 0.2f},   // asphalt: almost still
        {0.55f, 0.65f, 0.8f}, {0.55f, 0.65f, 0.8f},   // gravel
        {0.00f, 0.03f, 0.1f}, {0.00f, 0.03f, 0.1f},   // smooth
        {0.75f, 0.55f, 0.9f}, {0.75f, 0.55f, 0.9f},   // roughest (mud, off the road)
        {0.05f, 0.12f, 0.4f}, {0.05f, 0.12f, 0.4f},   // hard
        {0.45f, 0.40f, 0.7f}, {0.45f, 0.40f, 0.7f},   // snow: heavy, with a crunch
        {0.55f, 0.65f, 0.8f}, {0.55f, 0.65f, 0.8f},   // gravel (the other set)
        {0, 0, 0}, {0, 0, 0}};
    const uint32_t ground_bits = rd16(car + 0x46);
    float g_heavy = 0, g_buzz = 0, g_grain = 0;
    for (int w = 0; w < 4; w++) {
        const Ground& g = GROUND[(ground_bits >> (4 * w)) & 15];
        g_heavy += 0.25f * g.heavy, g_buzz += 0.25f * g.buzz, g_grain += 0.25f * g.grain;
    }
    // The bridge: the game plays its own sound on ground 2 in track sections
    // 0x1C-0x21 and 0x94-0x98 (car +0x48, fn_0003C834): the planks, a knock
    // every metre and a half or so
    const uint32_t section = rd32(car + 0x48);
    const bool bridge = ((ground_bits & 15) == 2 || ((ground_bits >> 4) & 15) == 2) &&   // (a front wheel, as the game)
                        ((section >= 0x1C && section <= 0x21) || (section >= 0x94 && section <= 0x98));
    // (the knocks: on 2 frames, softer 3, about 12 a second: at racing speed
    // the planks pass faster than the motors could follow one by one)
    static int plank = 0;
    float knock = 0;
    if (bridge) knock = (plank++ % 5) < 2 ? 1.0f : 0.3f;
    else plank = 0;
    static uint32_t seed = 12345;
    seed = seed * 1664525u + 1013904223u;
    const float rnd = float(seed >> 8) / float(1 << 24);
    const float pace = std::min(1.0f, speed / 20.0f);   // nothing standing, all of it from 72 km/h
    const float grain = 1.0f - g_grain * rnd;
    const float ground_heavy = std::max(knock, g_heavy * pace * grain);
    const float ground_buzz = std::max(0.6f * knock, g_buzz * pace * (1.0f - 0.5f * g_grain * rnd));

    const float rough = s.rough * std::min(1.0f, 0.3f + speed / 15.0f);
    const float hit = s.hit;
    const float road = 0.35f * arcade;

    // The pad: the low motor carries the heavy things (hits, rough ground,
    // slides), the high one the fine buzz
    const float low = std::max({hit, 0.5f * rough, 0.3f * slide, road, 0.35f * spin, ground_heavy});
    const float high = std::max({hit, 0.4f * rough, 0.15f * slide, 0.6f * road, 0.25f * spin, ground_buzz});
    bump(g_pad_motor[0], low);
    bump(g_pad_motor[1], high);

    // The triggers
    const float gas = std::clamp(float(g_inputs.accel) / 255.0f, 0.0f, 1.0f);
    const float brake = std::clamp(float(g_inputs.brake) / 255.0f, 0.0f, 1.0f);
    float left = 0, right = 0;
    if (brake > 0.05f && speed > 3.0f)
        left = brake * std::min(1.0f, speed / 25.0f) * (0.2f + 0.8f * std::clamp(decel / 15.0f, 0.0f, 1.0f));
    right = 0.6f * spin * std::max(gas, 0.3f);
    const float shared = std::max({0.45f * rough, 0.3f * slide, hit, 0.9f * std::max(ground_heavy, ground_buzz)});
    left = std::max(left, shared * (0.45f + 0.55f * brake));
    right = std::max(right, shared * (0.45f + 0.55f * gas));
    bump(g_trigger_rumble[0], left);
    bump(g_trigger_rumble[1], right);

    if (log)   // every race frame, with the raw jolts (to tune the thresholds)
        printf("[rumble] ground %04X game %X%X%X%X slip %.2f/%.2f dv6 %.2f gas %.2f brake %.2f vx %.3f vz %.3f section %3u%s  speed %5.1f  y %6.3f  x %7.2f  z %7.2f  jolt y %.3f p %.3f r %.3f  rough %.2f  slide %.2f  hit %.2f  spin %.2f  arcade %.2f  -> pad %.2f/%.2f  triggers %.2f/%.2f\n",
               ground_bits, rd32(0x1B0C7C), rd32(0x1B0C78), rd32(0x1B0C74), rd32(0x1B0C70), side_a, side_b, dv6, g_inputs.accel / 255.0f, g_inputs.brake / 255.0f, vx, vz, section, bridge ? " BRIDGE" : "", speed, y, rdf(car + 0x00), rdf(car + 0x08), raw_y, raw_p, raw_r, rough, slide, hit, spin, arcade, low, high, left, right);
}

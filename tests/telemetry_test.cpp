// OGS-TS telemetry (telemetry.cpp): the real sender run over a fake game RAM, its datagrams caught by the
// test seam. Checks the envelope and the 1,400-byte budget, the car's orientation (read back with a small
// Cap'n Proto reader: the vendored writer only writes), and that a crash sends events and driving, braking
// and a finish do not.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../third_party/ogs_telemetry/OgsTelemetryFrame.h"
#include "inputs.h"
#include "ppc_rt.h"
#include "telemetry_ogs.h"

static int g_failed = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAILED line %d: %s\n", __LINE__, #cond);            \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

// ---- the game's RAM, as telemetry.cpp reads it ------------------------------------------------------

static std::vector<uint8_t> g_memory(RAM_SIZE);
uint8_t* g_ram = g_memory.data();
uint8_t rt_rd8_slow(uint32_t) { return 0; }
uint16_t rt_rd16_slow(uint32_t) { return 0; }
uint32_t rt_rd32_slow(uint32_t) { return 0; }
void rt_wr8_slow(uint32_t, uint8_t) {}
void rt_wr16_slow(uint32_t, uint16_t) {}
void rt_wr32_slow(uint32_t, uint32_t) {}

constexpr uint32_t SEQUENCE = 0x22D618, PHASE = 0x22D624, GAME = 0x22CF14, CAR = 0x300000, SPEED = 0x22F3A8;

constexpr uint32_t VISIBLE_COUNT = 0x225A2C, VISIBLE_CARS = 0x225A30, RIVAL = 0x310000;

static void put_f(uint32_t a, float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    wr32(a, u);
}

// One other car in the game's list of cars in view, `x` metres to the player's right and `z` ahead (the player
// is at the origin, heading +z), driving at `speed` m/s along +z
static void put_rival(float x, float z, float speed = 60.0f) {
    wr32(VISIBLE_COUNT, 1), wr32(VISIBLE_CARS, RIVAL);
    put_f(RIVAL + 0x00, x), put_f(RIVAL + 0x04, 0.0f), put_f(RIVAL + 0x08, z);
    put_f(RIVAL + 0x0C, 0.0f), put_f(RIVAL + 0x14, speed / 60.0f);
}

// A race: the car at `speed` m/s on a heading (0 is +z; the game stores yaw as minus the heading)
static void set_race(float speed, float heading = 0.0f) {
    wr32(SEQUENCE, 1), wr32(PHASE, 1), wr32(GAME + 0x6C, 1), wr32(GAME + 0x7C, 0), wr32(0x2242B0, CAR);
    put_f(SPEED, speed);
    put_f(CAR + 0x0C, std::sin(heading) * speed / 60.0f);
    put_f(CAR + 0x14, std::cos(heading) * speed / 60.0f);
    wr16(CAR + 0x1A, uint16_t(int16_t(-heading * 65536.0f / (2.0f * 3.14159265f))));
}

// ---- the datagrams ----------------------------------------------------------------------------------

struct Datagram {
    uint8_t type;
    std::vector<uint8_t> data;
};
static std::vector<Datagram> g_sent;
static void sink(const uint8_t* d, size_t n) { g_sent.push_back({n > 4 ? d[4] : uint8_t(0xFF), std::vector<uint8_t>(d, d + n)}); }

static void begin() {
    telemetry_ogs_test_begin(sink);
    g_sent.clear();
    g_inputs.accel = 255, g_inputs.brake = 0, g_inputs.steer = 128;
    wr32(SEQUENCE, 0), wr32(PHASE, 0), wr32(0x2242B0, 0);
    wr32(VISIBLE_COUNT, 0);
    put_f(CAR + 0x00, 0.0f), put_f(CAR + 0x04, 0.0f), put_f(CAR + 0x08, 0.0f);
}
static void frames(int n) {
    for (int i = 0; i < n; i++) telemetry_ogs_frame(true);
}
static int count(uint8_t type) {
    int n = 0;
    for (const Datagram& d : g_sent) n += d.type == type;
    return n;
}
// Steady frames, then `n` frames of the car losing 1 m/s each (60 m/s2 over the 6 frames the sender looks at)
static void drive_and_slow(int n) {
    set_race(60.0f);
    frames(12);
    for (int i = 1; i <= n; i++) {
        set_race(60.0f - float(i));
        frames(1);
    }
}

// ---- a motion frame's orientation: the one value that needs a reader --------------------------------

static uint64_t word(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= uint64_t(p[i]) << (8 * i);
    return v;
}
static int32_t offset(uint64_t w) { return int32_t(uint32_t(w) & ~3u) / 4; }   // a pointer's signed word offset

// The orientation of the first object of a motion frame: root -> objects list -> pointer 2 (x, y, z, w)
static bool read_orientation(const Datagram& d, float q[4]) {
    const uint8_t* m = d.data.data() + 8;
    const uint8_t* end = d.data.data() + d.data.size();
    if (d.type != 1 || end - m < 16) return false;
    const uint8_t* seg = m + 8;
    uint64_t w = word(seg);                                   // the root pointer: the MotionFrame
    const uint8_t* frame = seg + 8 + 8 * offset(w);
    const uint8_t* objects = frame + 8 * uint16_t(w >> 32);   // its first pointer, after the data words
    uint64_t lw = word(objects);                              // a composite list of MotionObject
    if ((lw & 3) != 1 || ((lw >> 32) & 7) != 7) return false;
    const uint8_t* tag = objects + 8 + 8 * offset(lw);
    uint64_t t = word(tag);
    const uint8_t* first = tag + 8;                           // the first MotionObject
    const uint8_t* ptrs = first + 8 * uint16_t(t >> 32);      // after its data words
    uint64_t qw = word(ptrs + 8 * 2);                         // pointer 2: the orientation
    if ((qw & 3) != 0) return false;
    const uint8_t* quat = ptrs + 8 * 2 + 8 + 8 * offset(qw);
    size_t quat_bytes = 8 * size_t(uint16_t(qw >> 32));
    for (int i = 0; i < 4; i++) {                             // (trailing zero words are not sent)
        uint32_t u = 0;
        if (size_t(4 * i + 4) <= quat_bytes) memcpy(&u, quat + 4 * i, 4);
        memcpy(&q[i], &u, 4);
    }
    return quat + quat_bytes <= end;
}

static bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

int main() {
    {   // the envelope and the size budget, a discovery before the motion
        begin();
        set_race(40.0f);
        frames(3);
        CHECK(!g_sent.empty() && g_sent[0].type == 0);
        CHECK(count(1) == 3);
        for (const Datagram& d : g_sent) {
            CHECK(d.type == 0 || d.type == 1 || d.type == 2);
            CHECK(d.data.size() >= 16 && d.data.size() <= 1400);
            CHECK(memcmp(d.data.data(), "OMT1", 4) == 0 && d.data[5] == 0 && d.data[6] == 0 && d.data[7] == 0);
        }
    }
    {   // the orientation: a unit quaternion, heading 0 is +z, a quarter turn right is +x (C-2: world = q * body)
        for (float turn : {0.0f, 1.5707963f, -1.5707963f, 3.0f}) {
            begin();
            set_race(30.0f, turn);
            frames(2);
            float q[4] = {};
            CHECK(read_orientation(g_sent.back(), q));
            CHECK(near(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3], 1.0f, 1e-3f));
            ogs::telemetry::Vec3 forward = ogs::telemetry::Rotate({q[0], q[1], q[2], q[3]}, {0, 0, 1});
            CHECK(near(forward.X, std::sin(turn), 2e-3f) && near(forward.Y, 0.0f, 2e-3f) && near(forward.Z, std::cos(turn), 2e-3f));
        }
    }
    {   // a crash is one event (re-sent for a moment); steady driving, braking and the finish send none
        begin();
        drive_and_slow(30);
        CHECK(count(2) >= 1);
        for (const Datagram& d : g_sent) CHECK(d.data.size() <= 1400);

        begin();
        set_race(60.0f);
        frames(40);
        CHECK(count(2) == 0);                          // steady driving

        begin();
        g_inputs.brake = 255;
        drive_and_slow(30);
        CHECK(count(2) == 0);                          // the same loss braking is braking

        begin();
        set_race(60.0f);
        frames(20);
        for (int i = 1; i <= 4; i++) {                 // a tap on another car: 1 m/s lost in 4 frames
            set_race(60.0f - 0.25f * float(i));
            frames(1);
        }
        set_race(59.0f);
        frames(20);
        CHECK(count(2) >= 1);                          // a light tap is a hit too

        begin();
        set_race(60.0f);
        frames(20);
        for (int i = 1; i <= 60; i++) {                // lifting off: 3 m/s2 for a second
            set_race(60.0f - 0.05f * float(i));
            frames(1);
        }
        CHECK(count(2) == 0);                          // slowing gently is not a hit

        begin();                                       // the game stopping the car over the line is not a crash
        set_race(60.0f);
        frames(12);
        wr32(GAME + 0x7C, 1);                          // the goal flag: the finish line is crossed
        g_inputs.accel = 0;
        for (int i = 1; i <= 40; i++) {
            put_f(SPEED, 60.0f - 1.5f * float(i));     // 90 m/s2, no pedals
            put_f(CAR + 0x14, (60.0f - 1.5f * float(i)) / 60.0f);
            frames(1);
        }
        CHECK(count(2) == 0);

        begin();
        set_race(60.0f);
        frames(12);
        set_race(0.0f);
        put_f(CAR + 0x0C, 0.0f), put_f(CAR + 0x14, 0.0f);
        frames(10);
        CHECK(count(2) == 0);                          // the finish zeroing the speed in one frame
    }
    {   // another car: one that comes inside the player's box is a hit, however gently
        begin();
        set_race(60.0f);
        for (int i = 0; i < 60; i++) {                 // a car passing 5 m to the side
            put_rival(5.0f, -20.0f + float(i) * 0.7f);
            frames(1);
        }
        CHECK(count(2) == 0);

        begin();
        put_rival(1.5f, 0.0f);                         // alongside before the race starts: not a hit
        set_race(60.0f);
        frames(30);
        CHECK(count(2) == 0);

        begin();
        set_race(60.0f);
        put_rival(6.0f, 0.0f);
        frames(20);
        for (int i = 1; i <= 5; i++) {                 // it moves in against the side, at the same speed
            put_rival(6.0f - 0.9f * float(i), 0.0f);
            frames(1);
        }
        frames(10);
        CHECK(count(2) >= 1);

        begin();
        set_race(60.0f);
        put_rival(0.0f, 30.0f, 0.0f);                  // a car 30 m ahead, standing: not in the box
        frames(20);
        CHECK(count(2) == 0);
    }
    telemetry_ogs_test_begin(nullptr);
    if (g_failed) {
        printf("%d checks failed\n", g_failed);
        return 1;
    }
    printf("telemetry: all correct\n");
    return 0;
}

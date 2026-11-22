// UDP telemetry (telemetry.h). Everything is read from the game's RAM, as
// stats.cpp does (docs/game_ram.md has the addresses):
//
//   0x22F364  float  engine RPM (about 500 at idle, ~7000 at the change up)
//   0x22F368  float  the RPM the dial's needle shows (smoothed)
//   0x22F3A8  float  speed, m/s (x 3.6 = the HUD's km/h)
//   0x13AB80  int    gear shown on the HUD (1-4)
//   0x13AB74  int    position, 0 = 1st
//   [0x2242B0]       the player's car object:
//      +0x00 float x3  position in the world, metres (y up)
//      +0x0C float x3  velocity, metres per game frame (x 60 = m/s; y stays 0)
//      +0x18 s16 x3    pitch, yaw, roll (65536 = 360 degrees; yaw = minus the
//                      heading, the heading of +z being 0; in a slide the yaw
//                      and the velocity's heading part by the slip angle)
//      +0x64 float x4  wheel speed, metres per game frame
//
// Found on 77 RAM snapshots of races (F9) and the code that writes them
// (fn_00039AC8, the player car's engine); the signs of pitch and roll and the
// side of x are a reading, not measured on a moving rig.
#include "telemetry.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game_ram.h"
#include "inputs.h"
#include "ppc_rt.h"
#include "telemetry_ogs.h"

namespace {

// Game RAM (game_ram.h)
using namespace game_ram;
constexpr int CAR_GO = 1;                   // car status: racing
constexpr float GAME_HZ = 60.0f;            // the game's own unit of time (its m/frame, its ticks of 1/6000 s)
constexpr float G = 9.81f;
constexpr float PI = 3.14159265f;

float rdf(uint32_t a) {
    uint32_t u = rd32(a);
    float f;
    memcpy(&f, &u, 4);
    return std::isfinite(f) ? f : 0.0f;
}
float angle(uint32_t a) { return float(int16_t(rd16(a))) * (2.0f * PI / 65536.0f); }

// The Codemasters "extradata = 3" packet, by float index
enum : int {
    T_TIME = 0, T_LAP_TIME, T_LAP_DISTANCE, T_TOTAL_DISTANCE,
    T_X, T_Y, T_Z, T_SPEED, T_VX, T_VY, T_VZ,
    T_ROLL_X, T_ROLL_Y, T_ROLL_Z,      // the car's right, in the world
    T_PITCH_X, T_PITCH_Y, T_PITCH_Z,   // the car's forward, in the world
    T_SUSP_POS = 17, T_SUSP_VEL = 21, T_WHEEL_SPEED = 25,   // RL, RR, FL, FR
    T_THROTTLE = 29, T_STEER, T_BRAKE, T_CLUTCH, T_GEAR,
    T_G_LAT, T_G_LON, T_LAP, T_ENGINE_RATE,   // RPM / 10
    T_POSITION = 39, T_TOTAL_LAPS = 60, T_TRACK_SIZE, T_LAST_LAP_TIME, T_MAX_RPM, T_IDLE_RPM, T_MAX_GEARS,
    T_COUNT = 66
};

struct Sender {
#ifdef _WIN32
    SOCKET s = INVALID_SOCKET;
    bool wsa = false;
    bool ok() const { return s != INVALID_SOCKET; }
#else
    int s = -1;
    bool ok() const { return s >= 0; }
#endif
    sockaddr_in to{};
    int port = 0;

    bool open(int p) {
        if (ok() && port == p) return true;
        close();
#ifdef _WIN32
        if (!wsa) {
            WSADATA d;
            if (WSAStartup(MAKEWORD(2, 2), &d) != 0) return false;
            wsa = true;
        }
#endif
        s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (!ok()) return false;
#ifdef _WIN32
        u_long nonblocking = 1;
        ioctlsocket(s, FIONBIO, &nonblocking);
#endif
        to.sin_family = AF_INET;
        to.sin_port = htons(uint16_t(p));
        to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // this PC: SimHub and the rig's software run here
        port = p;
        printf("[telemetry] UDP to 127.0.0.1:%d (Codemasters extradata 3)\n", p);
        return true;
    }
    void close() {
#ifdef _WIN32
        if (ok()) closesocket(s);
        s = INVALID_SOCKET;
#else
        if (ok()) ::close(s);
        s = -1;
#endif
        port = 0;
    }
    void send(const float* f, int n) {
        if (ok()) sendto(s, reinterpret_cast<const char*>(f), n * 4, 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    }
};

Sender g_sender;
// Last frame's world velocity (m/s), for the G-forces
float g_prev_v[3] = {0, 0, 0};
float g_prev_y = 0;
bool g_have_prev = false;
float g_last_lap_time = 0;
int g_last_laps = 0;

}  // namespace

static void telemetry_dirt_frame(bool on, int port) {
    if (!on) {
        if (g_sender.ok()) g_sender.close();
        return;
    }
    if (!g_sender.open(port > 0 && port < 65536 ? port : 20777)) return;

    float p[T_COUNT] = {};
    p[T_MAX_RPM] = 800.0f;   // the dial's 8000 (RPM / 10, as the format has it)
    p[T_IDLE_RPM] = 50.0f;
    p[T_MAX_GEARS] = 4.0f;
    const uint32_t car = rd32(PLAYER_CAR);
    const bool racing = rd32(SEQUENCE) == 1 && rd32(PHASE) == 1 && car != 0 && car < 0x800000 &&
                        int32_t(rd32(G_CAR_STATUS)) == CAR_GO;
    if (!racing) {   // standing still: no G, level, no engine
        g_have_prev = false;
        p[T_PITCH_Z] = 1.0f;
        p[T_ROLL_X] = 1.0f;
        p[T_GEAR] = 0.0f;
        g_sender.send(p, T_COUNT);
        return;
    }

    // Orientation: forward and right vectors in the world
    const float pitch = angle(car + 0x18), heading = -angle(car + 0x1A), roll = angle(car + 0x1C);
    const float cp = std::cos(pitch), sp = std::sin(pitch), ch = std::cos(heading), sh = std::sin(heading);
    const float cr = std::cos(roll), sr = std::sin(roll);
    const float fwd[3] = {sh * cp, sp, ch * cp};
    const float flat_right[3] = {ch, 0.0f, -sh};
    const float right[3] = {flat_right[0] * cr, -sr, flat_right[2] * cr};

    // Velocity in m/s (y from the position: the game keeps it at 0)
    const float y = rdf(car + 0x04);
    float v[3] = {rdf(car + 0x0C) * GAME_HZ, g_have_prev ? (y - g_prev_y) * GAME_HZ : 0.0f, rdf(car + 0x14) * GAME_HZ};
    float g_lat = 0, g_lon = 0;
    if (g_have_prev) {
        float a[3];
        for (int i = 0; i < 3; i++) a[i] = (v[i] - g_prev_v[i]) * GAME_HZ;
        g_lat = (a[0] * flat_right[0] + a[2] * flat_right[2]) / G;
        g_lon = (a[0] * fwd[0] + a[1] * fwd[1] + a[2] * fwd[2]) / G;
        // a landing or a wall in one frame can give hundreds of G
        g_lat = std::fmax(-8.0f, std::fmin(8.0f, g_lat));
        g_lon = std::fmax(-8.0f, std::fmin(8.0f, g_lon));
    }
    memcpy(g_prev_v, v, sizeof v);
    g_prev_y = y;
    g_have_prev = true;

    const int laps = int32_t(rd32(G_LAPS));
    const float lap_time = float(int32_t(rd32(LAP_FRAMES))) / GAME_HZ;
    if (laps != g_last_laps) {
        if (laps > g_last_laps) g_last_lap_time = lap_time;
        g_last_laps = laps;
    }

    p[T_TIME] = float(int32_t(rd32(G_RACE_TIME))) / (GAME_HZ * 100.0f);
    p[T_LAP_TIME] = lap_time;
    p[T_LAP_DISTANCE] = float(int32_t(rd32(G_PROGRESS)) & 255);   // track points, 256 a lap
    p[T_TOTAL_DISTANCE] = float(int32_t(rd32(G_PROGRESS)));
    p[T_X] = rdf(car + 0x00);
    p[T_Y] = y;
    p[T_Z] = rdf(car + 0x08);
    p[T_SPEED] = rdf(SPEED);
    p[T_VX] = v[0], p[T_VY] = v[1], p[T_VZ] = v[2];
    p[T_ROLL_X] = right[0], p[T_ROLL_Y] = right[1], p[T_ROLL_Z] = right[2];
    p[T_PITCH_X] = fwd[0], p[T_PITCH_Y] = fwd[1], p[T_PITCH_Z] = fwd[2];
    for (int w = 0; w < 4; w++) p[T_WHEEL_SPEED + w] = rdf(car + 0x64 + 4 * w) * GAME_HZ;
    p[T_THROTTLE] = float(g_inputs.accel) / 255.0f;   // the pedals and the wheel as the cabinet reads them
    p[T_BRAKE] = float(g_inputs.brake) / 255.0f;
    p[T_STEER] = std::fmax(-1.0f, std::fmin(1.0f, (float(g_inputs.steer) - 128.0f) / 127.0f));
    p[T_GEAR] = float(int32_t(rd32(HUD_GEAR)));
    p[T_G_LAT] = g_lat;
    p[T_G_LON] = g_lon;
    p[T_LAP] = float(laps);
    p[T_ENGINE_RATE] = rdf(RPM) / 10.0f;
    p[T_POSITION] = float(int32_t(rd32(HUD_POSITION)) + 1);
    p[T_TOTAL_LAPS] = float(int32_t(rd32(G_LAPS_TOTAL)));
    p[T_TRACK_SIZE] = 256.0f;
    p[T_LAST_LAP_TIME] = g_last_lap_time;
    g_sender.send(p, T_COUNT);
}

void telemetry_frame(bool on, int format, int dirt_port) {
    telemetry_dirt_frame(on && format != TELEMETRY_OGS, dirt_port);   // (the other one is closed)
    telemetry_ogs_frame(on && format != TELEMETRY_DIRT);
}

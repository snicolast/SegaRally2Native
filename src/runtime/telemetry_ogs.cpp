// OGS-TS telemetry (telemetry_ogs.h). Game RAM, as stats.cpp reads it
// (docs/game_ram.md has the addresses):
//
//   0x22F364  float  engine RPM (about 500 at idle, ~7000 at the change up)
//   0x22F3A8  float  speed, m/s
//   0x13AB80  int    gear shown on the HUD (1-4)
//   0x13AB74  int    position, 0 = 1st
//   0x224324  int    car model (SELECT CAR)
//   [0x2242B0]       the player's car object:
//      +0x00 float x3  position in the world, metres (y up)
//      +0x0C float x3  velocity, metres per game frame (x 60 = m/s; y stays 0)
//      +0x18 s16 x3    pitch, yaw, roll (65536 = 360 degrees; yaw = minus the
//                      heading, the heading of +z being 0)
//
// The game's world is already y up, z forward; the side of x, and the signs of
// pitch, roll and steering, are a reading of the RAM, not measured on a moving
// rig (as in the DiRT output, telemetry.cpp).
#include "telemetry_ogs.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "../../third_party/ogs_telemetry/OgsTelemetryFrame.h"
#include "../../third_party/ogs_telemetry/OgsTelemetryWire.h"
#include "game_ram.h"
#include "inputs.h"
#include "ppc_rt.h"

namespace {
using namespace ogs::telemetry;

// Game RAM (game_ram.h)
using game_ram::SEQUENCE, game_ram::PHASE, game_ram::RACE_RESULT, game_ram::G_ROUND, game_ram::G_GOAL,
    game_ram::G_LAPS_TOTAL, game_ram::G_CAR_STATUS, game_ram::G_LAPS, game_ram::HUD_POSITION, game_ram::HUD_GEAR,
    game_ram::SELECT_CAR, game_ram::RPM, game_ram::SPEED, game_ram::PLAYER_CAR, game_ram::IN_AIR, game_ram::VISIBLE_COUNT,
    game_ram::VISIBLE_CARS;
constexpr int CAR_GO = 1, PHASE_RACE = 1, PHASE_REPLAY = 3;
constexpr int RESULT_FINISHED = 2, RESULT_TIME_UP = 3;
constexpr float GAME_HZ = 60.0f;      // the game's steps a second of its own time: its metres per frame x 60 are the speed in m/s
// (a recording: the car's change of position per frame x 57.524, the vblank, came 4% under the speed float, whose m/s
// the HUD's km/h confirms; x 60 matches). The vblank is only how often a frame is sent
constexpr float STREAM_HZ = 57.524f;   // the Model 3 vblank (runtime.cpp FRAME_HZ)
constexpr float PI = 3.14159265f;
constexpr float G = 9.80665f;

constexpr const char* GROUP = "239.255.17.17";
constexpr double DISCOVERY_EVERY = 2.0;   // s, for late joiners
constexpr double EVENT_CARRY = 0.1;       // s, a sent event rides along until it is this old (C-10)
// Hits (body.impact). The game's crashes are soft (pad_rumble.cpp: 15-28 m/s lost over a few tenths of
// a second, no jump in one frame) and its velocity jitters from frame to frame, so a hit is read two ways
// over the last 6 frames (0.1 s), checked on recordings:
//  - the velocity changing by more than driving does. The margin is wide: the jitter alone faked 14 m/s
//    at 62 m/s in a steering flick, bends reach 5, and walls were seen at 28-33
constexpr float IMPACT_MIN = 14.0f;       // m/s of velocity change to count as a hit
constexpr float IMPACT_FULL = 28.0f;      // ... and the one that is intensity 1
//  - the speed dropping with the brake off. A slow crash (63 to 26 m/s in 1.3 s, throttle down,
//    25-31 m/s2) turns the velocity no more than a bend does, but only a crash loses speed like that.
//    A tap on another car is a notch of 1-3 m/s in about 0.2 s (4-5 m/s2 over 6 frames); driving without
//    the brake loses at most 2-3 m/s2 (a 99th percentile on recordings), so a loss from SLOW_MIN up
//    counts when it starts suddenly (more than twice what the 6 frames before lost), and from SLOW_HARD
//    up always
constexpr float SLOW_MIN = 4.0f;          // m/s2 of speed lost to count as a hit, if it starts suddenly
constexpr float SLOW_HARD = 15.0f;        // ... and from here whatever came before
constexpr float SLOW_FULL = 35.0f;        // ... and the one that is intensity 1
constexpr float SLOW_ONSET = 2.0f;        // times the loss of the 6 frames before that a sudden one is
constexpr float SLOW_BRAKE = 0.3f;        // brake pedal (0..1) from which the same loss may be braking
// Another car touching. The game keeps the cars in view in a list (fn_00015FA4 sorts it by size on screen):
// the count at 0x225A2C and the car objects from 0x225A30, each with its position at +0 / +4 / +8 as the
// player's car (fn_0004B1BC reads them so) and its velocity at +0x0C / +0x14. A tap on a car often slows the
// player no more than the road does, so a car inside the player's own box (its length and width, in the
// player's axes) is a hit. The box is a guess from car sizes: SR2_TELEMETRY_LOG prints the cars near
constexpr float CONTACT_X = 2.0f, CONTACT_Y = 2.5f, CONTACT_Z = 4.5f;   // m: half-extents of the box, across, up, along
constexpr float CONTACT_FULL = 20.0f;     // m/s of closing speed that is intensity 1
constexpr float SNAP = 15.0f;             // m/s changed in ONE frame is a reset (the finish zeroes the speed), not a crash
constexpr int HOLD = 3;                   // frames a hit is measured before it is sent, for its peak
constexpr double IMPACT_WINDOW = 1.5;     // s: one crash is one event, unless it gets much harder

const char* const CARS[8] = {"Peugeot 306", "Toyota Corolla", "Ford Escort", "Subaru Impreza",
                             "Mitsubishi Lancer", "Lancia Stratos", "Toyota Celica", "Lancia Delta"};
// The course is the round (0x22CF48) % 6, by the game's ids; medium is a ContactMedium
// (2 asphalt, 3 dirt, 5 gravel, 8 snow), a reading of the courses
struct Course {
    int id;
    const char* name;
    uint16_t medium;
};
constexpr Course COURSES[4] = {{3, "Desert", 3}, {2, "Mountain", 5}, {5, "Snowy", 8}, {4, "Riviera", 2}};

// Channel ids, in the order they are declared
enum : uint16_t { C_PAUSED, C_REPLAY, C_RPM, C_THROTTLE, C_GEAR, C_SPEED, C_BRAKE, C_STEER, C_LAP, C_LAPS_TOTAL, C_POSITION };

float rdf(uint32_t a) {
    uint32_t u = rd32(a);
    float f;
    memcpy(&f, &u, 4);
    return std::isfinite(f) ? f : 0.0f;
}
float angle(uint32_t a) { return float(int16_t(rd16(a))) * (2.0f * PI / 65536.0f); }
float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// Yaw, pitch and roll as the unit quaternion of C-2 (the car's attitude: world = q * body).
// In the stored-Hamilton sense of the frame headers a rotation about +y is nose-right,
// about +x nose-down and about +z left-side-down, so the three are yaw, -pitch, -roll
Quat attitude(float yaw, float pitch, float roll) {
    const Quat qy{0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
    const Quat qp{std::sin(-pitch / 2), 0, 0, std::cos(pitch / 2)};
    const Quat qr{0, 0, std::sin(-roll / 2), std::cos(roll / 2)};
    Quat q = Multiply(Multiply(qy, qp), qr);
    const float n = std::sqrt(q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W);
    return {q.X / n, q.Y / n, q.Z / n, q.W / n};
}

void (*g_sink)(const uint8_t*, size_t) = nullptr;   // tests: where the datagrams go instead of the network

struct Publisher {
    SOCKET sock = INVALID_SOCKET;
    bool wsa = false;
    sockaddr_in to{};
    int port = 0;
    std::chrono::steady_clock::time_point t0;
    int64_t session_us = 0;
    double last_discovery = -1e9;
    uint32_t generation = 0;
    std::string label, location;   // car and course: the object's identity, a change is a new generation
    uint16_t medium = 2;
    bool announced = false;        // a discovery went out with this generation
    WireBuilder wire;

    // The car, tick to tick
    BodyDynamicsDeriver deriver;
    Vec3 pos{}, prev_pos{};
    Quat quat{0, 0, 0, 1};
    bool have_prev = false;
    uint32_t last_car = 0;
    // Impacts: ids run through the session; the ones sent in the last EVENT_CARRY ride along
    uint32_t next_event = 1;
    double last_impact = -1e9;
    float impact_level = 0;                // intensity of the last hit
    float pend_level = 0;                  // a hit being measured: its strongest so far and which way (sent after HOLD frames)
    Vec3 pend_away{0, 0, 0};
    int pend_age = 0;
    float hvx[8] = {}, hvz[8] = {}, hsp[16] = {};   // the last 8 frames of horizontal velocity, 16 of speed
    int frames = 0, air = 0;               // frames of this drive; frames in the air
    uint32_t touching[16] = {};            // the cars inside the box last frame, and whether that was read yet
    int touching_n = 0;
    bool contact_ready = false;
    bool log = getenv("SR2_TELEMETRY_LOG") != nullptr;
    std::vector<std::pair<double, EventInput>> recent;

    bool ok() const { return sock != INVALID_SOCKET; }

    bool open(int p) {
        if (ok() && port == p) return true;
        close();
        if (!wsa) {
            WSADATA d;
            if (WSAStartup(MAKEWORD(2, 2), &d) != 0) return false;
            wsa = true;
        }
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (!ok()) return false;
        u_long nonblocking = 1;
        ioctlsocket(sock, FIONBIO, &nonblocking);
        DWORD ttl = 1;   // the local subnet
        setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof ttl);
        int loop = 1;    // receivers on this PC too
        setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&loop), sizeof loop);
        to = {};
        to.sin_family = AF_INET;
        to.sin_port = htons(uint16_t(p));
        inet_pton(AF_INET, GROUP, &to.sin_addr);
        route_by_default();
        port = p;
        session_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        t0 = std::chrono::steady_clock::now();
        last_discovery = -1e9;
        announced = false;
        have_prev = false;
        recent.clear();
        printf("[telemetry] OGS-TS to %s:%d\n", GROUP, p);
        return true;
    }
    // Name the default route's interface for the group instead of leaving it to the OS, which
    // moves a group to loopback once anything here has sent to it there (implementation-udp.md,
    // network interfaces). Connecting a UDP socket only looks up the route; 192.0.2.1 is reserved
    // for documentation (RFC 5737)
    void route_by_default() {
        SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (probe == INVALID_SOCKET) return;
        sockaddr_in remote{}, local{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(9);
        inet_pton(AF_INET, "192.0.2.1", &remote.sin_addr);
        int len = sizeof local;
        if (connect(probe, reinterpret_cast<const sockaddr*>(&remote), sizeof remote) == 0 &&
            getsockname(probe, reinterpret_cast<sockaddr*>(&local), &len) == 0 && local.sin_addr.s_addr != htonl(INADDR_ANY) &&
            (ntohl(local.sin_addr.s_addr) >> 24) != 127)
            setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&local.sin_addr), sizeof local.sin_addr);
        closesocket(probe);
    }
    void close() {
        if (ok()) closesocket(sock);
        sock = INVALID_SOCKET;
        port = 0;
    }
    void send(const std::vector<uint8_t>& d) {
        if (g_sink) {
            g_sink(d.data(), d.size());
            return;
        }
        if (ok() && !d.empty()) sendto(sock, reinterpret_cast<const char*>(d.data()), int(d.size()), 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
    }

    double now() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }

    void set_identity(const std::string& l, const std::string& loc, uint16_t m) {
        if (l == label && loc == location && m == medium) return;
        label = l, location = loc, medium = m;
        if (announced) generation++;   // identity and environment are part of the descriptor set
        last_discovery = -1e9;
    }

    void send_discovery() {
        ObjectDecl obj;
        obj.Name = "player";
        obj.Type = 1;   // vehicle
        obj.Location = location;
        obj.TypeLabel = label;
        obj.Primary = true;
        using T = schema::ChannelType;
        auto channel = [&](const char* name, const char* unit, T type, const char* text) {
            ChannelDecl c;
            c.Name = name, c.Unit = unit, c.Type = type, c.Description = text;
            obj.Channels.push_back(c);
        };
        channel("session.paused", "", T::Boolean, "Not in a race (attract, menus, podium, replay): a rig settles (C-11)");
        channel("session.replay", "", T::Boolean, "A replay is playing");
        channel("engine.rpm", "rpm", T::Number, "Engine revolutions per minute");
        channel("engine.throttle", "", T::Number, "Accelerator pedal, 0..1");
        channel("transmission.gear", "", T::Number, "Gear on the HUD, 1..4");
        channel("vehicle.speed", "m/s", T::Number, "Speed");
        channel("brakes.pedal", "", T::Number, "Brake pedal, 0..1");
        channel("ctrl.steering.input", "", T::Number, "Steering wheel, -1..1, + = right");
        channel("race.lap", "", T::Number, "Laps completed");
        channel("race.lapsTotal", "", T::Number, "Laps in the race");
        channel("race.position", "", T::Number, "Place in the race, 1 = first");
        for (uint16_t id : {C_THROTTLE, C_BRAKE}) {
            obj.Channels[id].HasRange = true;
            obj.Channels[id].Min = 0, obj.Channels[id].Max = 1;
        }
        Environment env{};
        env.Gravity = G;
        env.Medium = medium;
        Instance instance;
        instance.MotionRateHz = STREAM_HZ;
        char host[256] = {};
        if (gethostname(host, sizeof host - 1) == 0) instance.Host = host;
        for (const auto& page : PaginateDiscovery(1, session_us, "Sega Rally 2", env, {obj}, generation, kDatagramBudget, instance)) send(page);
        announced = true;
    }

    // Looks for a hit this frame (the car's velocity v, in m/s) and, if there is one, queues a
    // body.impact. Called once per frame while racing, after the car's own state is read
    void detect_hit(double t, uint32_t car, const Vec3& v, bool same_car, bool finished) {
        // Over the finish line the game brings the car to a stop itself (a recording: 58 m/s to 2 in 2 s, no
        // pedals): that is not a crash
        if (finished) {
            frames = 0, air = 0, pend_level = 0;
            contact_ready = false, touching_n = 0;
            return;
        }
        if (!same_car) frames = 0, air = 0, contact_ready = false, touching_n = 0;
        // A hit this frame: how hard (0 none) and from where, in the car's own axes
        float level = 0;
        Vec3 away{0, 0, 0};   // toward what hit the car
        const float speed = rdf(SPEED);
        if (frames >= 1) {
            const int last = (frames - 1) & 7;
            if (std::hypot(v.X - hvx[last], v.Z - hvz[last]) > SNAP) frames = 0, air = 0;
        }
        if (frames >= 8) {
            const int old = (frames + 2) & 7;   // 6 frames ago
            const Vec3 dv{v.X - hvx[old], 0.0f, v.Z - hvz[old]};
            const float mag = std::sqrt(dv.X * dv.X + dv.Z * dv.Z);
            if (log && mag > 8.0f) printf("[telemetry] dv6 %.1f m/s\n", mag);
            const float lost = hsp[(frames - 6) & 15] - speed;            // m/s lost over the last 6 frames
            const float lost_before = frames >= 13 ? hsp[(frames - 12) & 15] - hsp[(frames - 6) & 15] : 0.0f;
            const float slow = lost * GAME_HZ / 6.0f;                     // m/s2 of speed lost
            const bool sudden = lost > SLOW_ONSET * (lost_before > 0.0f ? lost_before : 0.0f) + 0.15f;
            if (log && slow > 3.5f) printf("[telemetry] speed loss %.1f m/s2\n", slow);
            const float brake = g_inputs.brake / 255.0f;
            const float l_vec = mag > IMPACT_MIN ? clampf((mag - IMPACT_MIN) / (IMPACT_FULL - IMPACT_MIN), 0.2f, 1.0f) : 0.0f;
            const bool slowing = brake < SLOW_BRAKE && (slow > SLOW_HARD || (slow > SLOW_MIN && sudden));
            const float l_slow = slowing ? clampf((slow - SLOW_MIN) / (SLOW_FULL - SLOW_MIN), 0.2f, 1.0f) : 0.0f;
            level = l_vec > l_slow ? l_vec : l_slow;
            if (level > 0) {
                const Vec3 b = BodyDynamicsDeriver::RotateByInverse(quat, dv);
                // (a slow crash turns the velocity little: its direction is the one the car loses speed along)
                away = mag > 1.0f ? Vec3{-b.X / mag, -b.Y / mag, -b.Z / mag} : Vec3{0, 0, 1};
            }
        }
        // Another car touching: one that has just come inside the box (the cars already inside when the
        // race starts are not hits)
        {
            uint32_t inside[16];
            int n_inside = 0;
            const uint32_t count = rd32(VISIBLE_COUNT) < 16 ? rd32(VISIBLE_COUNT) : 16;
            for (uint32_t i = 0; i < count; i++) {
                const uint32_t r = rd32(VISIBLE_CARS + 4 * i);
                if (r == 0 || r == car || (r & 3) != 0 || r >= RAM_SIZE - 0x40) continue;
                const Vec3 rel{rdf(r) - pos.X, rdf(r + 4) - pos.Y, rdf(r + 8) - pos.Z};
                if (std::fabs(rel.X) > 100.0f || std::fabs(rel.Y) > 100.0f || std::fabs(rel.Z) > 100.0f) continue;
                const Vec3 b = BodyDynamicsDeriver::RotateByInverse(quat, rel);   // across, up, along the player
                if (log && std::fabs(b.X) < 8.0f && std::fabs(b.Z) < 12.0f && (frames & 3) == 0)
                    printf("[telemetry] car %08X at x %.1f y %.1f z %.1f m (box %.1f x %.1f)\n", r, b.X, b.Y, b.Z, CONTACT_X, CONTACT_Z);
                if (std::fabs(b.X) >= CONTACT_X || std::fabs(b.Y) >= CONTACT_Y || std::fabs(b.Z) >= CONTACT_Z) continue;
                inside[n_inside++] = r;
                bool was = false;
                for (int k = 0; k < touching_n; k++) was = was || touching[k] == r;
                if (was || !contact_ready) continue;
                const float rvx = (rdf(r + 0x0C) - rdf(car + 0x0C)) * GAME_HZ, rvz = (rdf(r + 0x14) - rdf(car + 0x14)) * GAME_HZ;
                const float closing = std::sqrt(rvx * rvx + rvz * rvz);
                const float l = clampf(0.2f + 0.8f * closing / CONTACT_FULL, 0.2f, 1.0f);
                if (l > level) {
                    const float d = std::sqrt(b.X * b.X + b.Y * b.Y + b.Z * b.Z);
                    level = l, away = d > 0.01f ? Vec3{b.X / d, b.Y / d, b.Z / d} : Vec3{0, 0, 1};   // toward the car
                }
                if (log) printf("[telemetry] car %08X touching, closing %.1f m/s\n", r, closing);
            }
            for (int k = 0; k < n_inside; k++) touching[k] = inside[k];
            touching_n = n_inside;
            contact_ready = true;
        }
        // A landing: as long a flight as it was, the game's own thump (pad_rumble.cpp)
        if (rd32(IN_AIR) == 1) air++;
        else {
            if (air >= 6 && level < 0.3f) level = clampf(float(air) / 40.0f, 0.3f, 1.0f), away = {0, -1, 0};
            air = 0;
        }
        // A hit is measured for HOLD frames and sent once, at its peak: a crash builds up over a few
        // frames, and an event for each step would be several knocks. A harder one later in the
        // window (the crash getting worse) is another event
        if (pend_level == 0 && level > 0 && (t - last_impact > IMPACT_WINDOW || level > impact_level + 0.25f))
            pend_level = level, pend_away = away, pend_age = 0;
        else if (pend_level > 0) {
            if (level > pend_level) pend_level = level, pend_away = away;
            if (++pend_age >= HOLD) {
                EventInput e;
                e.Id = next_event++;
                e.Name = "body.impact";
                e.Intensity = pend_level;
                e.Direction = pend_away;
                e.HasDirection = true;
                recent.push_back({t, e});
                last_impact = t;
                impact_level = pend_level;
                if (log) printf("[telemetry] body.impact %u intensity %.2f\n", e.Id, e.Intensity);
                pend_level = 0;
            }
        }
        hvx[frames & 7] = v.X, hvz[frames & 7] = v.Z, hsp[frames & 15] = speed;
        frames++;
    }

    // The channel values, in the order they were declared
    void add_channels(MotionObjectInput& o, bool racing, bool replay) {
    auto number = [&](uint16_t id, double v) {
        ChannelValueInput c;
        c.Id = id, c.Number = v;
        o.Channels.push_back(c);
    };
    auto flag = [&](uint16_t id, bool v) {
        ChannelValueInput c;
        c.Id = id, c.IsBool = true, c.Boolean = v;
        o.Channels.push_back(c);
    };
    flag(C_PAUSED, !racing);
    flag(C_REPLAY, replay);
    number(C_RPM, racing ? rdf(RPM) : 0.0);
    number(C_THROTTLE, racing ? g_inputs.accel / 255.0 : 0.0);
    number(C_GEAR, racing ? int32_t(rd32(HUD_GEAR)) : 0);
    number(C_SPEED, racing ? rdf(SPEED) : 0.0);
    number(C_BRAKE, racing ? g_inputs.brake / 255.0 : 0.0);
    number(C_STEER, racing ? clampf((float(int(g_inputs.steer)) - 128.0f) / 127.0f, -1.0f, 1.0f) : 0.0);
    number(C_LAP, racing ? int32_t(rd32(G_LAPS)) : 0);
    number(C_LAPS_TOTAL, racing ? int32_t(rd32(G_LAPS_TOTAL)) : 0);
    number(C_POSITION, racing ? int32_t(rd32(HUD_POSITION)) + 1 : 0);
    }

    void send_events(double t) {
        while (!recent.empty() && t - recent.front().first > EVENT_CARRY) recent.erase(recent.begin());
        if (recent.empty()) return;
        std::vector<EventInput> events;
        for (const auto& e : recent) events.push_back(e.second);
        for (const auto& run : PlanEventFrames(events, t)) {
            BuildEventFrame(wire, t, std::vector<EventInput>(events.begin() + run.first, events.begin() + run.first + run.second));
            send(wire.Wrap(FrameType::Event));
        }
    }
};

Publisher g_pub;

}  // namespace

void telemetry_ogs_test_begin(void (*sink)(const uint8_t*, size_t)) {
    g_pub.close();
    g_pub = Publisher{};
    g_sink = sink;
}

void telemetry_ogs_frame(bool on) {
    Publisher& p = g_pub;
    if (!on) {
        if (p.ok()) p.close();
        return;
    }
    if (!p.open(40123)   /* fixed by the standard */) return;

    const double t = p.now();
    const int32_t phase = int32_t(rd32(PHASE));
    const bool in_game = rd32(SEQUENCE) == 1;
    const uint32_t car = rd32(PLAYER_CAR);
    // The race is over from the finish line (goal flag, which stays up until the next stage starts) or time up:
    // the result card shows a car of its own, which the player-car pointer / status can then describe, and a
    // rig following it swung round 180 degrees. Not racing = paused, the rig keeps the last pose and settles
    const int32_t race_result = int32_t(rd32(RACE_RESULT));
    const bool over = rd32(G_GOAL) == 1 || race_result == RESULT_FINISHED || race_result == RESULT_TIME_UP;
    const bool racing = in_game && phase == PHASE_RACE && !over && car != 0 && car < 0x800000 && int32_t(rd32(G_CAR_STATUS)) == CAR_GO;
    const bool replay = in_game && phase == PHASE_REPLAY;

    if (in_game) {
        const int model = int32_t(rd32(SELECT_CAR));
        const int round = int32_t(rd32(G_ROUND)) % 6;
        std::string location = p.location;
        uint16_t medium = p.medium;
        for (const Course& c : COURSES)
            if (c.id == round) location = c.name, medium = c.medium;
        p.set_identity(model >= 0 && model < 8 ? CARS[model] : "", location, medium);
    }
    if (t - p.last_discovery >= DISCOVERY_EVERY) {
        p.send_discovery();
        p.last_discovery = t;
    }

    MotionObjectInput o;
    o.Name = "player";
    if (racing) {
        p.quat = attitude(-angle(car + 0x1A), angle(car + 0x18), angle(car + 0x1C));
        p.pos = {rdf(car + 0x00), rdf(car + 0x04), rdf(car + 0x08)};
        // Velocity in m/s (y from the position: the game keeps it at 0)
        const bool same_car = p.have_prev && car == p.last_car;
        Vec3 v{rdf(car + 0x0C) * GAME_HZ, same_car ? (p.pos.Y - p.prev_pos.Y) * GAME_HZ : 0.0f, rdf(car + 0x14) * GAME_HZ};
        p.deriver.Sample(1.0 / GAME_HZ, p.pos, p.quat, true, v);
        p.detect_hit(t, car, v, same_car, rd32(G_GOAL) == 1);
        p.prev_pos = p.pos;
        p.last_car = car;
        p.have_prev = true;
        o.HasBodyDynamics = true;
        o.SpecificForce = p.deriver.SpecificForce;
        o.AngularVelocity = p.deriver.AngularVelocity;
    } else {
        // Not driving: the last pose, nothing measured (C-11), and no stale knocks
        p.deriver = BodyDynamicsDeriver{};
        p.have_prev = false;
        p.frames = p.air = 0;
        p.pend_level = 0;
        p.recent.clear();
    }
    o.Position = p.pos;
    o.Orientation = p.quat;

    p.add_channels(o, racing, replay);

    p.send_events(t);   // before the frame, so a receiver has the knock as the car shows it
    BuildMotion(p.wire, t, {o}, p.generation);
    p.send(p.wire.Wrap(FrameType::Motion));
}

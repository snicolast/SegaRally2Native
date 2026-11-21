// OGS-TS telemetry (Open Game Standards Telemetry Standard, issue 140): the
// player's car, as motion rigs, haptics and dashboards read it, sent by UDP
// multicast (239.255.17.17, TTL 1, so it also leaves the PC for a rig on the
// LAN). Read from the game's RAM, no hooks, and serialised by the standard's
// own dependency-free C++ writer (third_party/ogs_telemetry).
//
// An optional second output instead of the DiRT Rally packet (telemetry.h); save/settings.ini:
//   telemetry = 1
//   telemetry_format = ogs-ts     (dirt or ogs-ts)
//
// Streamed while a race is being driven: pose, body-frame specific force and
// angular rates (derived from the car's motion), RPM, speed, gear, pedals,
// steering, lap and position, and a body.impact event on a hard knock (walls,
// landings). Out of a race (attract, menus, podium, replays) the stream goes
// on with session.paused set (and session.replay in the replay), so a rig
// settles instead of following the demo (CONVENTIONS C-11).
#pragma once

#include <cstddef>
#include <cstdint>

// Game thread, every vblank (telemetry_frame in telemetry.cpp)
void telemetry_ogs_frame(bool on);

// Tests (tests/telemetry_test.cpp): forget everything and hand each datagram to `sink` instead of the network
void telemetry_ogs_test_begin(void (*sink)(const uint8_t* data, size_t size));

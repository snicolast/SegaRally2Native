// Telemetry for SimHub, motion rigs and dashboards (GitHub issue 111): every
// game frame the player's car (speed, RPM, gear, orientation, G-forces...) is
// sent by UDP to this PC in the layout of the Codemasters "extradata = 3"
// packet (DiRT Rally, 264 bytes, 66 little-endian floats), which those
// programs already read. Off by default; save/settings.ini:
//   telemetry = 1
//   telemetry_port = 20777
// and, optionally, the OGS-TS stream (telemetry_ogs.h) instead of it:
//   telemetry_format = dirt        (dirt or ogs-ts)
// Only while a race is being driven: the attract, the menus and the replays
// send a car standing still, so a motion rig does not follow the demo.
#pragma once

enum TelemetryFormat { TELEMETRY_DIRT = 0, TELEMETRY_OGS = 1 };

// Game thread, every vblank (runtime.cpp, after stats_frame). format: a TelemetryFormat;
// dirt_port is the UDP port of the DiRT packet (OGS-TS has a fixed one, set by the standard)
void telemetry_frame(bool on, int format, int dirt_port);

// The debug snapshot file (F9 in the game: out/snap/snap_<frame>.bin), read
// and written in one place for the game (save_snapshot / load_snapshot in
// src/runtime/app_files.cpp, --replay) and the capture viewer
// (src/viewer/capture.cpp). It holds the Real3D memory of the frame on
// screen, the whole texture RAM and the 2D layer memory.
//
//   u32  magic "LWS1"
//   u64  frame number
//   u32  number of pages
//   per page: u32 key (PowerPC address >> 16), 65536 bytes
//   2048 x 2048 u16 texels (texture RAM)
//   0x190000 bytes (tile generator memory)
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "r3d_state.h"

constexpr size_t SNAPSHOT_TEXELS = 2048 * 2048;

// false if the file cannot be written
bool write_snapshot(const std::string& path, const R3DFrameState& state, const std::vector<uint16_t>& texels,
                    const std::vector<uint8_t>& vram);

// false with the reason in `error` (the file cannot be opened, is not a
// snapshot or stops short); the outputs are left alone then
bool read_snapshot(const std::string& path, std::shared_ptr<R3DFrameState>& state, std::vector<uint16_t>& texels,
                   std::vector<uint8_t>& vram, std::string& error);

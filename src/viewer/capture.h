// A debug snapshot of the game read on its own, without the game (the file
// F9 writes: its format is in src/video/snapshot.h)
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "r3d_state.h"

struct Capture {
    std::shared_ptr<R3DFrameState> state;   // culling and polygon RAM of the frame
    std::vector<uint16_t> texels;           // texture RAM, 2048 x 2048
    std::vector<uint8_t> vram;              // 2D layer memory (TILEGEN_SIZE bytes)
    std::string path;
};

// false with the reason in `error` (the file cannot be opened, is not a
// snapshot or stops short)
bool load_capture(const std::string& path, Capture& out, std::string& error);
// The same format written back (the tests)
bool save_capture(const std::string& path, const Capture& c);

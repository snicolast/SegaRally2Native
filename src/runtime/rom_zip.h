// Reads Sega Rally 2's memory regions straight from the original
// srally2.zip (MAME/Supermodel set), as tools/romtool.py does for data/:
// each chip is found by its CRC, so the names inside the zip do not matter.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct RomSet {
    std::vector<uint8_t> crom, banked_crom, vrom, sound_program, sound_samples;
    // Digital Sound Board (68000 program + MPEG data) and the force feedback
    // board's Z80 program
    std::vector<uint8_t> dsb_program, dsb_mpeg, drive_program;
};

// One chip of a region: its data and where its first byte goes
struct RomPart {
    uint32_t offset;
    std::vector<uint8_t> data;
};

// Lays the chips of a region out the way the board sees them: `chunk` bytes
// of each chip in turn every `stride` bytes, 16-bit byte-swapped with `swap`
std::vector<uint8_t> interleave_region(const std::vector<RomPart>& parts, int stride, int chunk, bool swap);

// Fills `out` from the zip. On failure returns false with the reason in
// `error` (the zip cannot be opened, a chip is missing or its CRC is wrong)
bool load_rom_zip(const std::string& zip_path, RomSet& out, std::string& error);

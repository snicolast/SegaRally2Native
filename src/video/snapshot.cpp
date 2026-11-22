#include "snapshot.h"

#include <cstdio>

#include "tilegen.h"

namespace {
constexpr uint32_t SNAPSHOT_MAGIC = 0x3153574C;   // "LWS1"
// The three Real3D regions hold 144 pages; anything far above that is not a snapshot
constexpr uint32_t MAX_PAGES = 4096;
}  // namespace

bool write_snapshot(const std::string& path, const R3DFrameState& state, const std::vector<uint16_t>& texels,
                    const std::vector<uint8_t>& vram) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    uint32_t count = uint32_t(state.pages.size());
    bool ok = fwrite(&SNAPSHOT_MAGIC, 4, 1, f) == 1 && fwrite(&state.frame, 8, 1, f) == 1 && fwrite(&count, 4, 1, f) == 1;
    for (const auto& [key, page] : state.pages)
        ok = ok && fwrite(&key, 4, 1, f) == 1 && fwrite(page->data(), 1, page->size(), f) == page->size();
    ok = ok && fwrite(texels.data(), 2, texels.size(), f) == texels.size();
    ok = ok && fwrite(vram.data(), 1, vram.size(), f) == vram.size();
    return fclose(f) == 0 && ok;
}

bool read_snapshot(const std::string& path, std::shared_ptr<R3DFrameState>& state_out, std::vector<uint16_t>& texels_out,
                   std::vector<uint8_t>& vram_out, std::string& error) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        error = "cannot open " + path;
        return false;
    }
    uint32_t magic = 0, count = 0;
    auto state = std::make_shared<R3DFrameState>();
    bool ok = fread(&magic, 4, 1, f) == 1;
    if (!ok || magic != SNAPSHOT_MAGIC) {
        fclose(f);
        error = path + " is not a snapshot (F9 in the game writes them to out/snap)";
        return false;
    }
    ok = fread(&state->frame, 8, 1, f) == 1 && fread(&count, 4, 1, f) == 1 && count <= MAX_PAGES;
    for (uint32_t i = 0; ok && i < count; i++) {
        uint32_t key = 0;
        auto page = std::make_shared<R3DPage>();
        ok = fread(&key, 4, 1, f) == 1 && fread(page->data(), 1, page->size(), f) == page->size();
        if (ok) state->pages[key] = std::move(page);
    }
    std::vector<uint16_t> texels(SNAPSHOT_TEXELS);
    ok = ok && fread(texels.data(), 2, texels.size(), f) == texels.size();
    std::vector<uint8_t> vram(TILEGEN_SIZE);
    ok = ok && fread(vram.data(), 1, vram.size(), f) == vram.size();
    fclose(f);
    if (!ok) {
        error = path + " stops short: not a whole snapshot";
        return false;
    }
    state_out = std::move(state);
    texels_out = std::move(texels);
    vram_out = std::move(vram);
    return true;
}

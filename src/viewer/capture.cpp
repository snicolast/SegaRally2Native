#include "capture.h"

#include "snapshot.h"
#include "tilegen.h"

bool load_capture(const std::string& path, Capture& out, std::string& error) {
    if (!read_snapshot(path, out.state, out.texels, out.vram, error)) return false;
    out.path = path;
    return true;
}

bool save_capture(const std::string& path, const Capture& c) {
    if (!c.state || c.texels.size() != SNAPSHOT_TEXELS || c.vram.size() != TILEGEN_SIZE) return false;
    return write_snapshot(path, *c.state, c.texels, c.vram);
}

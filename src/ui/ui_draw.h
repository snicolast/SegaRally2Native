// What the menus (frontend.cpp) and the achievement pop-up (achievement_toast.cpp)
// both draw with: colours, the "over" of a pixel on their premultiplied BGRA
// canvas, a font file read whole and a line of text rasterised by GDI.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace ui_draw {

// One channel of 0xRRGGBB (shift 16, 8 or 0; 24 for the alpha of 0xAARRGGBB) as 0..1
inline float chan(uint32_t rgb, int shift) { return float((rgb >> shift) & 0xFF) / 255.0f; }

// a to b, t 0..1
inline uint32_t mix(uint32_t a, uint32_t b, float t) {
    uint32_t r = 0;
    for (int s = 0; s <= 16; s += 8) {
        float v = chan(a, s) + (chan(b, s) - chan(a, s)) * t;
        r |= uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f) << s;
    }
    return r;
}

// Integer "over" on premultiplied BGRA (it runs for every pixel of panels and text), a: 0..256
inline uint32_t over(uint32_t d, uint32_t rgb, int a) {
    int k = 256 - a;
    uint32_t rb = (((rgb & 0xFF00FFu) * a + (d & 0xFF00FFu) * k) >> 8) & 0xFF00FFu;
    uint32_t g = (((rgb & 0x00FF00u) * a + (d & 0x00FF00u) * k) >> 8) & 0x00FF00u;
    uint32_t da = ((255u * a + (d >> 24) * k) >> 8);
    return rb | g | (da << 24);
}

// The whole file (a font), empty if it cannot be read
std::vector<char> read_file(const std::string& path);

// `s` in `font`, white on black in a bitmap of `dc`, with `pad` pixels around
// its box: its coverage (0..255) in `a`, w x h. The sizes are set even when
// the bitmap cannot be made (then `a` stays empty)
struct TextRaster {
    int w = 0, h = 0, text_w = 0, text_h = 0;
    TEXTMETRICW tm{};
    std::vector<uint8_t> a;
};
TextRaster rasterise_text(HDC dc, HFONT font, const std::wstring& s, int pad);

}  // namespace ui_draw

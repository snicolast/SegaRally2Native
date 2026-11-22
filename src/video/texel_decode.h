// One Model 3 texel to 8-bit RGBA: the same decoding as decode() in r3d.frag.
// The texture packs (texture_pack.cpp) and the viewer (viewer/textures.cpp)
// both name a texture by a hash of this, so they share this one copy.
#pragma once
#include <cstdint>

inline void decode_texel(uint16_t t, uint32_t fmt, uint8_t* o) {
    auto lum = [&](uint32_t l, uint32_t max, bool opaque, uint32_t a) {
        o[0] = o[1] = o[2] = uint8_t((l * 255 + max / 2) / max);
        o[3] = opaque ? uint8_t(a) : uint8_t(0);
    };
    auto l4 = [&](uint32_t l, uint32_t a) { lum(l, 15, true, (a * 255 + 7) / 15); };
    switch (fmt) {
    case 0:
        o[0] = uint8_t((((t >> 10) & 31) * 255 + 15) / 31);
        o[1] = uint8_t((((t >> 5) & 31) * 255 + 15) / 31);
        o[2] = uint8_t(((t & 31) * 255 + 15) / 31);
        o[3] = (t & 0x8000) ? 0 : 255;
        break;
    case 1: l4(t & 15, (t >> 4) & 15); break;
    case 2: l4((t >> 4) & 15, t & 15); break;
    case 3: l4((t >> 8) & 15, (t >> 12) & 15); break;
    case 4: l4((t >> 12) & 15, (t >> 8) & 15); break;
    case 5: lum(t & 255, 255, (t & 255) != 255, 255); break;
    case 6: lum((t >> 8) & 255, 255, ((t >> 8) & 255) != 255, 255); break;
    case 7:
        o[0] = uint8_t(((t >> 12) & 15) * 17);
        o[1] = uint8_t(((t >> 8) & 15) * 17);
        o[2] = uint8_t(((t >> 4) & 15) * 17);
        o[3] = uint8_t((t & 15) * 17);
        break;
    default: {
        uint32_t n = (t >> (4 * (fmt - 8))) & 15;
        lum(n, 15, n != 15, 255);
    }
    }
}

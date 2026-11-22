#include "textures.h"

#include <algorithm>
#include <cstdio>

#include "../common/fnv1a.h"

TextureWord texture_word(uint32_t tex) {
    return {int(tex & 2047), int((tex >> 11) & 2047), 32 << ((tex >> 22) & 7), 32 << ((tex >> 25) & 7), tex >> 28};
}

uint64_t decode_texture(uint32_t tex, R3DTextureRam& ram, std::vector<uint8_t>& rgba) {
    TextureWord w = texture_word(tex);
    std::vector<uint16_t> texels;
    ram.read_texture(w.x, w.y, w.w, w.h, texels);
    rgba.resize(texels.size() * 4);
    for (size_t i = 0; i < texels.size(); i++) decode_texel(texels[i], w.format, &rgba[i * 4]);
    uint32_t dims[2] = {uint32_t(w.w), uint32_t(w.h)};
    return fnv1a_bytes(rgba.data(), rgba.size(), fnv1a_bytes(dims, sizeof(dims)));
}

std::string texture_file_name(uint64_t hash) {
    char name[32];
    snprintf(name, sizeof(name), "%016llx.png", (unsigned long long)hash);
    return name;
}

std::vector<TextureUse> model_textures(const R3DScene& scene, const R3DViewport& v, const R3DInstance& in,
                                       R3DTextureRam& ram) {
    std::vector<TextureUse> uses;
    auto count = [&](const std::vector<R3DVertex>& list, size_t first, size_t size, const uint32_t r[2]) {
        size_t from = std::min<size_t>(r[0], size), to = std::min<size_t>(r[1], size);
        for (size_t i = first + from; i + 2 < first + to && i + 2 < list.size(); i += 3) {
            const R3DVertex& x = list[i];
            if (!(x.flags & R3D_TEXTURED)) continue;
            auto it = std::find_if(uses.begin(), uses.end(), [&](const TextureUse& u) { return u.tex == x.tex; });
            if (it == uses.end()) {
                uses.emplace_back();
                it = uses.end() - 1;
                it->tex = x.tex;
            }
            it->triangles++;
        }
    };
    count(scene.vertices, v.opaque_first, v.opaque_count, in.opaque);
    count(scene.vertices, v.trans_first, v.trans_count, in.trans);
    count(scene.backs, v.back_first, v.back_count, in.back);
    for (TextureUse& u : uses) {
        u.word = texture_word(u.tex);
        u.hash = decode_texture(u.tex, ram, u.rgba);
    }
    std::stable_sort(uses.begin(), uses.end(), [](const TextureUse& a, const TextureUse& b) { return a.triangles > b.triangles; });
    return uses;
}

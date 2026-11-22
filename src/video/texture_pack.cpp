#include "texture_pack.h"

#include "../common/fnv1a.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// Same decoding as r3d.frag decode(), to 8-bit RGBA
void decode(uint16_t t, uint32_t fmt, uint8_t* o) {
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

// The texels of b that t has darkened as a painted shadow does (set_unpaint_shadows):
// 0 unless at least a fifth of the texels are the same, at least 3 in 100
// are b's at 0.30-0.47 of its brightness (FOREST's: 0.385) and the others
// changed are fewer than a tenth of those. First on one texel in 61, as
// most pairs differ everywhere. (No pair of the other courses' textures
// comes near: their road with and without a zebra crossing is the closest)
size_t painted_over(const uint16_t* t, const uint16_t* b, size_t n) {
    auto lum = [](uint16_t v) { return int((v >> 10) & 31) + int((v >> 5) & 31) + int(v & 31); };
    size_t darker = 0;
    for (size_t step : {size_t(61), size_t(1)}) {
        size_t same = 0, other = 0, all = 0;
        darker = 0;
        for (size_t i = 0; i < n; i += step, all++) {
            if (t[i] == b[i]) {
                same++;
                continue;
            }
            int lt = lum(t[i]), lb = lum(b[i]);
            if ((t[i] & 0x8000) == (b[i] & 0x8000) && lb > 6 && lt * 100 >= lb * 30 && lt * 100 <= lb * 47)
                darker++;
            else
                other++;
        }
        if (same * 5 < all || darker * 33 < all || other * 10 > darker) return 0;
    }
    return darker;
}

}  // namespace

void TexturePack::set_use_files(bool on) {
    if (on == use_files_) return;
    use_files_ = on;
    cache_.clear();
}

void TexturePack::set_unpaint_shadows(bool on) {
    if (on == unpaint_) return;
    unpaint_ = on;
    cache_.clear();
}

void TexturePack::init(const std::string& dir, const std::string& dump_dir, const std::string& clean_dir) {
    dir_ = dir;
    dump_dir_ = dump_dir;
    clean_dir_ = clean_dir;
    list_hashes(dir, available_);
    // Originals remembered by earlier runs: <texture word>_<hash>.png
    if (!clean_dir_.empty())
        for (const std::string& f : list_files(clean_dir_, "*.png")) {
            unsigned word = 0;
            unsigned long long hash = 0;
            Clean c;
            if (sscanf(f.c_str(), "%8x_%16llx", &word, &hash) != 2 || !available_.count(hash)) continue;
            bool known = false;
            for (const auto& [w, list] : clean_)
                for (const Clean& k : list) known |= k.hash == hash;
            if (known) continue;
            if (!png_load(clean_dir_ + "/" + f, c.w, c.h, c.rgba)) continue;
            c.hash = hash;
            clean_[word].push_back(std::move(c));
        }
    if (!dump_dir_.empty()) {
        CreateDirectoryA(dump_dir_.c_str(), nullptr);
        // Textures already dumped by earlier runs are not written again
        list_hashes(dump_dir_, dumped_);
    }
}

uint32_t TexturePack::lookup(uint32_t tex, R3DTextureRam& ram) {
    int x = int(tex & 2047), y = int((tex >> 11) & 2047);
    int w = 32 << ((tex >> 22) & 7), h = 32 << ((tex >> 25) & 7);
    uint64_t generation = ram.texture_generation(x, y, w, h);
    auto it = cache_.find(tex);
    if (it != cache_.end() && it->second.generation == generation) return it->second.slot;
    uint32_t slot = resolve(tex, ram, generation);
    cache_[tex] = {generation, slot};
    return slot;
}

uint64_t TexturePack::decode_texture(uint32_t tex, R3DTextureRam& ram, uint64_t& generation, int& w, int& h) {
    int x = int(tex & 2047), y = int((tex >> 11) & 2047);
    w = 32 << ((tex >> 22) & 7), h = 32 << ((tex >> 25) & 7);
    uint32_t fmt = tex >> 28;
    generation = ram.read_texture(x, y, w, h, texels_);
    rgba_.resize(texels_.size() * 4);
    for (size_t i = 0; i < texels_.size(); i++) decode(texels_[i], fmt, &rgba_[i * 4]);
    uint32_t dims[2] = {uint32_t(w), uint32_t(h)};
    return fnv1a_bytes(rgba_.data(), rgba_.size(), fnv1a_bytes(dims, sizeof(dims)));
}

void TexturePack::dump_car(uint32_t tex, R3DTextureRam& ram) {
    if (dump_dir_.empty()) return;
    int x = int(tex & 2047), y = int((tex >> 11) & 2047);
    int w = 32 << ((tex >> 22) & 7), h = 32 << ((tex >> 25) & 7);
    uint64_t generation = ram.texture_generation(x, y, w, h);
    auto it = car_words_.find(tex);
    if (it != car_words_.end() && it->second == generation) return;
    car_words_[tex] = generation;
    uint64_t hash = decode_texture(tex, ram, generation, w, h);
    if (!car_dumped_.insert(hash).second) return;
    std::string dir = dump_dir_ + "/car";
    CreateDirectoryA(dir.c_str(), nullptr);
    char name[32];
    snprintf(name, sizeof(name), "/%016llx.png", (unsigned long long)hash);
    png_save(dir + name, w, h, rgba_.data());
    if (FILE* f = fopen((dir + "/car.txt").c_str(), "a")) {
        fprintf(f, "%016llx  word %08x  page x %d y %d  %dx%d  format %u\n", (unsigned long long)hash, tex, x, y, w, h, tex >> 28);
        fclose(f);
    }
}

uint32_t TexturePack::resolve(uint32_t tex, R3DTextureRam& ram, uint64_t& generation) {
    int w, h;
    uint64_t hash = decode_texture(tex, ram, generation, w, h);
    char name[32];
    snprintf(name, sizeof(name), "/%016llx.png", (unsigned long long)hash);

    if (!dump_dir_.empty() && dumped_.insert(hash).second) png_save(dump_dir_ + name, w, h, rgba_.data());

    if (use_files_ && available_.count(hash)) {
        // A replaced texture: its original is remembered (once), to recognise
        // it later with dirt blended in
        bool known = false;
        for (const auto& [word, list] : clean_)
            for (const Clean& c : list) known |= c.hash == hash;
        if (!known) {
            clean_[tex].push_back({hash, w, h, rgba_});
            if (!clean_dir_.empty()) {
                CreateDirectoryA(clean_dir_.c_str(), nullptr);
                char file[64];
                snprintf(file, sizeof(file), "/%08x_%016llx.png", tex, (unsigned long long)hash);
                png_save(clean_dir_ + file, w, h, rgba_.data());
            }
        }
        return load_slot(hash);
    }
    if (uint32_t slot = unpainted_slot(tex, w, h, hash)) return slot;
    return use_files_ ? dirty_slot(w, h, hash) : 0;
}

// set_unpaint_shadows: a texture (texels_) that is one seen before with a
// shadow painted in gets a slot with that one's picture; and that one too, so
// the road is drawn the same way all along (textures from texture memory and
// pictures in slots are not filtered quite alike). Of several (FOREST: a
// painted one is also another painted one with a shadow more), the one it
// darkened most, the plain one. One that turns up later than the textures
// painted over it: those are looked up again
uint32_t TexturePack::unpainted_slot(uint32_t tex, int w, int h, uint64_t hash) {
    if (!unpaint_ || (tex >> 28) != 0 || w < 64 || h < 64) return 0;   // (ARGB1555 only)
    auto done = unpainted_.find(hash);
    if (done != unpainted_.end()) return done->second;
    const size_t n = size_t(w) * h;
    const Seen* original = nullptr;
    size_t most = 0;
    bool known = false;
    for (const Seen& s : seen_) {
        known |= s.hash == hash;
        if (s.hash == hash || s.w != w || s.h != h) continue;
        if (size_t d = painted_over(texels_.data(), s.texels.data(), n); d > most) {
            most = d;
            original = &s;
        } else if (painted_over(s.texels.data(), texels_.data(), n)) {
            unpainted_.erase(s.hash);
            cache_.erase(s.tex);
        }
    }
    uint32_t slot = 0;
    if (original) {
        const uint64_t key = original->hash ^ 0x5AD05AD05AD05AD0ull;   // (a slot of its own, never looked up by hash)
        if (auto loaded = slots_.find(key); loaded != slots_.end()) {
            slot = loaded->second;
        } else if (slots_.size() + 1 < max_slots_) {
            RgbaImage img;
            img.w = w, img.h = h;
            img.rgba.resize(n * 4);
            for (size_t i = 0; i < n; i++) decode(original->texels[i], 0, &img.rgba[i * 4]);
            slot = uint32_t(slots_.size() + 1);
            slots_[key] = slot;
            img.slot = slot;
            pending_.push_back(std::move(img));
            unpainted_[original->hash] = slot;
            cache_.erase(original->tex);
        }
        if (slot) {
            unpainted_[hash] = slot;
            printf("[textures] %016llx is %016llx with a shadow painted in: drawn without it\n",
                   (unsigned long long)hash, (unsigned long long)original->hash);
        }
    }
    if (!known) {
        seen_.push_back({hash, tex, w, h, texels_});
        // (a course's worth, some 16 MB: the oldest go first)
        size_t texels = 0;
        for (const Seen& s : seen_) texels += s.texels.size();
        while (texels > (8u << 20) && seen_.size() > 1) {
            texels -= seen_.front().texels.size();
            seen_.erase(seen_.begin());
        }
    }
    return slot;
}

uint32_t TexturePack::load_slot(uint64_t hash) {
    auto loaded = slots_.find(hash);
    if (loaded != slots_.end()) return loaded->second;
    if (!available_.count(hash) || slots_.size() + 1 >= max_slots_) return 0;
    char name[32];
    snprintf(name, sizeof(name), "/%016llx.png", (unsigned long long)hash);
    RgbaImage img;
    if (!png_load(dir_ + name, img.w, img.h, img.rgba)) {
        fprintf(stderr, "[textures] could not read %s%s\n", dir_.c_str(), name);
        available_.erase(hash);
        return 0;
    }
    uint32_t slot = uint32_t(slots_.size() + 1);
    img.slot = slot;
    slots_[hash] = slot;
    pending_.push_back(std::move(img));
    return slot;
}

// Edge strength of a picture, centred (mean 0): the brightness step to the
// right and below each pixel
void TexturePack::edge_map(const uint8_t* rgba, int w, int h, std::vector<float>& out) {
    auto lum = [&](int x, int y) {
        const uint8_t* p = rgba + (size_t(y) * w + x) * 4;
        return 0.3f * p[0] + 0.59f * p[1] + 0.11f * p[2];
    };
    out.assign(size_t(w - 1) * (h - 1), 0.0f);
    double sum = 0;
    for (int y = 0; y + 1 < h; y++)
        for (int x = 0; x + 1 < w; x++) {
            float l = lum(x, y);
            float v = std::fabs(lum(x + 1, y) - l) + std::fabs(lum(x, y + 1) - l);
            out[size_t(y) * (w - 1) + x] = v;
            sum += v;
        }
    float mean = out.empty() ? 0.0f : float(sum / out.size());
    for (float& v : out) v -= mean;
}

// A texture without a replacement may be a replaced one with dirt blended in
// (rgba_ holds it): the original at the same place that shares the most
// pixels with it, at least a few in ten. Its dirt: where the colour moved,
// how far towards the dirt's colour (that colour taken as the average of the
// most changed pixels), as colour and amount for the shader to lay over the
// replacement
uint32_t TexturePack::dirty_slot(int w, int h, uint64_t hash) {
    auto done = dirty_.find(hash);
    if (done != dirty_.end()) return done->second;
    uint32_t result = 0;
    // Any original of the same size, wherever it was seen: a rival's car is
    // loaded elsewhere in texture memory than the same car seen clean (a
    // different car shares only a few pixels in a hundred with it)
    // Compared by the drawing's outlines: the correlation of the two pictures'
    // edge strength (brightness steps between neighbours). Mud and snow tint
    // and veil the colours (a thick coat left a fifth of the pixels near
    // their colour) but the lettering and stripes still show through: the
    // right original scores 0.77-0.98, another car's texture of the same
    // size 0.5 at most
    const Clean* best = nullptr;
    size_t n = size_t(w) * h;
    std::vector<float> e;
    edge_map(rgba_.data(), w, h, e);
    float best_score = 0;
    for (auto& [word, list] : clean_)
        for (Clean& c : list) {
            if (c.w != w || c.h != h) continue;
            if (c.edges.empty()) edge_map(c.rgba.data(), w, h, c.edges);
            double xy = 0, xx = 0, yy = 0;
            for (size_t i = 0; i < e.size(); i++) {
                xy += double(e[i]) * c.edges[i];
                xx += double(e[i]) * e[i];
                yy += double(c.edges[i]) * c.edges[i];
            }
            float score = xx > 0 && yy > 0 ? float(xy / std::sqrt(xx * yy)) : 0.0f;
            if (score > best_score) best = &c, best_score = score;
        }
    size_t best_same = size_t(best_score * 100);
    // (0.72: the rivals' number plates, another number on the same plate, came to 0.65 with ours)
    // Another drawing, not dirt: one coat has one colour, so it lightens dark
    // pixels (snow, dust) or darkens light ones (mud), never both. The race
    // number plate with the player's position (a clean "2" came to 0.755 with
    // the "1", which has the only HD plate) turns black strokes white and white
    // ones black: it keeps the game's own picture (issue #125)
    if (best) {
        auto lum = [](const uint8_t* p) { return 0.3f * p[0] + 0.59f * p[1] + 0.11f * p[2]; };
        size_t to_white = 0, to_black = 0;
        for (size_t i = 0; i < n; i++) {
            float was = lum(&best->rgba[i * 4]), now = lum(&rgba_[i * 4]);
            to_white += was < 64 && now > 192;
            to_black += was > 192 && now < 64;
        }
        if (to_white > n / 256 && to_black > n / 256) best = nullptr;
    }
    if (best && best_score >= 0.72f && slots_.size() + 2 < max_slots_) {
        uint32_t base = load_slot(best->hash);
        if (base) {
            // the dirt's colour: average of the pixels that moved the most
            double sum[3] = {0, 0, 0}, weight = 0;
            for (size_t i = 0; i < n; i++) {
                float d = 0;
                for (int k = 0; k < 3; k++) d += std::fabs(float(rgba_[i * 4 + k]) - float(best->rgba[i * 4 + k]));
                if (d < 60) continue;
                for (int k = 0; k < 3; k++) sum[k] += d * rgba_[i * 4 + k];
                weight += d;
            }
            float mud[3];
            for (int k = 0; k < 3; k++) mud[k] = weight > 0 ? float(sum[k] / weight) : 128.0f;
            RgbaImage img;
            img.w = w, img.h = h;
            img.rgba.resize(n * 4);
            for (size_t i = 0; i < n; i++) {
                const uint8_t *c = &best->rgba[i * 4], *d = &rgba_[i * 4];
                float to_mud = 0, moved = 0, len = 0;
                for (int k = 0; k < 3; k++) {
                    float m = mud[k] - c[k], v = float(d[k]) - c[k];
                    to_mud += m * v;
                    len += m * m;
                    moved += std::fabs(v);
                }
                float a = moved < 6 ? 0.0f : len > 1 ? std::clamp(to_mud / len, 0.0f, 1.0f) : 1.0f;
                if (moved >= 6 && a < 0.2f) a = std::clamp(moved / 255.0f, 0.2f, 1.0f);   // moved, not towards the average
                uint8_t* o = &img.rgba[i * 4];
                for (int k = 0; k < 3; k++)
                    o[k] = uint8_t(std::clamp(a > 0.05f ? c[k] + (float(d[k]) - c[k]) / a : mud[k], 0.0f, 255.0f));
                o[3] = uint8_t(a * 255.0f + 0.5f);
            }
            uint32_t slot = uint32_t(slots_.size() + 1);
            slots_[hash ^ 0xD1D1D1D1D1D1D1D1ull] = slot;   // (a slot of its own, never looked up by hash)
            img.slot = slot;
            pending_.push_back(std::move(img));
            result = base | slot << 16;
            printf("[textures] %016llx is %016llx with dirt (outlines alike %d%%)\n", (unsigned long long)hash,
                   (unsigned long long)best->hash, int(best_same));
        }
    }
    if (result) dirty_[hash] = result;   // (a miss is looked at again: its original may turn up later)
    return result;
}

uint32_t TexturePack::own_picture(const std::string& file, float top, float bottom, bool flip,
                                  const std::string& over) {
    char part[48];
    snprintf(part, sizeof(part), "|%.4f|%.4f|%d", top, bottom, int(flip));
    std::string name = file + part + "|" + over;
    uint64_t id = fnv1a_bytes(name.data(), name.size()) ^ 0x0A0A0A0A0A0A0A0Aull;   // (never a texture's hash)
    auto loaded = slots_.find(id);
    if (loaded != slots_.end()) return loaded->second;
    if (own_missing_.count(id) || slots_.size() + 1 >= max_slots_) return 0;
    RgbaImage full;
    if (!png_load(file, full.w, full.h, full.rgba)) {
        fprintf(stderr, "[textures] could not read %s\n", file.c_str());
        own_missing_.insert(id);
        return 0;
    }
    if (!over.empty()) {
        RgbaImage top_image;
        if (!png_load(over, top_image.w, top_image.h, top_image.rgba))
            fprintf(stderr, "[textures] could not read %s\n", over.c_str());
        else if (top_image.w != full.w || top_image.h != full.h)
            fprintf(stderr, "[textures] %s is not the size of %s, left out\n", over.c_str(), file.c_str());
        else
            for (size_t i = 0; i < full.rgba.size(); i += 4) {
                const uint8_t* o = &top_image.rgba[i];
                for (int k = 0; k < 3; k++) full.rgba[i + k] = uint8_t((o[k] * o[3] + full.rgba[i + k] * (255 - o[3]) + 127) / 255);
            }
    }
    int y0 = std::clamp(int(std::lround(top * full.h)), 0, full.h);
    int y1 = std::clamp(int(std::lround(bottom * full.h)), y0, full.h);
    if (y1 == y0) {
        own_missing_.insert(id);
        return 0;
    }
    RgbaImage img;
    img.w = full.w;
    img.h = y1 - y0;
    img.rgba.resize(size_t(img.w) * img.h * 4);
    size_t row = size_t(img.w) * 4;
    for (int y = 0; y < img.h; y++)
        memcpy(&img.rgba[size_t(y) * row], &full.rgba[size_t(flip ? y1 - 1 - y : y0 + y) * row], row);
    uint32_t slot = uint32_t(slots_.size() + 1);
    img.slot = slot;
    slots_[id] = slot;
    printf("[textures] %s, rows %d-%d: own picture %dx%d\n", file.c_str(), y0, y1, img.w, img.h);
    pending_.push_back(std::move(img));
    return slot;
}

std::vector<RgbaImage> TexturePack::take_new_images() {
    std::vector<RgbaImage> out;
    out.swap(pending_);
    return out;
}

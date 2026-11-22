// Texture packs: replacement images for the game's textures.
//
// Every texture a polygon uses is identified by a hash of its decoded RGBA
// contents, so the same texture is recognised wherever and whenever the game
// uploads it. With dumping enabled each new texture is written once as
// out/textures/<hash>.png; a file with the same name in textures/ replaces
// it, at any resolution and with its own alpha channel. The textures of the
// player's car (Sega Rally 2, R3D_CAR) are also written to
// out/textures/car/, so they can be found among the thousands of others.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "image_io.h"
#include "r3d_state.h"

class TexturePack {
public:
    static constexpr uint32_t MAX_SLOTS = 4096;   // replacement images on the GPU (slot 0 = none)

    // dir: folder with replacement PNGs; dump_dir: where new textures are
    // written (empty = no dumping); clean_dir: where the originals of the
    // replaced textures are remembered (empty = not kept between runs)
    void init(const std::string& dir, const std::string& dump_dir, const std::string& clean_dir = "");
    bool active() const { return unpaint_ || (use_files_ && !available_.empty()) || !dump_dir_.empty(); }

    // HD LIVERIES off: the replacement files are not used (the cars keep the
    // ROM's own textures); set_unpaint_shadows still applies
    void set_use_files(bool on);
    // Sun shadows on: a texture that is another one with a shadow painted in
    // is drawn as that other one, and the sun shadows put the shadow where it
    // falls. FOREST's start has its road in five pictures: the plain road and
    // four copies of it with shadows painted in (each texel there at 0.385 of
    // its brightness), and with the sun shadows over them every shadow came
    // twice. The other courses have none (snapshots of all four looked at)
    void set_unpaint_shadows(bool on);
    // Fewer slots than MAX_SLOTS when the GPU cannot hold that many (R3DRenderer::texture_slots)
    void set_max_slots(uint32_t n) { max_slots_ = n < MAX_SLOTS ? n : MAX_SLOTS; }

    // Replacement slot for a polygon texture word (R3DVertex::tex), 0 = none.
    // Bits 16-31: a dirt picture over it (Sega Rally 2 blends mud and snow
    // into the car's textures: the dirty texture is recognised as a known
    // clean one at the same place, and what changed becomes a picture of
    // dirt colour (RGB) and amount (A), see r3d.frag)
    uint32_t lookup(uint32_t tex, R3DTextureRam& ram);

    // With dumping on: also writes a texture the player's car uses to
    // <dump_dir>/car/ (same file name), once per texture word
    void dump_car(uint32_t tex, R3DTextureRam& ram);

    // A picture of the port's own, not one of the game's (the FOREST card of
    // SELECT COURSE, R3DSceneBuilder::own_texture): the rows from top to
    // bottom (fractions of the height) of a PNG, turned upside down when flip
    // (a texture's first row in texture RAM may be the bottom of what it
    // shows). over (optional): a PNG of the same size drawn over it first
    // (alpha), left out when it cannot be read. Its slot, 0 when the file
    // cannot be read
    uint32_t own_picture(const std::string& file, float top, float bottom, bool flip, const std::string& over = {});

    // Replacements loaded since the last call, to be uploaded by the renderer
    std::vector<RgbaImage> take_new_images();


private:
    uint32_t max_slots_ = MAX_SLOTS;
    struct Entry {
        uint64_t generation;
        uint32_t slot;
    };
    uint32_t resolve(uint32_t tex, R3DTextureRam& ram, uint64_t& generation);
    uint64_t decode_texture(uint32_t tex, R3DTextureRam& ram, uint64_t& generation, int& w, int& h);
    uint32_t load_slot(uint64_t hash);   // the replacement's slot, loading it (0 = none)
    uint32_t dirty_slot(int w, int h, uint64_t hash);
    uint32_t unpainted_slot(uint32_t tex, int w, int h, uint64_t hash);

    // set_unpaint_shadows: the textures seen (ARGB1555, 64x64 or larger), to
    // find a painted texture's original among them
    struct Seen {
        uint64_t hash;
        uint32_t tex;   // its texture word: looked up again (cache_) when its original turns up later
        int w, h;
        std::vector<uint16_t> texels;
    };
    std::vector<Seen> seen_;
    std::unordered_map<uint64_t, uint32_t> unpainted_;   // painted texture's hash -> its original's slot
    bool use_files_ = true, unpaint_ = false;

    // The originals of replaced textures, by texture word (several cars use the same place)
    struct Clean {
        uint64_t hash;
        int w, h;
        std::vector<uint8_t> rgba;
        std::vector<float> edges;   // edge_map, made when first compared
    };
    static void edge_map(const uint8_t* rgba, int w, int h, std::vector<float>& out);
    std::unordered_map<uint32_t, std::vector<Clean>> clean_;
    std::unordered_map<uint64_t, uint32_t> dirty_;   // dirty texture hash -> replace value (0 = not one)
    std::string clean_dir_;

    std::string dir_, dump_dir_;
    std::unordered_map<uint32_t, Entry> cache_;            // texture word -> slot
    std::unordered_set<uint64_t> available_;               // hashes with a replacement file
    std::unordered_map<uint64_t, uint32_t> slots_;         // loaded hash -> slot
    std::unordered_set<uint64_t> dumped_;
    std::unordered_set<uint64_t> own_missing_;             // own pictures whose file could not be read
    std::unordered_map<uint32_t, uint64_t> car_words_;     // car texture word -> generation dumped
    std::unordered_set<uint64_t> car_dumped_;
    std::vector<RgbaImage> pending_;
    std::vector<uint16_t> texels_;
    std::vector<uint8_t> rgba_;
};

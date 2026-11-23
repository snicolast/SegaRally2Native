// Replacements for 2D graphics (tilegen layers above the 3D image), such as
// the route map shown before each stage.
//
// The foreground 2D image is split into 8x8-pixel cells. A replacement is a
// rectangle of cells with the hash each cell had when it was captured, plus a
// picture to show there. While most of those cells are on screen unchanged the
// picture replaces them; cells that differ (the route drawn so far, the hint
// text) keep the game's own pixels on top, so live information is not lost.
//
// Files in textures/2d/: <name>.txt (made by tools/make_2d_replacement.py from
// a capture) and <name>.png (the new picture, any resolution, with alpha).
#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "image_io.h"

class Pack2D {
public:
    static constexpr int CELLS_X = 62, CELLS_Y = 48;   // 496x384 in 8x8 cells
    // Pictures on the GPU (compose.frag overlays[], slot 0 = none). A slot
    // number is one byte in the text cells (the list and the lit marks), so
    // 256 is the most there can be
    static constexpr uint32_t MAX_SLOTS = 256;
    static_assert(MAX_SLOTS <= 256, "slots are written as one byte in the text cells");

    // Pictures use the GPU slots first_slot..last_slot (the foreground and the
    // layers below the 3D image each have their own Pack2D and range). When a
    // session meets more different pictures than that, a new one takes the slot
    // of the one not shown for longest (its file is read again if it comes back)
    void init(const std::string& dir, uint32_t first_slot = 1, uint32_t last_slot = MAX_SLOTS - 1);
    bool empty() const { return entries_.empty(); }
    // How "when" lines read the game's RAM (none: those replacements never show)
    static void set_ram_reader(uint32_t (*read)(uint32_t addr)) { ram_reader_ = read; }

    // Hashes of all the cells of a 496x384 BGRA image
    static void hash_cells(const uint32_t* front, std::vector<uint64_t>& out);
    // Capture for tools/make_2d_replacement.py: <path>.png and <path>.cells
    static void save_capture(const uint32_t* front, const std::string& path);

    static constexpr int MAX_ACTIVE = 16;   // pictures on screen at once, per Pack2D (the race HUD shows up to 12)
    // Finds the replacements on screen (anywhere, at any brightness: cells are
    // compared on `raw`, the 496x384 layers without fade offsets), clears their
    // unchanged cells in `target` (stride pixels per row) and, for the
    // foreground, in the high-resolution text cells so the pictures show
    // through, and adds them to the list in the text cells' last row (see
    // text_hd.h), marked as below the 3D image when `below_3d`. Returns how many.
    // raw_top: `raw` with only the top layer pair, where texts are drawn; a
    // reference cell captured that way matches whatever is behind it
    // under (optional, 496 pixels per row): what the cleared cells show
    // instead of nothing (the picture behind a replaced text), copied into
    // under_dst (under_stride pixels per row; by default `target` itself).
    // The pictures are drawn above the 3D image and below the foreground, so
    // for the foreground it goes into the layers below the 3D image
    int apply(uint32_t* target, int stride, const uint32_t* raw, const uint32_t* raw_top, uint32_t* text_cells,
              bool below_3d = false, const uint32_t* under = nullptr, uint32_t* under_dst = nullptr,
              int under_stride = 0);

    // Pictures loaded since the last call, to be uploaded by the renderer (a
    // reused slot comes again with its new picture, which replaces the old one)
    std::vector<RgbaImage> take_new_images();

private:
    static inline uint32_t (*ram_reader_)(uint32_t) = nullptr;
    struct Entry {
        std::string name;
        int cx = 0, cy = 0, cw = 0, ch = 0;   // rectangle in cells
        std::vector<uint64_t> cells;          // reference hash per cell (0 = ignored)
        uint32_t slot = 0;                    // 0 = picture not loaded (yet, or its slot was reused)
        bool failed = false;                  // its file could not be read: not tried again
        int min_share = 50;   // "match P" line: P% of the reference cells must be unchanged (titles that differ
                              // only in a digit, STAGE1 and STAGE5, need nearly all of them)
        // "when ADDR VALUE" lines (hex address, decimal value): shown only while
        // those words of the game's RAM hold those values (FOREST's start
        // screen is MOUNTAIN's picture: "when 22cf44 1", the course id)
        std::vector<std::pair<uint32_t, uint32_t>> when;
        bool cover = false;   // "cover" line: the picture hides every cell of its box (glints over a text, the
                              // picture behind it, letters wider than the old ones)
        // A second state of the same cells (<name>_lit.txt / .png: a bar graph
        // lit up): a cell showing it gets that picture instead (drawn over the
        // first one, masked per cell through the text cells, see compose.frag)
        std::vector<uint64_t> lit;
        std::string lit_name;
        uint32_t lit_slot = 0;
        // "px CELL p0 .. p63" lines (BGRA, hex): the pixels of a cell that changes
        // between the two states, in each state (px_off from <name>.txt, px_lit from
        // <name>_lit.txt). A cell caught between them (a bar graph's last bars half
        // lit) shows each 2x2-pixel block in the state its pixels are closer to
        std::unordered_map<int, std::array<uint32_t, 64>> px_off, px_lit;
        // <name>_seg.png (optional, the size of the pictures): the bar graph's
        // segments, numbered in the order they light up (red 1..255, 0 = none).
        // The last lit segment is then voted over all the cells that change, and
        // every cell with segments shows the lit picture up to it, cut along the
        // segments' own edges instead of the cells' or 2x2-pixel blocks
        // (compose.frag). seg2d: the number at the centre of each original
        // pixel of the box (empty: no such file)
        uint32_t seg_slot = 0;
        bool seg_tried = false;
        std::vector<uint8_t> seg2d;
        std::vector<uint8_t> seg_first;   // per cell: its lowest segment number (0: none)
    };
    // Who holds each slot of the range, and the last apply() that showed it
    struct SlotUse {
        int entry = -1;   // index in entries_ (-1 = free)
        int kind = 0;     // the entry's picture (0), lit picture (1) or segment map (2)
        uint64_t shown = 0;
    };
    std::string dir_;
    uint32_t first_slot_ = 1, last_slot_ = MAX_SLOTS - 1;
    std::vector<Entry> entries_;
    std::vector<SlotUse> slots_;
    uint64_t frame_ = 0;   // apply() calls
    bool reuse_told_ = false;
    // A slot for picture `entry` (kind: as SlotUse): a free one, else the one
    // not shown for longest; 0 when all were shown in the last frames
    uint32_t take_slot(int entry, int kind);
    void mark_shown(uint32_t slot) { slots_[slot - first_slot_].shown = frame_; }
    std::vector<RgbaImage> pending_;
    std::vector<uint64_t> hashes_, hashes_top_;
    int matches(const Entry& e, int cx, int cy, int& total) const;
    // For a cell of `e` (index in its box) that shows neither state: which of
    // its 4x4 blocks of 2x2 pixels show the lit state (bit y * 4 + x), each
    // block as most of its changing pixels in `raw` are closer to one state or
    // the other (blocks with none of them: as most of the cell's are). 0 = none
    // lit, or no reference pixels for it
    static uint32_t lit_blocks(const Entry& e, int index, const uint32_t* raw, int px, int py);
    // The same cell with a segment map: adds to votes[segment] +1 for each of
    // its changing pixels closer to the lit state, -1 for the others, and +1
    // for each pixel unlike both captures where they are alike (a bar lit in
    // neither of them)
    static void vote_segments(const Entry& e, int index, const uint32_t* raw, int px, int py, int* votes);
    bool same(int cy, int cx, uint64_t ref) const {
        return hashes_[cy * CELLS_X + cx] == ref || hashes_top_[cy * CELLS_X + cx] == ref;
    }
    static uint64_t empty_cell_hash();
};

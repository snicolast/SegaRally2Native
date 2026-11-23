// HD HUD pictures (src/video/pack2d.cpp): a session that meets more different
// pictures than GPU slots in the range. Each must still be shown whenever it
// comes on screen (the first ones again later), in a slot of the range, and
// the ones staying on screen keep theirs while the others come and go.
// And a bar graph with a segment map, filled to each level: every cell with
// segments is drawn from the voted level, whatever cell its segments end in.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "image_io.h"
#include "pack2d.h"
#include "text_hd.h"
#include "tilegen.h"

static int g_failed = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAILED line %d: %s\n", __LINE__, #cond);            \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

// Pictures 0-59 share one 4x4-cell box (like the co-driver signs); 60-62 have
// a box each (like the gear box and the tachometer, on screen all race)
constexpr int SIGNS = 60, FIXED = 3, PICTURES = SIGNS + FIXED;
static int box_x(int i) { return i < SIGNS ? 10 : 20 + 10 * (i - SIGNS); }
static int box_y(int i) { return i < SIGNS ? 10 : 30; }

// The screen with these pictures: noise of its own in each one's box
static std::vector<uint32_t> screen(const std::vector<int>& pictures) {
    std::vector<uint32_t> s(SCREEN_W * SCREEN_H, 0);
    for (int i : pictures) {
        uint32_t r = 0x9E3779B9u * uint32_t(i + 1);
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) {
                r ^= r << 13;
                r ^= r >> 17;
                r ^= r << 5;
                s[(box_y(i) * 8 + y) * SCREEN_W + box_x(i) * 8 + x] = 0xFF000000u | (r & 0xFFFFFF);
            }
    }
    return s;
}

static void write_pack(const std::string& dir) {
    std::filesystem::create_directories(dir);
    std::vector<uint8_t> png(16 * 16 * 4, 255);
    for (int i = 0; i < PICTURES; i++) {
        char name[16];
        snprintf(name, sizeof name, "p%02d", i);
        std::vector<uint64_t> cells;
        Pack2D::hash_cells(screen({i}).data(), cells);
        FILE* f = fopen((dir + "/" + name + ".txt").c_str(), "w");
        fprintf(f, "rect %d %d 4 4\n", box_x(i), box_y(i));
        for (int cy = box_y(i); cy < box_y(i) + 4; cy++)
            for (int cx = box_x(i); cx < box_x(i) + 4; cx++)
                fprintf(f, "%016llx\n", (unsigned long long)cells[cy * Pack2D::CELLS_X + cx]);
        fclose(f);
        png_save(dir + "/" + name + ".png", 16, 16, png.data());
    }
}

// One frame with these pictures on screen: the slot each was listed with (0 = not shown)
static std::vector<uint32_t> show(Pack2D& pack, const std::vector<int>& pictures, std::vector<uint32_t>& text_cells) {
    std::vector<uint32_t> raw = screen(pictures), target = raw;
    std::fill(text_cells.begin(), text_cells.end(), 0);   // TileGen::compose empties the list every frame
    pack.apply(target.data(), SCREEN_W, raw.data(), raw.data(), text_cells.data());
    const uint32_t* list = text_cells.data() + TextHD::CELLS_Y * TextHD::ROW_TEXELS;
    std::vector<uint32_t> slots(pictures.size(), 0);
    for (uint32_t k = 0; k < list[0] >> 16; k++) {
        int cx = int(list[1 + 2 * k] >> 16 & 0xFF);
        for (size_t p = 0; p < pictures.size(); p++)
            if (box_x(pictures[p]) == cx) slots[p] = list[2 + 2 * k] >> 16;
    }
    return slots;
}

// A bar graph of 4x1 cells: five segments 6 pixels wide (x 0-29, so they end
// inside the cells), grey when off and yellow when lit, x 30-31 never changes
constexpr int BAR_X = 30, BAR_Y = 20, SEG_W = 6, SEGS = 5;
static uint32_t bar_pixel(int x, int level) {
    if (x >= SEG_W * SEGS) return 0xFF101010u;
    return x / SEG_W < level ? 0xFFFFFF00u : 0xFF404040u;
}
static std::vector<uint32_t> bar_screen(int level) {
    std::vector<uint32_t> s(SCREEN_W * SCREEN_H, 0);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 32; x++) s[(BAR_Y * 8 + y) * SCREEN_W + BAR_X * 8 + x] = bar_pixel(x, level);
    return s;
}
static void write_bar(const std::string& dir) {
    std::filesystem::create_directories(dir);
    for (int lit = 0; lit < 2; lit++) {
        std::vector<uint32_t> s = bar_screen(lit ? SEGS : 0), other = bar_screen(lit ? 0 : SEGS);
        std::vector<uint64_t> cells, other_cells;
        Pack2D::hash_cells(s.data(), cells);
        Pack2D::hash_cells(other.data(), other_cells);
        std::string name = dir + (lit ? "/bar_lit" : "/bar");
        FILE* f = fopen((name + ".txt").c_str(), "w");
        fprintf(f, "rect %d %d 4 1\n", BAR_X, BAR_Y);
        for (int cx = BAR_X; cx < BAR_X + 4; cx++)
            fprintf(f, "%016llx\n", (unsigned long long)cells[BAR_Y * Pack2D::CELLS_X + cx]);
        for (int c = 0; c < 4; c++) {   // the pixels of the cells that change
            if (cells[BAR_Y * Pack2D::CELLS_X + BAR_X + c] == other_cells[BAR_Y * Pack2D::CELLS_X + BAR_X + c]) continue;
            fprintf(f, "px %d", c);
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) fprintf(f, " %08x", bar_pixel(c * 8 + x, lit ? SEGS : 0));
            fprintf(f, "\n");
        }
        fclose(f);
        std::vector<uint8_t> png(32 * 8 * 4, 255);
        png_save(name + ".png", 32, 8, png.data());
    }
    std::vector<uint8_t> seg(32 * 8 * 4, 255);   // red: the segment (1-5), 0 past them
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 32; x++) seg[(y * 32 + x) * 4] = uint8_t(x < SEG_W * SEGS ? x / SEG_W + 1 : 0);
    png_save(dir + "/bar_seg.png", 32, 8, seg.data());
}

static void bar_graph_test(const std::string& dir) {
    write_bar(dir);
    Pack2D pack;
    pack.init(dir, 1, 10);
    std::vector<uint32_t> text_cells(TextHD::ROWS * TextHD::ROW_TEXELS);
    const int first_segment[4] = {1, 2, 3, 5};   // the lowest segment in each cell
    for (int level = 0; level <= SEGS; level++) {
        std::vector<uint32_t> raw = bar_screen(level), target = raw;
        std::fill(text_cells.begin(), text_cells.end(), 0);
        CHECK(pack.apply(target.data(), SCREEN_W, raw.data(), raw.data(), text_cells.data()) == 1);
        if (level == 0) CHECK(pack.take_new_images().size() == 3);   // off, lit and the segment map
        for (int c = 0; c < 4; c++) {
            const uint32_t* cell = text_cells.data() + (BAR_Y * Pack2D::CELLS_X + BAR_X + c) * TextHD::CELL_TEXELS;
            bool lit = first_segment[c] <= level;
            CHECK((cell[1] >> 24 == 0xFF) == lit);    // the lit picture in this cell...
            CHECK((cell[2] >> 24 == 0xFF) == lit);    // ...cut at the segment map's level
            if (lit) CHECK((cell[2] & 0xFF) == uint32_t(level));
            for (int y = 0; y < 8; y++)   // the game's cell gives way to the pictures
                for (int x = 0; x < 8; x++) CHECK(target[(BAR_Y * 8 + y) * SCREEN_W + (BAR_X + c) * 8 + x] == 0);
        }
    }
    std::filesystem::remove_all(dir);
}

int main() {
    const uint32_t first = 1, last = 27;   // the old foreground range, fewer slots than pictures
    std::string dir = (std::filesystem::temp_directory_path() / "sr2_pack2d_test").string();
    std::filesystem::remove_all(dir);
    write_pack(dir);

    Pack2D pack;
    pack.init(dir, first, last);
    std::vector<uint32_t> text_cells(TextHD::ROWS * TextHD::ROW_TEXELS);
    std::vector<uint32_t> in_slot(last + 1, ~0u);   // picture whose image the renderer has in each slot
    uint32_t fixed_slot[FIXED] = {};
    int shown = 0, frames = 0;
    // Every sign in turn, twice over (the second time the first ones come back
    // after the range filled up), with the three fixed pictures on screen too
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < SIGNS; i++, frames++) {
            std::vector<int> on = {i, SIGNS, SIGNS + 1, SIGNS + 2};
            std::vector<uint32_t> slots = show(pack, on, text_cells);
            for (const RgbaImage& img : pack.take_new_images()) {
                CHECK(img.slot >= first && img.slot <= last);
                if (img.slot >= first && img.slot <= last) in_slot[img.slot] = 1000;   // known after this loop
            }
            for (size_t p = 0; p < on.size(); p++) {
                uint32_t s = slots[p];
                CHECK(s >= first && s <= last);
                if (s < first || s > last) continue;
                shown++;
                // drawn from a slot that holds its own picture
                CHECK(in_slot[s] == 1000 || in_slot[s] == uint32_t(on[p]));
                in_slot[s] = uint32_t(on[p]);
                if (on[p] >= SIGNS) {   // the fixed ones never move
                    uint32_t& f = fixed_slot[on[p] - SIGNS];
                    CHECK(!f || f == s);
                    f = s;
                }
            }
            // no two pictures on screen share a slot
            std::vector<uint32_t> sorted = slots;
            std::sort(sorted.begin(), sorted.end());
            CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
        }
    printf("%d of %d pictures shown (slots %u-%u, %d different)\n", shown, frames * (1 + FIXED), first, last,
           PICTURES);
    CHECK(shown == frames * (1 + FIXED));

    // A picture that stays on screen is loaded once
    show(pack, {7}, text_cells);
    pack.take_new_images();
    for (int k = 0; k < 3; k++) {
        CHECK(show(pack, {7}, text_cells)[0] != 0);
        CHECK(pack.take_new_images().empty());
    }
    std::filesystem::remove_all(dir);
    bar_graph_test(dir + "_bar");
    if (g_failed) printf("%d check(s) failed\n", g_failed);
    else printf("pack2d: all checks passed\n");
    return g_failed ? 1 : 0;
}

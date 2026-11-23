#include "pack2d.h"

#include "../common/fnv1a.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "text_hd.h"
#include "tilegen.h"

void Pack2D::hash_cells(const uint32_t* front, std::vector<uint64_t>& out) {
    out.assign(CELLS_X * CELLS_Y, 0);
    for (int cy = 0; cy < CELLS_Y; cy++)
        for (int cx = 0; cx < CELLS_X; cx++) {
            uint64_t h = FNV1A_BASIS;
            for (int y = 0; y < 8; y++) h = fnv1a_words(front + (cy * 8 + y) * SCREEN_W + cx * 8, 8, h);
            out[cy * CELLS_X + cx] = h ? h : 1;   // 0 is reserved for "ignored"
        }
}

void Pack2D::save_capture(const uint32_t* front, const std::string& path) {
    std::vector<uint8_t> rgba(SCREEN_W * SCREEN_H * 4);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint32_t c = front[i];
        rgba[i * 4 + 0] = uint8_t(c >> 16);
        rgba[i * 4 + 1] = uint8_t(c >> 8);
        rgba[i * 4 + 2] = uint8_t(c);
        rgba[i * 4 + 3] = uint8_t(c >> 24);
    }
    png_save(path + ".png", SCREEN_W, SCREEN_H, rgba.data());
    std::vector<uint64_t> cells;
    hash_cells(front, cells);
    if (FILE* f = fopen((path + ".cells").c_str(), "w")) {
        for (int cy = 0; cy < CELLS_Y; cy++)
            for (int cx = 0; cx < CELLS_X; cx++)
                fprintf(f, "%016llx%c", (unsigned long long)cells[cy * CELLS_X + cx], cx + 1 < CELLS_X ? ' ' : '\n');
        fclose(f);
    }
}

void Pack2D::init(const std::string& dir, uint32_t first_slot, uint32_t last_slot) {
    dir_ = dir;
    first_slot_ = std::max(first_slot, 1u);
    last_slot_ = std::min(last_slot, MAX_SLOTS - 1);
    slots_.assign(first_slot_ <= last_slot_ ? last_slot_ - first_slot_ + 1 : 0, SlotUse{});
    for (const std::string& file : list_files(dir, "*.txt")) {
        Entry e;
        e.name = file.substr(0, file.size() - 4);
        FILE* f = fopen((dir + "/" + file).c_str(), "r");
        if (!f) continue;
        char line[1024];   // ("px" lines: 64 pixels of 8 digits)
        while (fgets(line, sizeof(line), f)) {
            unsigned long long h;
            if (line[0] == '#') continue;
            if (sscanf(line, "rect %d %d %d %d", &e.cx, &e.cy, &e.cw, &e.ch) == 4) continue;
            if (sscanf(line, "match %d", &e.min_share) == 1) continue;
            unsigned when_addr = 0, when_value = 0;
            if (sscanf(line, "when %x %u", &when_addr, &when_value) == 2) {
                e.when.push_back({when_addr, when_value});
                continue;
            }
            if (strncmp(line, "cover", 5) == 0) {   // texts: every cell of the box gives way, changed or empty
                e.cover = true;
                continue;
            }
            if (strncmp(line, "px ", 3) == 0) {
                std::array<uint32_t, 64> px{};
                int cell = -1, used = 0, at = 3;
                bool ok = sscanf(line + at, "%d%n", &cell, &used) == 1;
                at += used;
                for (int i = 0; ok && i < 64; i++, at += used) ok = sscanf(line + at, "%x%n", &px[i], &used) == 1;
                if (ok && cell >= 0) e.px_off[cell] = px;
                continue;
            }
            if (sscanf(line, "%llx", &h) == 1) e.cells.push_back(h == empty_cell_hash() ? 0 : h);
        }
        fclose(f);
        bool valid = e.cw > 0 && e.ch > 0 && e.cx >= 0 && e.cy >= 0 && e.cx + e.cw <= CELLS_X &&
                     e.cy + e.ch <= CELLS_Y && e.cells.size() == size_t(e.cw) * e.ch;
        if (valid) entries_.push_back(std::move(e));
        else fprintf(stderr, "[2d] %s/%s is not valid\n", dir.c_str(), file.c_str());
    }
    // <name>_lit: the lit state of <name> (same rectangle), not a replacement of its own
    for (size_t i = 0; i < entries_.size();) {
        const std::string& n = entries_[i].name;
        auto base = n.size() > 4 && n.compare(n.size() - 4, 4, "_lit") == 0
                        ? std::find_if(entries_.begin(), entries_.end(),
                                       [&](const Entry& b) { return b.name == n.substr(0, n.size() - 4); })
                        : entries_.end();
        if (base == entries_.end() || base->cw != entries_[i].cw || base->ch != entries_[i].ch) {
            i++;
            continue;
        }
        base->lit = std::move(entries_[i].cells);
        base->lit_name = n;
        base->px_lit = std::move(entries_[i].px_off);
        entries_.erase(entries_.begin() + i);
    }
    if (!entries_.empty()) printf("[2d] %zu 2D graphics replacements\n", entries_.size());
}

// Reference cells of `e` found unchanged with its top-left cell at (cx, cy)
int Pack2D::matches(const Entry& e, int cx, int cy, int& total) const {
    int matched = 0;
    total = 0;
    for (int y = 0; y < e.ch; y++)
        for (int x = 0; x < e.cw; x++) {
            uint64_t ref = e.cells[y * e.cw + x];
            uint64_t lit = e.lit.empty() ? 0 : e.lit[y * e.cw + x];
            if (!ref && !lit) continue;
            total++;
            matched += (ref && same(cy + y, cx + x, ref)) || (lit && same(cy + y, cx + x, lit));
        }
    return matched;
}

uint32_t Pack2D::lit_blocks(const Entry& e, int index, const uint32_t* raw, int px, int py) {
    auto off = e.px_off.find(index), lit = e.px_lit.find(index);
    if (off == e.px_off.end() || lit == e.px_lit.end()) return 0;
    auto distance = [](uint32_t a, uint32_t b) {
        int d = 0;
        for (int shift = 0; shift < 32; shift += 8) d += std::abs(int((a >> shift) & 0xFF) - int((b >> shift) & 0xFF));
        return d;
    };
    int votes[16] = {}, counted[16] = {}, cell = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            uint32_t a = off->second[y * 8 + x], b = lit->second[y * 8 + x];
            if (a == b) continue;
            uint32_t p = raw[(py + y) * SCREEN_W + px + x];
            int da = distance(p, a), db = distance(p, b), v = (db < da) - (da < db);
            int block = (y / 2) * 4 + x / 2;
            votes[block] += v;
            counted[block]++;
            cell += v;
        }
    uint32_t blocks = 0;
    for (int i = 0; i < 16; i++)
        if (counted[i] ? votes[i] > 0 : cell > 0) blocks |= 1u << i;
    return blocks;
}

void Pack2D::vote_segments(const Entry& e, int index, const uint32_t* raw, int px, int py, int* votes) {
    auto off = e.px_off.find(index), lit = e.px_lit.find(index);
    if (off == e.px_off.end() || lit == e.px_lit.end() || e.seg2d.empty()) return;
    auto distance = [](uint32_t a, uint32_t b) {
        int d = 0;
        for (int shift = 0; shift < 32; shift += 8) d += std::abs(int((a >> shift) & 0xFF) - int((b >> shift) & 0xFF));
        return d;
    };
    const int w = e.cw * 8, x0 = (index % e.cw) * 8, y0 = (index / e.cw) * 8;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            uint32_t a = off->second[y * 8 + x], b = lit->second[y * 8 + x];
            int s = e.seg2d[(y0 + y) * w + x0 + x];
            if (!s) continue;
            uint32_t p = raw[(py + y) * SCREEN_W + px + x];
            int da = distance(p, a), db = distance(p, b);
            if (a == b) {   // the same in both captures (a bar never lit in them): lit when something else is there
                if (da > 48) votes[s]++;
                continue;
            }
            votes[s] += (db < da) - (da < db);
        }
}

uint32_t Pack2D::take_slot(int entry, int kind) {
    // Pictures listed in this apply() or in the frames just before it may still
    // be drawn from their slot (frames in flight): those are never taken
    constexpr uint64_t KEEP_FRAMES = 4;
    int best = -1;
    for (int i = 0; i < int(slots_.size()); i++) {
        const SlotUse& u = slots_[i];
        if (u.entry < 0) {   // free
            best = i;
            break;
        }
        if (u.shown + KEEP_FRAMES <= frame_ && (best < 0 || u.shown < slots_[best].shown)) best = i;
    }
    if (best < 0) return 0;
    SlotUse& u = slots_[best];
    uint32_t slot = first_slot_ + uint32_t(best);
    if (u.entry >= 0) {   // the picture in it goes back to "not loaded"
        Entry& old = entries_[u.entry];
        (u.kind == 2 ? old.seg_slot : u.kind == 1 ? old.lit_slot : old.slot) = 0;
        if (!reuse_told_)
            printf("[2d] %s: more than %zu different pictures in this session, reusing the slots of the ones "
                   "not shown for longest\n", dir_.c_str(), slots_.size());
        reuse_told_ = true;
    }
    u = {entry, kind, frame_};
    return slot;
}

// Empty cells (nothing drawn) prove nothing: they are ignored in the references
uint64_t Pack2D::empty_cell_hash() {
    static const uint32_t zeros[64] = {};
    return fnv1a_words(zeros, 64);
}

int Pack2D::apply(uint32_t* target, int stride, const uint32_t* raw, const uint32_t* raw_top, uint32_t* text_cells,
                  bool below_3d, const uint32_t* under, uint32_t* under_dst, int under_stride) {
    if (under && !under_dst) {
        under_dst = target;
        under_stride = stride;
    }
    // The list is emptied once a frame (TileGen::compose) and filled by each Pack2D
    uint32_t* list = text_cells + TextHD::CELLS_Y * TextHD::ROW_TEXELS;
    int listed = int(list[0] >> 16), active = 0;
    if (entries_.empty()) return 0;
    frame_++;
    hash_cells(raw, hashes_);
    hash_cells(raw_top, hashes_top_);
    std::unordered_map<uint64_t, std::vector<int>> where;   // cell hash -> screen cells, built on demand
    struct Found {
        Entry* e;
        int ox, oy, matched, total;
    };
    std::vector<Found> found_list;
    for (Entry& e : entries_) {
        if (e.failed) continue;
        bool holds = true;
        for (const auto& [addr, value] : e.when) holds = holds && ram_reader_ && ram_reader_(addr) == value;
        if (!holds) continue;
        // On screen when enough of the reference cells are unchanged (half, or
        // its "match" line): first where it was captured, then wherever a few
        // of its cells appear
        int total = 0, ox = e.cx, oy = e.cy;
        int matched = matches(e, ox, oy, total);
        auto enough = [&] { return total > 0 && matched * 100 >= total * e.min_share; };
        bool found = enough();
        if (!found) {
            if (where.empty())
                for (int i = 0; i < CELLS_X * CELLS_Y; i++) {
                    where[hashes_[i]].push_back(i);
                    if (hashes_top_[i] != hashes_[i]) where[hashes_top_[i]].push_back(i);
                }
            int anchors = 0;
            for (int i = 0; i < e.cw * e.ch && anchors < 8 && !found; i++) {
                auto it = where.find(e.cells[i]);
                if (!e.cells[i] || it == where.end() || it->second.size() > 16) continue;   // skip plain cells
                anchors++;
                for (int pos : it->second) {
                    int cx = pos % CELLS_X - i % e.cw, cy = pos / CELLS_X - i / e.cw;
                    if (cx < 0 || cy < 0 || cx + e.cw > CELLS_X || cy + e.ch > CELLS_Y) continue;
                    matched = matches(e, cx, cy, total);
                    if (enough()) {
                        ox = cx;
                        oy = cy;
                        found = true;
                        break;
                    }
                }
            }
        }
        if (found) found_list.push_back({&e, ox, oy, matched, total});
    }
    for (const Found& f : found_list) {
        if (active == MAX_ACTIVE) break;
        // Replacements over the same cells are rival readings of the screen
        // (STAGE1 is half of STAGE2: same letters, another digit): only the
        // best match is shown, by share of its cells and then by how many
        bool beaten = false;
        for (const Found& g : found_list) {
            bool overlap = g.ox < f.ox + f.e->cw && f.ox < g.ox + g.e->cw && g.oy < f.oy + f.e->ch &&
                           f.oy < g.oy + g.e->ch;
            int64_t share = int64_t(g.matched) * f.total - int64_t(f.matched) * g.total;
            beaten |= &g != &f && overlap && (share > 0 || (share == 0 && g.matched > f.matched));
        }
        if (beaten) continue;
        Entry& e = *f.e;
        int ox = f.ox, oy = f.oy;
        const int index = int(&e - entries_.data());
        if (!e.slot) {
            RgbaImage img;
            if (!png_load(dir_ + "/" + e.name + ".png", img.w, img.h, img.rgba)) {
                fprintf(stderr, "[2d] could not read %s/%s.png\n", dir_.c_str(), e.name.c_str());
                e.failed = true;
                continue;
            }
            e.slot = take_slot(index, 0);
            if (!e.slot) continue;   // (the game's own picture this frame, tried again on the next)
            img.slot = e.slot;
            pending_.push_back(std::move(img));
        }
        mark_shown(e.slot);
        if (!e.lit.empty() && !e.lit_slot) {
            RgbaImage img;
            if (!png_load(dir_ + "/" + e.lit_name + ".png", img.w, img.h, img.rgba)) {
                fprintf(stderr, "[2d] could not read %s/%s.png\n", dir_.c_str(), e.lit_name.c_str());
                e.lit.clear();
            } else {
                e.lit_slot = take_slot(index, 1);   // (none: the game's cells this frame)
                img.slot = e.lit_slot;
                if (e.lit_slot) pending_.push_back(std::move(img));
            }
        }
        if (e.lit_slot) mark_shown(e.lit_slot);
        if (e.lit_slot && !e.seg_slot && (!e.seg_tried || !e.seg2d.empty())) {
            // the segment map, if there is one (read again when its slot was reused)
            RgbaImage img;
            std::string base = e.lit_name.substr(0, e.lit_name.size() - 4);
            bool first = !e.seg_tried;
            e.seg_tried = true;
            if (png_load(dir_ + "/" + base + "_seg.png", img.w, img.h, img.rgba) && img.w > 0 && img.h > 0) {
                if (first) {
                    const int w = e.cw * 8, h = e.ch * 8;
                    e.seg2d.assign(size_t(w) * h, 0);
                    for (int y = 0; y < h; y++)
                        for (int x = 0; x < w; x++) {
                            int sx = std::min(int((x + 0.5) * img.w / w), img.w - 1);
                            int sy = std::min(int((y + 0.5) * img.h / h), img.h - 1);
                            e.seg2d[size_t(y) * w + x] = img.rgba[(size_t(sy) * img.w + sx) * 4];
                        }
                    // the first segment in each cell (0: none)
                    e.seg_first.assign(size_t(e.cw) * e.ch, 0);
                    for (int y = 0; y < h; y++)
                        for (int x = 0; x < w; x++) {
                            uint8_t s = e.seg2d[size_t(y) * w + x], &first = e.seg_first[(y / 8) * e.cw + x / 8];
                            if (s && (!first || s < first)) first = s;
                        }
                }
                e.seg_slot = take_slot(index, 2);
                img.slot = e.seg_slot;
                if (e.seg_slot) pending_.push_back(std::move(img));
            }
        }
        if (e.seg_slot) mark_shown(e.seg_slot);
        // With a segment map: the last segment that most of its changing pixels
        // show lit, over all the cells that change between the states (the bar
        // fills in order, so all before it are lit too). Every cell with
        // segments is then drawn from it, not from its own match: the new
        // segments do not end where the cells do
        uint32_t level = 0;
        if (e.seg_slot) {
            int votes[256] = {};
            for (const auto& [index, px] : e.px_off)
                vote_segments(e, index, raw, (ox + index % e.cw) * 8, (oy + index / e.cw) * 8, votes);
            for (int s = 255; s > 0 && !level; s--)
                if (votes[s] > 0) level = uint32_t(s);
        }
        // Unchanged cells give way to the picture; changed ones stay on top
        for (int y = 0; y < e.ch; y++)
            for (int x = 0; x < e.cw; x++) {
                uint64_t ref = e.cells[y * e.cw + x];
                uint64_t lit = e.lit_slot ? e.lit[y * e.cw + x] : 0;   // (lit picture not loaded: the game's cells)
                bool on_lit = lit && same(oy + y, ox + x, lit);
                bool on_ref = ref && same(oy + y, ox + x, ref);
                uint32_t blocks = 0;    // (0: the whole cell)
                uint32_t segment = 0;   // the last lit segment (0: no segment map here)
                const int index = y * e.cw + x;
                bool between = lit && !on_lit && !on_ref && e.px_off.count(index);
                if (e.seg_slot && e.seg_first[index] && (on_lit || on_ref || between)) {
                    // lit up to `level`: the lit picture where its first segment is lit
                    segment = level;
                    on_lit = e.seg_first[index] <= level;
                    on_ref = true;
                } else if (between) {
                    // caught between the two states: each block as the closer one, never the game's cell
                    blocks = lit_blocks(e, index, raw, (ox + x) * 8, (oy + y) * 8);
                    on_lit = blocks != 0;
                    on_ref = true;
                    if (blocks == 0xFFFF) blocks = 0;
                }
                if (!e.cover && !on_lit && !on_ref) continue;
                for (int py = 0; py < 8; py++) {
                    memset(target + ((oy + y) * 8 + py) * stride + (ox + x) * 8, 0, 8 * 4);
                    if (!under) continue;
                    // the picture behind the old text, where the new one leaves it visible
                    const uint32_t* src = under + ((oy + y) * 8 + py) * SCREEN_W + (ox + x) * 8;
                    uint32_t* dst = under_dst + ((oy + y) * 8 + py) * under_stride + (ox + x) * 8;
                    for (int i = 0; i < 8; i++)
                        if (src[i]) dst[i] = src[i];
                }
                if (!below_3d) {
                    uint32_t* cell = text_cells + ((oy + y) * CELLS_X + ox + x) * TextHD::CELL_TEXELS;
                    memset(cell, 0, TextHD::CELL_TEXELS * 4);
                    // this cell shows the lit picture: all of it, or the 2x2-pixel blocks in G and B
                    if (on_lit) cell[1] = 0xFF000000u | e.lit_slot << 16 | blocks;
                    // and with a segment map: lit up to that segment (map's slot in R, segment in B)
                    if (on_lit && segment) cell[2] = 0xFF000000u | e.seg_slot << 16 | segment;
                }
            }
        if (1 + 2 * (listed + 1) >= TextHD::ROW_TEXELS) break;
        list[1 + 2 * listed] = uint32_t(oy + e.ch) << 24 | uint32_t(ox) << 16 | uint32_t(oy) << 8 | uint32_t(ox + e.cw);
        list[2 + 2 * listed] = e.slot << 16 | uint32_t(below_3d) << 8;
        listed++;
        active++;
        if (e.lit_slot && !below_3d && 1 + 2 * (listed + 1) < TextHD::ROW_TEXELS) {
            // the lit picture over the same box, only in its marked cells (B = 1)
            list[1 + 2 * listed] = list[2 * listed - 1];
            list[2 + 2 * listed] = e.lit_slot << 16 | 1u;
            listed++;
        }
    }
    list[0] = uint32_t(listed) << 16;
    return active;
}

std::vector<RgbaImage> Pack2D::take_new_images() {
    std::vector<RgbaImage> out;
    out.swap(pending_);
    return out;
}

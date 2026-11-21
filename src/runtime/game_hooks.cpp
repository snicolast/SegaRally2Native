// The port's own code in place of single instructions of the game's
// (tools/game_hooks.py, game_hooks.h).
#include "game_hooks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "options.h"
#include "ppc_rt.h"

extern Options g_opt;   // app.cpp
extern std::vector<uint8_t> g_vrom;   // runtime.cpp

// ---- SELECT COURSE with a fifth card, FOREST --------------------------------
// The ROM has a fifth stage, FOREST (course id 1, docs/game_ram.md), that no
// select reaches: SELECT COURSE's carousel was written for four cards. With
// FOREST on, its cursor, turns and drawing count five (the hooks in
// game_hooks.py) and the fifth card, after SNOWY, races FOREST. Practice
// only: the championship never shows SELECT COURSE. Never in ARCADE ORIGINAL.
namespace {
constexpr uint32_t CARD_FLAGS = 0x47D424;   // a word per card (the chosen one blinks); room for four
constexpr int FOREST_CARD = 4;
constexpr uint32_t FOREST_COURSE = 1;
uint32_t g_forest_flag = 0;                 // the fifth card's flag, kept here

int card_of(uint32_t ea) { return int(int32_t(ea - CARD_FLAGS) >> 2); }
}  // namespace

int sr2_cards() {
    return g_opt.forest && !g_opt.arcade ? 5 : 4;
}

// The four cards were a quarter turn apart: the side ones at 90 degrees, the
// back one alone. Five at 72 degrees on the same ellipse put the two at the
// back (144) against each other, and the front pair out further than the side
// ones were. Five: the radius times 1 + a (1 - cos) / 2 + b sin^2, 1 at the
// front; a 0.358, b -0.214 put the front pair (72) 7% in and the back pair
// (144) 25% out. SR2_CARD_SPREAD=a,b tries others without a build
double sr2_card_radius(double angle, double radius) {
    if (sr2_cards() == 4) return radius;
    static const double* ab = [] {
        static double v[2] = {0.358, -0.214};
        if (const char* e = getenv("SR2_CARD_SPREAD")) sscanf(e, "%lf,%lf", &v[0], &v[1]);
        return v;
    }();
    double c = std::cos(angle), u = (1 - c) / 2, s2 = 1 - c * c;
    return double(float(radius * (1 + ab[0] * u + ab[1] * s2)));
}

// FOREST's card: the DESERT card's models (708, lit 709, the first of each
// table) with its own pictures. The model number carries a mark (bit 28: the
// model table's offset, number << 4, leaves it out), and the VROM address
// drawn (sr2_model_address) is then a copy of the model in VROM nobody uses
// (from SR2_OWN_VROM: the 64 MB end in zeros from 0xFA6F40), so the renderer
// tells it apart from DESERT's (R3DSceneBuilder::own_texture, app.cpp: the
// picture and name band from res/forest/card.png)
static constexpr uint32_t OWN_MODEL = 0x10000000;   // on the model number

// A model's length in VROM words: its polygons up to the one marked last
// (bit 2 of word 1) or a zero word 6 (real3d_spec.md, as R3DSceneBuilder::model)
static size_t model_words(const uint32_t* v, size_t from, size_t total) {
    size_t p = from;
    for (int guard = 0; guard < 65536 && p + 7 <= total; guard++) {
        const uint32_t* h = v + p;
        if (h[6] == 0) break;
        int n = (h[0] & 0x40) ? 4 : 3, shared = 0;
        for (int i = 0; i < 4; i++) shared += (h[0] >> i) & 1;
        p += 7 + 4 * size_t(n > shared ? n - shared : 0);
        if (h[1] & 4) break;
    }
    return p - from;
}

// The copy of the model at addr in the free VROM (made once), addr itself
// when there is no room
static uint32_t own_copy(uint32_t addr) {
    static std::unordered_map<uint32_t, uint32_t> copies;
    static uint32_t next = SR2_OWN_VROM;
    auto it = copies.find(addr);
    if (it != copies.end()) return it->second;
    uint32_t copy = addr;
    size_t total = g_vrom.size() / 4;
    uint32_t* v = reinterpret_cast<uint32_t*>(g_vrom.data());
    size_t n = addr < total ? model_words(v, addr, total) : 0;
    bool room = n && next + n <= total;
    for (size_t i = 0; room && i < n; i++) room = v[next + i] == 0;
    if (room) {
        memcpy(v + next, v + addr, n * 4);
        copy = next;
        next += uint32_t(n);
        printf("[forest] card model %06x copied to %06x (%zu words)\n", addr, copy, n);
    } else {
        printf("[forest] no room in VROM at %06x for the card model %06x: DESERT's pictures\n", next, addr);
    }
    copies[addr] = copy;
    return copy;
}

uint32_t sr2_card_model(uint32_t table, uint32_t offset, bool) {
    if (offset >> 2 >= uint32_t(FOREST_CARD)) return rd32(table) | OWN_MODEL;
    return rd32(table + offset);
}

uint32_t sr2_model_address(uint32_t ea, uint32_t model) {
    uint32_t addr = rd32(ea);
    return model & OWN_MODEL ? own_copy(addr) : addr;
}

uint32_t sr2_card_flag_load(uint32_t ea) {
    return card_of(ea) == FOREST_CARD && sr2_cards() > FOREST_CARD ? g_forest_flag : rd32(ea);
}

void sr2_card_flag_store(uint32_t ea, uint32_t v) {
    int card = card_of(ea);
    if (card == FOREST_CARD && sr2_cards() > FOREST_CARD) {
        g_forest_flag = v;
        return;
    }
    wr32(ea, v);
}

// The loops that set all the flags on or off count four: the last word they
// write (card 0) goes to FOREST's too
void sr2_card_flag_store_all(uint32_t ea, uint32_t v) {
    wr32(ea, v);
    if (card_of(ea) == 0) g_forest_flag = v;
}

// The palettes are a copy of 0xE97CC (0, 1, 3, 2) on the stack, four words.
// FOREST: DESERT's for now
uint32_t sr2_card_palette(uint32_t table, uint32_t offset) {
    return rd32(offset >> 2 >= uint32_t(FOREST_CARD) ? table : table + offset);
}

// FOREST's practice times were never filled in: 999 s and no extensions
// (the clock filled for 15 s at the start, ticking, and never ran down).
// Written here in their place, in the RAM copy of the tables (pointers per
// mode at 0xE8348; rows per difficulty in the game's numbering: NORMAL,
// HARD, VERY HARD, VERY EASY, EASY), from FOREST's own championship times
// (13 s to the first checkpoint, 28 to the second, 32 to the line: two
// checkpoints a lap) as MOUNTAIN's practice is to its championship: 60 s at
// the start (80 SPECIAL), the first extension by difficulty, then a little
// more than the championship's, 12 s from the line to the first checkpoint
static void forest_practice_times() {
    constexpr uint32_t MODES = 0xE8348;   // per mode: ?, courses, words per row, table
    static const int first[5] = {31, 29, 24, 46, 36};
    for (int special = 0; special < 2; special++) {
        uint32_t mode = MODES + 16 * uint32_t(2 + special);   // practice, practice SPECIAL
        uint32_t courses = rd32(mode + 4), words = rd32(mode + 8), table = rd32(mode + 12);
        int laps = special ? 10 : 3;
        if (words < uint32_t(3 * laps + 1)) continue;
        for (uint32_t diff = 0; diff < 5; diff++) {
            uint32_t row = table + 4 * words * (diff * courses + FOREST_COURSE);
            uint32_t at = row;
            wr32(at, uint32_t((special ? 80 : 60) * 60)), at += 4;
            for (int lap = 0; lap < laps; lap++) {
                if (lap) wr32(at, 12 * 60), at += 4;   // the line, to the first checkpoint
                wr32(at, uint32_t((lap ? 30 : first[diff]) * 60)), at += 4;   // to the second
                if (lap + 1 < laps) wr32(at, 34 * 60), at += 4;               // to the line
            }
            wr32(at, ~0u);
        }
    }
}

uint32_t sr2_course_id(uint32_t table, uint32_t offset) {
    if (offset >> 2 != uint32_t(FOREST_CARD)) return rd32(table + offset);
    forest_practice_times();
    return FOREST_COURSE;
}

// The sounds of a course at the start of the race (table 0xF1674) and at the
// goal (0xF16D4), 12 bytes a course: FOREST's are -1 (never filled in: the
// sound board got 00 00 00 and rang for seconds). It takes MOUNTAIN's
uint32_t sr2_course_sound(uint32_t ea) {
    constexpr uint32_t ROW = 12, MOUNTAIN = 2;
    for (uint32_t table : {0xF1674u, 0xF16D4u})
        if (ea >= table + FOREST_COURSE * ROW && ea < table + (FOREST_COURSE + 1) * ROW)
            return rd32(ea + (MOUNTAIN - FOREST_COURSE) * ROW);
    return rd32(ea);
}

// ---- OBJECT DISTANCE -------------------------------------------------------
// The course's placed objects (table 0x497CCC, 24 bytes each: spectators,
// cameramen, flashes, cones, signs, tyre barriers, snowmen...) are put in the
// scene each frame by 0x5D9F4 only when 0x5D784 finds them between 20 m
// behind and 100 m ahead of the camera (0xF54CC, 0xF54D0) and inside the view
// with 5 m to spare; out of the race (attract, replay, podium) 0x5D888 takes
// the kind-4 ones (cameramen, moving crowd...) within 100 m (0xF54DC). So the
// crowd popped in 100 m ahead. OBJECT DISTANCE stands in for both 100s (the
// two lfs in game_hooks.py); 100 is the arcade's. At most 400 m: the scene's
// instance list has room for ~4000 nodes a frame, with no check in the game,
// and a start line already takes ~330 at 100 m
double sr2_object_distance() {
    return double(std::clamp(g_opt.object_distance, 100, 400));
}

// ---- SELECT CAR with all eight cars ----------------------------------------
// SELECT CAR's scene (set up by 0x3C10) has eight cars: the six in a V
// opening towards the camera, three a side, and the two hidden ones, the
// Toyota Celica (6) and the Lancia Delta (7), parked to one side (x -7 / -9)
// for the camera to slide over to. The arcade opened them with VIEW CHANGE +
// HAND BRAKE on SELECT CAR, and only once the cabinet had run 100 games and
// then 30 days (0x4BE60 starts the clock, 0x4BCF0 asks it); the port keeps no
// backup RAM from one launch to the next, so that never came. Here the eight
// stand in one V, four a side: the Delta at the far left, the Celica at the
// far right (where the arcade's wheel picked them), and the wheel picks among
// eight bands. The game's own drawing, lighting and race set-up take any of
// the eight (its tables have eight). SR2_CAR_SLOTS=x,y,z;... (eight, by car)
// tries other places without a build
namespace {
constexpr uint32_t SLOT_POS = 0x47D530, SLOT_ANGLES = 0x47D464, SLOT_BYTES = 12;
constexpr int PLACES = 8;
constexpr int CAR_AT[PLACES] = {7, 0, 1, 2, 3, 4, 5, 6};   // left to right
// The ROM's V (x, depth, height): 0 (-2.35, -1.15), 1 (-1.65, -0.075), 2
// (-1.0, 0.95) and the mirror; the four a side on the same lines, a little
// closer together
struct Place { float x, y, z; };
Place g_places[PLACES] = {{-2.55f, -1.45f, 1.5f}, {-2.0f, -0.6f, 1.5f}, {-1.45f, 0.25f, 1.5f}, {-0.9f, 1.1f, 1.5f},
                          {0.9f, 1.1f, 1.5f},     {1.45f, 0.25f, 1.5f}, {2.0f, -0.6f, 1.5f},  {2.55f, -1.45f, 1.5f}};
// The wheel's bands (the ADC, 0x80 the centre), four a side with a gap
// between: up to 0x1C, 0x1E-0x37, 0x39-0x52, 0x54-0x6F | 0x91-0xAC,
// 0xAE-0xC7, 0xC9-0xE2, from 0xE4. The centre keeps the car there is
constexpr int BAND_LO[PLACES] = {0x00, 0x1E, 0x39, 0x54, 0x91, 0xAE, 0xC9, 0xE4};
constexpr int BAND_HI[PLACES] = {0x1C, 0x37, 0x52, 0x6F, 0xAC, 0xC7, 0xE2, 0xFF};

int slot_of(uint32_t ea) { return int(int32_t(ea - SLOT_POS) / int32_t(SLOT_BYTES)); }
}  // namespace

bool sr2_select_eight() {
    return g_opt.hidden_cars;
}

void sr2_car_slots() {
    if (!sr2_select_eight()) return;
    static bool read_env = false;
    if (!read_env) {
        read_env = true;
        if (const char* e = getenv("SR2_CAR_SLOTS")) {   // by car: x,y,z;x,y,z;...
            for (int car = 0; car < PLACES && e && *e; car++) {
                Place p;
                if (sscanf(e, "%f,%f,%f", &p.x, &p.y, &p.z) == 3) g_places[sr2_select_place_of(car)] = p;
                e = strchr(e, ';');
                if (e) e++;
            }
        }
    }
    const uint32_t start_angle = rd32(SLOT_ANGLES);   // the set-up's first angle for all (-45: they turn in)
    for (int place = 0; place < PLACES; place++) {
        uint32_t car = uint32_t(CAR_AT[place]);
        const Place& p = g_places[place];
        wr32(SLOT_POS + SLOT_BYTES * car + 0, d2fbits(p.x));
        wr32(SLOT_POS + SLOT_BYTES * car + 4, d2fbits(p.y));
        wr32(SLOT_POS + SLOT_BYTES * car + 8, d2fbits(p.z));
        wr32(SLOT_ANGLES + SLOT_BYTES * car, start_angle);   // the pair's was 0: they turn in like the others
    }
}

// The pair comes in, turns and is drawn with the others by 0x40D0 (the
// count goes to eight); 0x4224, its own drawing at one side, draws it far off
double sr2_hidden_car_x(double x) {
    return sr2_select_eight() ? x + 1000.0 : x;
}

uint32_t sr2_select_band(uint32_t adc, uint32_t car) {
    if (!sr2_select_eight()) return 0;
    int v = int(adc & 0xFF);
    for (int place = 0; place < PLACES; place++)
        if (v >= BAND_LO[place] && v <= BAND_HI[place]) return uint32_t(CAR_AT[place]);
    return car;
}

int32_t sr2_car_goes_right(uint32_t ea, uint32_t slot2) {
    if (!sr2_select_eight()) return int32_t(ea) > int32_t(slot2) ? 1 : 0;
    return sr2_select_place_of(slot_of(ea)) >= PLACES / 2 ? 1 : 0;
}

int sr2_select_places() {
    return sr2_select_eight() ? PLACES : 6;
}

int sr2_select_car_at(int place) {
    return sr2_select_eight() ? CAR_AT[std::clamp(place, 0, PLACES - 1)] : std::clamp(place, 0, 5);
}

int sr2_select_place_of(int car) {
    if (!sr2_select_eight()) return car;
    for (int place = 0; place < PLACES; place++)
        if (CAR_AT[place] == car) return place;
    return 0;
}

int sr2_select_band_centre(int place) {
    static constexpr int SIX[6] = {0x20, 0x50, 0x65, 0x9A, 0xAF, 0xE0};   // the ROM's six bands (0x3F30)
    if (!sr2_select_eight()) return SIX[std::clamp(place, 0, 5)];
    place = std::clamp(place, 0, PLACES - 1);
    return (BAND_LO[place] + BAND_HI[place]) / 2;
}

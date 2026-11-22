"""C++ that tools/recomp.py puts in place of single instructions' translations,
by address. Each one calls src/runtime/game_hooks.cpp, so a change of the game
can be switched on and off while it runs, where a patch of the ROM could not.

Only plain instructions (no branches): the code after each one runs as the
game's own.
"""

# SELECT COURSE with a fifth card, FOREST: the stage the ROM has (course id 1)
# that no select reaches (docs/game_ram.md). The carousel was written for
# four cards: the count, the wrap of the cursor and the quarter turns between
# cards are constants in the code, and its tables have room for four. With
# sr2_cards() at 4 (FOREST off) every hook does what the instruction did.
#
#   cursor 0x1371C0, carousel angle 0x1371B8 (cards at 2 pi / cards apart)
#   0x47408 (step 5): the cards come in one by one up to the count (0x22436C)
#   0x5914...0x5FD0 (step 6): turning, the cursor read back from the angle
#   0x5FF4: the cards drawn, model by card (lit when its flag is on)
#   0x46AAC / 0x4835C: the race set-up, card -> course id (table 0xF1F20)

N = "sr2_cards()"
TURN = "double(6.28318548f / float(sr2_cards()))"   # 2 pi / cards: pi / 2 for four, as the ROM has

HOOKS = {}


def _set(addrs, code):
    for a in addrs:
        HOOKS[a] = code


# The count of cards the carousel comes in with (step 5): cmpwi r0, 3 / li r0, 4
_set([0x47420], [f"cmps(c, 1, int32_t(c.r[0]), {N} - 1);"])
_set([0x47430], [f"c.r[0] = uint32_t({N});"])

# The cursor read back from the angle: li rX, 4 (counted down from the last card)
_set([0x5CE8, 0x5DBC], [f"c.r[11] = uint32_t({N});"])
_set([0x5ECC], [f"c.r[10] = uint32_t({N});"])
# and its wrap: cmpwi rX, 4 (past the last card: the first) ...
_set([0x599C, 0x5D44, 0x5E20], [f"cmps(c, 1, int32_t(c.r[11]), {N});"])
_set([0x5F28], [f"cmps(c, 1, int32_t(c.r[10]), {N});"])
# ... and rlwinm r9, r9, 0, 30, 31 (before the first: the last; r9 is 0 or ~0)
_set([0x59BC, 0x5D64, 0x5E44, 0x5F50], [f"c.r[9] = c.r[9] & uint32_t({N} - 1);"])
# The angle a card is at: (cards - cursor) turns (subfic r0, r0 / r10, 4)
_set([0x56F8], [f"c.r[0] = addca(c, ~c.r[0], uint32_t({N}), 1);"])
_set([0x5F70], [f"c.r[0] = addca(c, ~c.r[10], uint32_t({N}), 1);"])
# One turn: lfs fX, -0x6814(r9) (0xE97EC, pi / 2)
_set([0x5728, 0x5FA0], [f"c.f[0].d = {TURN};"])
_set([0x5CD8, 0x5D24, 0x5DAC, 0x5DF8, 0x5EBC, 0x5F08], [f"c.f[12].d = {TURN};"])

# Drawing (0x5FF4): card i at 2 pi i x 0.25 (lfs f13, 0.25: 0xE9814) -> 1 / cards
_set([0x60C0], [f"c.f[13].d = double(1.0f / float({N}));"])
# and how far out from the middle (an ellipse, 1.4 / sqrt(cos^2 + 2.25 sin^2)):
# fdivs f3, f27, f3, f29 the card's angle. Five cards: the two at the back
# drawn further apart, the front ones where the four's side cards were
_set([0x61B4], ["c.f[3].d = sr2_card_radius(c.f[29].d, rs(c.f[27].d / c.f[3].d));"])
# and its model: lwzx r3, r9, r29 (lit, 0xE9790) / lwzx r3, r9, r30 (0xE9780)
_set([0x6210], ["c.r[3] = sr2_card_model(c.r[29], c.r[9], true);"])
_set([0x6224], ["c.r[3] = sr2_card_model(c.r[30], c.r[9], false);"])
# ... FOREST's, DESERT's with a mark, drawn by 0x3902C at the VROM's mirror
# (own pictures, game_hooks.cpp): lwzx r4, r9, r0 the model's address (table
# 0xA4BA8 + number << 4, r31 the number)
_set([0x3906C], ["c.r[4] = sr2_model_address(c.r[9] + c.r[0], c.r[31]);"])

# The cards' flags (0x47D424, a word each: the chosen card blinks) have room
# for four; the fifth's word is somebody else's (0x47D434)
_set([0x61F8], ["c.r[0] = sr2_card_flag_load(c.r[9] + c.r[28]);"])            # lwzx r0, r9, r28
_set([0x538C, 0x59F4, 0x47574, 0x46298], ["sr2_card_flag_store(c.r[11] + c.r[9], c.r[0]);"])   # stwx r0, r11, r9
_set([0x5964], ["sr2_card_flag_store_all(c.r[9] + c.r[0], c.r[11]);"])            # stwx r11, r9, r0 (all off)
_set([0x58FC], ["sr2_card_flag_store_all(c.r[11] + c.r[7], c.r[8]);"])            # stwx r8, r11, r7 (all off)
_set([0x512C], ["sr2_card_flag_store_all(c.r[7] + c.r[0], c.r[8]);"])             # stwx r8, r7, r0 (all on)

# The 2D picture under the cards, a palette per card (a copy of 0xE97CC on the
# stack: 0, 1, 3, 2): lwzx r6, rX, r0
_set([0x52E8], ["c.r[6] = sr2_card_palette(c.r[31], c.r[0]);"])
_set([0x540C, 0x57E0, 0x5AE8], ["c.r[6] = sr2_card_palette(c.r[9], c.r[0]);"])

# The race set-up: the card's course id, lwzx r0, r9, r0 (0xF1F20: 3, 2, 4, 5)
_set([0x46AF8, 0x48480], ["c.r[0] = sr2_course_id(c.r[9], c.r[0]);"])

# FOREST's sounds at the start of the race (0xF1674: now, later, frames
# between) and at the goal (0xF16D4) were never filled in (-1: the sound
# board gets 00 00 00 and rings a bell for seconds): MOUNTAIN's, the course
# most like it (game_hooks.cpp). 0x3E4B8 start, 0x3E54C / 0x3E588 goal
_set([0x3E50C], ["c.r[0] = sr2_course_sound(c.r[31] + 8);"])           # lwz r0, 8(r31)
_set([0x3E514], ["c.r[0] = sr2_course_sound(c.r[31] + 4);"])           # lwz r0, 4(r31)
_set([0x3E524], ["c.r[3] = sr2_course_sound(c.r[30] + c.r[29]);"])     # lwzx r3, r30, r29
_set([0x3E570], ["c.r[3] = sr2_course_sound(c.r[9] + c.r[0]);"])       # lwzx r3, r9, r0
_set([0x3E5B0], ["c.r[3] = sr2_course_sound(c.r[9] + 4);"])            # lwz r3, 4(r9)
# and the goal's sound as the final lap ends, from the pace notes (0x3E68C,
# FOREST's own notes in game_hooks.cpp)
_set([0x3E85C], ["c.r[3] = sr2_course_sound(c.r[9] + c.r[0]);"])       # lwzx r3, r9, r0

# OBJECT DISTANCE: the course's placed objects (spectators, cameramen, signs,
# cones...) are put in the scene only within 100 m ahead of the camera
# (0x5D784, lfs f0 of 0xF54D0); out of the race, the kind-4 ones within 100 m
# (0x5D888, lfs f0 of 0xF54DC). Both take the option (game_hooks.cpp)
_set([0x5D7EC, 0x5D96C], ["c.f[0].d = sr2_object_distance();"])

# SELECT CAR with all eight cars: the ROM's six in their V and the two hidden
# ones (6 Toyota Celica, 7 Lancia Delta), which the arcade kept to one side of
# the same scene (x -7 / -9) and slid the camera over to with VIEW CHANGE +
# HAND BRAKE once the cabinet had run 100 games and 30 days (0x4BCF0). With
# sr2_select_eight() the eight stand in one V, the wheel picks among eight
# bands, and the hidden pair comes in, turns and leaves with the others
# (game_hooks.cpp). Off, every hook does what the instruction did.
#
#   slots: position 0x47D530, angles 0x47D464 (12 bytes a car), lit 0x47D688
#   0x3C10 sets the slots up; 0x40D0 draws cars 0..count-1 (count 0x224340,
#   up to 6 as they come in); 0x4224 draws 6 and 7 (not while coming in)
#   0x47754 (step 9) picks the car: 0x3F30 from the wheel (six bands)
E8 = "sr2_select_eight()"
# The set-up's last store (stfs f12, 4(r10)), then the eight slots placed
_set([0x3E14], ["{ uint32_t ea = c.r[10] + 4; wr32(ea, d2fbits(c.f[12].d)); }", "sr2_car_slots();"])
# Coming in (step 8, 0x475xx): all lit (li r0, 5 / li r11, 0x14: six words) ...
_set([0x4761C], [f"c.r[0] = uint32_t({E8} ? 7 : 5);"])
_set([0x47620], [f"c.r[11] = uint32_t({E8} ? 0x1C : 0x14);"])
# ... one more car each time the last one has turned far enough, up to the
# count (cmpwi r0, 5 / li r0, 6 / cmpwi r0, 5), the last one's angle
# (lfs f0, 0x3C(r9): slot 5) and the loop that squares them all (li r0, 5 /
# li r11, 0x3C)
_set([0x476CC, 0x476E4], [f"cmps(c, 1, int32_t(c.r[0]), {E8} ? 7 : 5);"])
_set([0x476D8], [f"c.r[0] = uint32_t({E8} ? 8 : 6);"])
_set([0x476EC], [f"{{ uint32_t ea = c.r[9] + ({E8} ? 84 : 60); c.f[0].d = f2d(rd32(ea)); }}"])
_set([0x47710], [f"c.r[0] = uint32_t({E8} ? 7 : 5);"])
_set([0x47714], [f"c.r[11] = uint32_t({E8} ? 0x54 : 0x3C);"])
# 0x40D0 draws the count, now up to eight (the coming in, step 8, draws
# only with it); 0x4224, which draws the pair, puts it out of sight
# (lfs f1, 0(r31): the slot's x)
_set([0x4294], ["c.f[1].d = sr2_hidden_car_x(f2d(rd32(c.r[31])));"])
# The wheel: cmpwi r9, 0x44 (the first band) made false, so li r0, 0 is
# reached and takes the car of eight bands (or the car there was)
_set([0x3F44], [f"cmps(c, 1, {E8} ? 0 : int32_t(c.r[9]), {E8} ? 1 : 68);"])
_set([0x3F54], ["c.r[0] = sr2_select_band(c.r[9], c.r[10]);"])
# Leaving (step after the transmission, 0x479B4): the six fly off left and
# right (addi r0, r11, 0x3C: up to slot 5; cmpw r11, r10: slot 2 or less
# goes left): the eight, the Delta with the left ones
_set([0x47A38], [f"c.r[0] = c.r[11] + ({E8} ? 84 : 60);"])
_set([0x47A50], ["cmps(c, 1, sr2_car_goes_right(c.r[11], c.r[10]), 0);"])

# START!! (the picture before each race, mode table 0xF15F0: init 0x45204):
# the race HUD's lap-time box leaves lines of the A/A' line mask (0xF10F7000)
# picking A' for the left 128 pixels (set by 0x16D94 as START ends, put back
# by 0x16CC8 as the race ends), and the init only blanks layer A's names. From
# the second race on, a box of the left side showed layer B through (the
# NAME ENTRY backdrop still there). After lis r9, 0x4B (0x45228) the mask is
# filled back with A everywhere, as 0x41CB4 does
_set([0x45228], ["c.r[9] = 0x004B0000;", "sr2_start_mask();"])

// The port's own code in place of single instructions of the game's
// (tools/game_hooks.py lists them by address; recomp.py puts the calls in the
// generated code). Each one does what the instruction did unless its change
// is on.
#pragma once
#include <cstdint>

// SELECT COURSE (practice) with a fifth card, FOREST (game_hooks.cpp)
int sr2_cards();   // 4 as the arcade, 5 with FOREST
double sr2_card_radius(double angle, double radius);                  // how far out a card is drawn
uint32_t sr2_card_model(uint32_t table, uint32_t offset, bool lit);   // card offset / 4 drawn
uint32_t sr2_model_address(uint32_t ea, uint32_t model);              // its VROM address (FOREST's: a copy)
// VROM words from here on are the port's own models (copies of the game's, the
// FOREST card's), drawn with own pictures (R3DSceneBuilder::set_own_models)
constexpr uint32_t SR2_OWN_VROM = 0xFF0000;
uint32_t sr2_card_flag_load(uint32_t ea);                             // a card's flag (0x47D424 + 4 x card)
void sr2_card_flag_store(uint32_t ea, uint32_t v);
void sr2_card_flag_store_all(uint32_t ea, uint32_t v);                // the loops that set them all (four)
uint32_t sr2_card_palette(uint32_t table, uint32_t offset);           // the 2D picture's palette for a card
uint32_t sr2_course_id(uint32_t table, uint32_t offset);              // the race set-up: card -> course id
uint32_t sr2_course_sound(uint32_t ea);                               // a course's sound at the start / goal (FOREST: MOUNTAIN's)
void sr2_start_mask();                                                // START!!: the 2D A/A' line mask back to all A

// OBJECT DISTANCE (game_hooks.cpp): how far ahead of the camera, in metres,
// the course's placed objects are put in the scene (the ROM's 100.0 at 0xF54D0)
double sr2_object_distance();

// SELECT CAR with all eight cars (game_hooks.cpp): the six and the hidden
// pair (6 Toyota Celica, 7 Lancia Delta) in one V, picked by the wheel among
// eight bands. Off (--no-hidden-cars): the arcade's six
bool sr2_select_eight();
void sr2_car_slots();                                     // after the scene's set-up: the eight placed
double sr2_hidden_car_x(double x);                        // the pair's x where 0x4224 draws it (out of sight)
uint32_t sr2_select_band(uint32_t adc, uint32_t car);     // the car of the wheel's band (0 off: the first band's)
int32_t sr2_car_goes_right(uint32_t ea, uint32_t slot2);  // leaving: 1 a slot that flies off to the right
// SELECT CAR, left to right: how many cars, the car at a place, the place of
// a car, and the wheel's value in the middle of a place's band (app_input.cpp)
int sr2_select_places();
int sr2_select_car_at(int place);
int sr2_select_place_of(int car);
int sr2_select_band_centre(int place);

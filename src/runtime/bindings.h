// The keyboard's keys and the gamepads' buttons for the car (issue #189),
// chosen in OPTIONS > CONTROLS and kept in save/controls.ini. The menus keep
// their own keys (arrows, Enter, Esc; the d-pad, the stick, A and B), and so
// do the pause, the coin and start (Esc, 5 / 1; START, BACK), so nobody can
// lose the way back. Steering wheels have theirs in OPTIONS > WHEEL (wheel.h).
//
// Each action takes up to two inputs (the gamepad's accelerator: RT and A).
// An input belongs to one action: given to another, it leaves the first, and
// an action left with nothing gets the one it took from (they swap).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "wheel.h"   // WheelRow, WheelPrompt: the rows and the window are drawn as the WHEEL tab's

enum BindAct { ACT_STEER_LEFT, ACT_STEER_RIGHT, ACT_GAS, ACT_BRAKE, ACT_SHIFT_UP, ACT_SHIFT_DOWN, ACT_HANDBRAKE,
               ACT_VIEW, ACT_COUNT };
constexpr int BIND_SLOTS = 2;
// A gamepad's input: an SDL_GamepadButton, or one of the triggers (-1 = none)
constexpr int PAD_LT = 100, PAD_RT = 101;

// The inputs of an action (BIND_SLOTS of them, -1 = none): Windows virtual-key
// codes for the keyboard, PAD_ inputs for the gamepads
const int* bind_keys(int act);
const int* bind_pad(int act);
// The gamepad inputs held now on any pad (gamepad.cpp, every poll): the
// CONTROLS rows light up with them. Bit b: button b (0..31), 32: LT, 33: RT
void bind_pad_held(uint64_t mask);
constexpr uint64_t pad_bit(int input) { return input == PAD_LT ? 1ull << 32 : input == PAD_RT ? 1ull << 33 : 1ull << input; }

// OPTIONS > CONTROLS: DEVICE (the keyboard's or the gamepads'), then the rows
// of that device, one per action, then RESET TO DEFAULTS. Choosing an action
// listens for the next key or button (the window over the tab: bind_prompt),
// which becomes it. The device shown follows the one driving the menu
// (menu_pad) until DEVICE is changed by hand, and stays while one listens
bool bind_shows_pad(bool menu_pad);
std::vector<WheelRow> bind_rows(bool pad);
void bind_row_input(int row, bool pad);
void bind_row_clear(int row, bool pad);   // Delete / the pad's Y: the action has nothing
bool bind_listening();                    // waiting for a key or a button
bool bind_listening_pad();                // ... a gamepad's (else the keyboard's)
void bind_listen_cancel();                // Esc, START, BACK, or the time out
void bind_take_key(int vk);               // a key pressed while the keyboard listens
void bind_take_pad(int input);            // a button or a trigger while the gamepads listen
WheelPrompt bind_prompt();                // the window (also a moment after one is taken)

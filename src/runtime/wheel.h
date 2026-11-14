// Steering wheels and other joysticks that are not gamepads (SDL's joystick
// API, no SDL window): Logitech, Thrustmaster, Fanatec, arcade-style USB
// wheels... Written without a wheel at hand, so everything that differs from
// one wheel to another is in save/wheel.ini, written with the defaults the
// first time a wheel is plugged in:
//   - steering: one axis (0 by default), the part of its travel that gives
//     the full lock (the arcade wheel turns about 270 degrees, so a 900
//     degree wheel set to 900 in its driver wants about 0.3)
//   - pedals: one axis each, or one combined axis (accelerator one way, brake
//     the other). Their resting value is learnt when the wheel arrives (the
//     pedals let go), so it does not matter which way an axis runs; a 0 from
//     SDL waits for the pedal's first reading near an end of its axis, and
//     gas_rest / brake_rest in wheel.ini can give it by hand
//   - menus and the pause screen: the hat moves, two buttons accept and go
//     back, start resumes, the shift paddles change the settings tab
//   - buttons: shift up / down (the paddles), VIEW CHANGE, hand brake, coin,
//     start, and an H shifter's gears 1-4 when the wheel has one
//   - force feedback (SDL haptic): the game's own motor force as a constant
//     force, plus a centring spring, with their strengths
// SR2_WHEEL_LOG=1 prints every axis and button that moves, to find them.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum class PadKey;   // gamepad.h

struct WheelState {
    bool connected = false;
    std::string name;
    float steer = 0;                // -1 (left) .. 1 (right)
    float gas = 0, brake = 0;       // 0..1
    bool view = false, handbrake = false;     // held
    bool shift_up = false, shift_down = false, coin = false, start = false;   // pressed since the last poll
    int gear = -1;                  // H shifter: -1 none configured, 0 neutral, 1..4
    bool moved = false;             // something moved in the last poll (the wheel takes over)
};

// Called by Gamepads (gamepad.cpp), which owns SDL and its events
void wheels_init();                         // after SDL_Init: SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC too
void wheels_added(uint32_t joystick_id);    // SDL_EVENT_JOYSTICK_ADDED
void wheels_removed(uint32_t joystick_id);  // SDL_EVENT_JOYSTICK_REMOVED
bool wheel_is_ours(uint32_t joystick_id);   // a wheel: Gamepads leaves it alone even if SDL maps it as a gamepad
void wheels_poll(bool focused, bool menu, std::vector<PadKey>& keys);   // menu: PadKeys from the hat and buttons
void wheels_shutdown();

const WheelState& wheel_state();
// Force feedback from the game's drive board: `force` -1..1 (the motor, 0 =
// none; the sign as the board drives it), `shake` 0..1 (sudden changes)
void wheel_set_force(float force, float shake);

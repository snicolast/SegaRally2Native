// Sega Rally 2's own RAM: the addresses of the game state the port reads (and
// a few it writes), in one place. How each one was found, and what more is
// known about the structures, is in docs/game_ram.md. PowerPC addresses,
// 32-bit big-endian words unless said otherwise (rd32 / rd8 in runtime.h).
#pragma once
#include <cstdint>

namespace game_ram {

// The main loop's sequence (0x0002DD94): 0 attract, 1 a game (from the credit
// to GAME OVER), -1 at power on; the next entry, made current by the loop
constexpr uint32_t SEQUENCE = 0x22D618, SEQUENCE_NEXT = 0x22D61C;
// In a game: 1 through the selects and the races, 2 to 5 the podium, replay,
// ranking and GAME OVER screens
constexpr uint32_t PHASE = 0x22D624;
// The race: 0 before the start, 1 racing, 2 finished, 3 time up
constexpr uint32_t RACE_RESULT = 0x22D630;
// The credit / START screen's steps: 1 a credit taken, 4 START taken
constexpr uint32_t START_STEP = 0x22436C;

// The game structure
constexpr uint32_t GAME = 0x22CF14;
constexpr uint32_t G_MODE = GAME + 0x00;         // 0 championship, 2 practice
constexpr uint32_t G_ROUND = GAME + 0x34;        // the course of the race (course id + 6 in practice)
constexpr uint32_t G_LAPS_TOTAL = GAME + 0x3C;   // laps of the race
constexpr uint32_t G_CAR_STATUS = GAME + 0x6C;   // 1 from GO: racing
constexpr uint32_t G_CLOCK = GAME + 0x70;        // 2 while the clock counts
constexpr uint32_t G_LEFT = GAME + 0x74;         // time left, in frames
constexpr uint32_t G_GOAL = GAME + 0x7C;         // 1 from the frame the finish line is crossed
constexpr uint32_t G_STAGES = GAME + 0x90;       // stages cleared
constexpr uint32_t G_LAPS = GAME + 0x94;         // laps done
constexpr uint32_t G_CHECKPOINTS = GAME + 0x98;  // checkpoints passed on the lap
constexpr uint32_t G_PROGRESS = GAME + 0xA8;     // track points passed, 256 a lap
constexpr uint32_t G_RANK = GAME + 0x104;        // the rank, as the CLEARED card shows it
constexpr uint32_t G_TOTAL = GAME + 0x168;       // the championship's total time
constexpr uint32_t G_RACE_TIME = GAME + 0x16C;   // the race's time
constexpr uint32_t G_LAP_TIMES = GAME + 0x684;   // the lap times
constexpr uint32_t G_COMPLETE = GAME + 0x6F0;    // 1 once every stage of the game is cleared
constexpr uint32_t G_RANKING = GAME + 0x6F4;     // the place a finished game took in the records (-1 none)

// The selects' state (set up by 0x00046160)
constexpr uint32_t SELECT = 0x224320;
constexpr uint32_t SELECT_CAR = SELECT + 0x04;     // the SELECT CAR cursor (index into the model table at 0xF1F00)
constexpr uint32_t SELECT_COURSE = SELECT + 0x08;  // 0 desert, 1 mountain, 2 riviera, 3 snowy
constexpr uint32_t SELECT_MODE = SELECT + 0x0C;    // 0 championship, 1 practice
constexpr uint32_t SELECT_GEARS = SELECT + 0x14;   // 0 automatic, 1 manual
constexpr uint32_t SELECT_STEP = SELECT + 0x3C;    // 3 SELECT GAME ... 17 racing
// NAME ENTRY (select step 15, 0x00047EFC; its controls 0x00037BD4): the
// initials are the first 3 bytes of SELECT (0xFF empty, 0-25 A-Z, 26 '.'),
// and this is 3 once END is taken (or the time is up)
constexpr uint32_t NAME_DONE = SELECT + 0x64;

// The HUD and the cars
constexpr uint32_t HUD_POSITION = 0x13AB74;   // the player's position, 0 = 1st
constexpr uint32_t HUD_GEAR = 0x13AB80;
constexpr uint32_t CARS = 0x225A18;           // cars in the race
constexpr uint32_t VISIBLE_COUNT = 0x225A2C, VISIBLE_CARS = 0x225A30;   // the cars in view and their objects
constexpr uint32_t PLAYER_CAR = 0x2242B0;     // pointer to the player's car object
constexpr uint32_t RPM = 0x22F364, SPEED = 0x22F3A8;
constexpr uint32_t IN_AIR = 0x22F394;         // 1 while the player's car is in the air
constexpr uint32_t LAP_FRAMES = 0x1B0EB0;     // the current lap's time, in 1/60 s

}  // namespace game_ram

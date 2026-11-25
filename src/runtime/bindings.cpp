#include "bindings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>

namespace {

constexpr const char* INI_FILE = "save/controls.ini";
constexpr double SETTLE = 0.3;      // the key or button that chose the row is let go first
constexpr double LISTEN_FOR = 8.0;  // then the window gives up
constexpr double SHOW_DONE = 1.1;   // a key taken: shown a moment

struct ActInfo {
    const char* key;     // in controls.ini
    const char* label;   // the row
    const char* glyph;   // the window's picture: GAS / BRAKE the pedal, HAND the hand brake, else on a button
    bool on_pad;         // a gamepad steers with its stick: no rows for the steering
};
const ActInfo ACT[ACT_COUNT] = {
    {"steer_left", "STEER LEFT", "<", false},  {"steer_right", "STEER RIGHT", ">", false},
    {"accelerator", "ACCELERATOR", "GAS", true}, {"brake", "BRAKE", "BRAKE", true},
    {"shift_up", "SHIFT UP", "+", true},       {"shift_down", "SHIFT DOWN", "-", true},
    {"hand_brake", "HAND BRAKE", "HAND", true}, {"view", "VIEW CHANGE", "VIEW", true}};

const int KEY_DEFAULT[ACT_COUNT][BIND_SLOTS] = {{VK_LEFT, -1}, {VK_RIGHT, -1}, {VK_UP, -1},    {VK_DOWN, -1},
                                                {'A', -1},     {'Z', -1},      {VK_SPACE, -1}, {'V', -1}};
const int PAD_DEFAULT[ACT_COUNT][BIND_SLOTS] = {
    {-1, -1}, {-1, -1}, {PAD_RT, SDL_GAMEPAD_BUTTON_SOUTH}, {PAD_LT, SDL_GAMEPAD_BUTTON_EAST},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, -1}, {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, -1},
    {SDL_GAMEPAD_BUTTON_WEST, -1}, {SDL_GAMEPAD_BUTTON_NORTH, -1}};

// The game's own keys and buttons, not given to an action: the pause, the
// coin and start, the arcade's test and service, the snapshot, full screen,
// the picture capture; on a pad the coin, START and the guide button
bool reserved_key(int vk) {
    switch (vk) {
    case VK_ESCAPE: case VK_F2: case VK_F3: case VK_F9: case VK_F11: case VK_F12:
    case '1': case '2': case '5': case '6': return true;
    default: return false;
    }
}
bool reserved_pad(int b) {
    return b == SDL_GAMEPAD_BUTTON_BACK || b == SDL_GAMEPAD_BUTTON_START || b == SDL_GAMEPAD_BUTTON_GUIDE;
}

struct Layout {
    int key[ACT_COUNT][BIND_SLOTS];
    int pad[ACT_COUNT][BIND_SLOTS];
};
Layout g_lay;
bool g_loaded = false;
uint64_t g_pad_held = 0;
bool g_device_toggle = false;   // DEVICE chosen: bind_shows_pad shows the other one

// ---- Names: controls.ini's, and the rows' ----

struct KeyName { int vk; const char* name; };
const KeyName KEY_NAMES[] = {
    {VK_LEFT, "LEFT"}, {VK_RIGHT, "RIGHT"}, {VK_UP, "UP"}, {VK_DOWN, "DOWN"}, {VK_SPACE, "SPACE"},
    {VK_RETURN, "ENTER"}, {VK_TAB, "TAB"}, {VK_BACK, "BACKSPACE"}, {VK_SHIFT, "SHIFT"}, {VK_CONTROL, "CTRL"},
    {VK_LSHIFT, "LEFT SHIFT"}, {VK_RSHIFT, "RIGHT SHIFT"}, {VK_LCONTROL, "LEFT CTRL"}, {VK_RCONTROL, "RIGHT CTRL"},
    {VK_CAPITAL, "CAPS LOCK"}, {VK_INSERT, "INSERT"}, {VK_DELETE, "DELETE"}, {VK_HOME, "HOME"}, {VK_END, "END"},
    {VK_PRIOR, "PAGE UP"}, {VK_NEXT, "PAGE DOWN"}, {VK_MULTIPLY, "NUM *"}, {VK_ADD, "NUM +"},
    {VK_SUBTRACT, "NUM -"}, {VK_DECIMAL, "NUM ."}, {VK_DIVIDE, "NUM /"}, {VK_OEM_1, "OEM 1"}, {VK_OEM_PLUS, "OEM PLUS"},
    {VK_OEM_COMMA, "OEM COMMA"}, {VK_OEM_MINUS, "OEM MINUS"}, {VK_OEM_PERIOD, "OEM PERIOD"}, {VK_OEM_2, "OEM 2"},
    {VK_OEM_3, "OEM 3"}, {VK_OEM_4, "OEM 4"}, {VK_OEM_5, "OEM 5"}, {VK_OEM_6, "OEM 6"}, {VK_OEM_7, "OEM 7"},
    {VK_OEM_102, "OEM 102"}};

// controls.ini's name of a key: a letter, a digit, NUM 0, F5, a word, or its code
std::string key_ini_name(int vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, char(vk));
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "NUM " + std::to_string(vk - VK_NUMPAD0);
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    for (const KeyName& k : KEY_NAMES)
        if (k.vk == vk) return k.name;
    char b[8];
    snprintf(b, sizeof b, "0x%02X", vk & 0xFF);
    return b;
}

int key_from_ini(const std::string& s) {
    if (s.size() == 1 && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= '0' && s[0] <= '9'))) return s[0];
    if (s.size() == 1 && s[0] >= 'a' && s[0] <= 'z') return s[0] - 'a' + 'A';
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) return int(strtol(s.c_str() + 2, nullptr, 16));
    for (int vk = 1; vk < 256; vk++)
        if (_stricmp(key_ini_name(vk).c_str(), s.c_str()) == 0) return vk;
    return -1;
}

// The row's name of a key: the keyboard's own for the signs (a Spanish one's Ñ...)
std::string key_label(int vk) {
    if (vk >= VK_OEM_1 && vk <= VK_OEM_102 && vk != VK_OEM_PLUS && vk != VK_OEM_MINUS) {
        UINT sc = MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC);
        wchar_t w[32] = {};
        if (sc && GetKeyNameTextW(LONG(sc << 16), w, 32) > 0) {
            std::string out;
            for (const wchar_t* c = w; *c; c++) {
                wchar_t u = wchar_t(uintptr_t(CharUpperW(LPWSTR(uintptr_t(*c)))));
                out += u < 256 ? char(u) : '?';   // (Latin-1, as the menus' text)
            }
            if (!out.empty()) return out;
        }
    }
    if (vk == VK_OEM_PLUS) return "+";
    if (vk == VK_OEM_MINUS) return "-";
    std::string n = key_ini_name(vk);
    return n.rfind("0x", 0) == 0 ? "KEY " + n : n;
}

// The gamepad's: as on an Xbox pad (the buttons are positions, the same on every pad)
std::string pad_label(int b) {
    switch (b) {
    case PAD_LT: return "LT";
    case PAD_RT: return "RT";
    case SDL_GAMEPAD_BUTTON_SOUTH: return "A";
    case SDL_GAMEPAD_BUTTON_EAST: return "B";
    case SDL_GAMEPAD_BUTTON_WEST: return "X";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Y";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "LS";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "RS";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-PAD UP";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-PAD DOWN";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-PAD LEFT";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-PAD RIGHT";
    case SDL_GAMEPAD_BUTTON_MISC1: return "SHARE";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return "P1";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return "P2";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return "P3";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return "P4";
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "TOUCHPAD";
    default: return "BUTTON " + std::to_string(b);
    }
}

// controls.ini's: SDL's names (a, rightshoulder, lefttrigger...)
std::string pad_ini_name(int b) {
    if (b == PAD_LT) return SDL_GetGamepadStringForAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    if (b == PAD_RT) return SDL_GetGamepadStringForAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    const char* s = SDL_GetGamepadStringForButton(SDL_GamepadButton(b));
    return s ? s : std::to_string(b);
}

int pad_from_ini(const std::string& s) {
    SDL_GamepadAxis a = SDL_GetGamepadAxisFromString(s.c_str());
    if (a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) return PAD_LT;
    if (a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) return PAD_RT;
    SDL_GamepadButton b = SDL_GetGamepadButtonFromString(s.c_str());
    if (b != SDL_GAMEPAD_BUTTON_INVALID) return int(b);
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++)   // (the rows' names too: RB, LT...)
        if (_stricmp(pad_label(i).c_str(), s.c_str()) == 0) return i;
    if (_stricmp(s.c_str(), "LT") == 0) return PAD_LT;
    if (_stricmp(s.c_str(), "RT") == 0) return PAD_RT;
    return -1;
}

std::string names(const int* in, bool pad, const char* sep, bool label) {
    std::string out;
    for (int s = 0; s < BIND_SLOTS; s++) {
        if (in[s] < 0) continue;
        if (!out.empty()) out += sep;
        out += label ? (pad ? pad_label(in[s]) : key_label(in[s])) : pad ? pad_ini_name(in[s]) : key_ini_name(in[s]);
    }
    return out;
}

// ---- controls.ini ----

std::string trimmed(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

void defaults(Layout& l, bool pad) {
    for (int a = 0; a < ACT_COUNT; a++)
        for (int s = 0; s < BIND_SLOTS; s++) (pad ? l.pad : l.key)[a][s] = (pad ? PAD_DEFAULT : KEY_DEFAULT)[a][s];
}

void load() {
    g_loaded = true;
    defaults(g_lay, false);
    defaults(g_lay, true);
    FILE* f = fopen(INI_FILE, "r");
    if (!f) return;
    int section = -1;   // 0 [keyboard], 1 [gamepad]
    char line[256];
    while (fgets(line, sizeof line, f)) {
        std::string l = trimmed(line);
        if (l.empty() || l[0] == ';' || l[0] == '#') continue;
        if (l[0] == '[') {
            section = _stricmp(l.c_str(), "[keyboard]") == 0 ? 0 : _stricmp(l.c_str(), "[gamepad]") == 0 ? 1 : -1;
            continue;
        }
        size_t eq = l.find('=');
        if (section < 0 || eq == std::string::npos) continue;
        std::string k = trimmed(l.substr(0, eq)), v = trimmed(l.substr(eq + 1));
        for (int a = 0; a < ACT_COUNT; a++) {
            if (_stricmp(k.c_str(), ACT[a].key) != 0) continue;
            int* in = section ? g_lay.pad[a] : g_lay.key[a];
            std::fill(in, in + BIND_SLOTS, -1);   // (empty: nothing)
            int s = 0;
            size_t from = 0;
            while (from <= v.size() && s < BIND_SLOTS) {
                size_t comma = v.find(',', from);
                std::string name = trimmed(v.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
                int code = name.empty() ? -1 : section ? pad_from_ini(name) : key_from_ini(name);
                if (code >= 0 && !(section ? reserved_pad(code) : reserved_key(code))) in[s++] = code;
                if (comma == std::string::npos) break;
                from = comma + 1;
            }
        }
    }
    fclose(f);
}

void save() {
    CreateDirectoryA("save", nullptr);
    FILE* f = fopen(INI_FILE, "w");
    if (!f) return;
    fprintf(f,
            "; Sega Rally 2 Native: the keyboard's keys and the gamepads' buttons for the car.\n"
            "; OPTIONS > CONTROLS sets them: choose an action and press the key or button.\n"
            "; They can be edited here too: up to two for an action, with a comma between\n"
            "; them; empty for none; a line taken out goes back to its default.\n"
            "; Kept for the game whatever is here: the menus' keys, Esc (pause), 5 / 1 (coin,\n"
            "; start; 6 / 2 the second player's), F2 / F3 (test, service), F9, F11 and F12;\n"
            "; on a gamepad BACK (coin), START (pause) and the guide button. The gamepads\n"
            "; steer with the left stick. Steering wheels: OPTIONS > WHEEL (wheel.ini).\n"
            "\n[keyboard]\n");
    for (int a = 0; a < ACT_COUNT; a++) fprintf(f, "%s = %s\n", ACT[a].key, names(g_lay.key[a], false, ", ", false).c_str());
    fprintf(f, "\n; gamepad names: a b x y leftshoulder rightshoulder lefttrigger righttrigger\n"
               "; leftstick rightstick dpup dpdown dpleft dpright misc1 paddle1..paddle4 touchpad\n"
               "[gamepad]\n");
    for (int a = 0; a < ACT_COUNT; a++)
        if (ACT[a].on_pad) fprintf(f, "%s = %s\n", ACT[a].key, names(g_lay.pad[a], true, ", ", false).c_str());
    fclose(f);
}

Layout& lay() {
    if (!g_loaded) load();
    return g_lay;
}

// ---- The rows and the window ----

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// The rows' actions: the keyboard's all, the gamepad's without the steering
std::vector<int> acts(bool pad) {
    std::vector<int> v;
    for (int a = 0; a < ACT_COUNT; a++)
        if (!pad || ACT[a].on_pad) v.push_back(a);
    return v;
}

struct Listen {
    bool on = false, pad = false;
    int act = -1;
    double since = 0;
    std::string reject;               // a key the game keeps: said in the window, still listening
    std::string done, note;           // just taken, and what it took from
    double done_at = -1;
} g_l;

bool is_default(bool pad) {
    const Layout& l = lay();
    for (int a = 0; a < ACT_COUNT; a++)
        for (int s = 0; s < BIND_SLOTS; s++)
            if ((pad ? l.pad : l.key)[a][s] != (pad ? PAD_DEFAULT : KEY_DEFAULT)[a][s]) return false;
    return true;
}

bool held_now(int act, bool pad) {
    const int* in = pad ? lay().pad[act] : lay().key[act];
    for (int s = 0; s < BIND_SLOTS; s++) {
        if (in[s] < 0) continue;
        if (pad ? (g_pad_held & pad_bit(in[s])) != 0 : (GetAsyncKeyState(in[s]) & 0x8000) != 0) return true;
    }
    return false;
}

// Gives `input` to `act` alone. It leaves any other action it was on; one left
// with nothing gets what `act` had (they swap). Returns what to say about it
std::string assign(bool pad, int act, int input) {
    Layout& l = lay();
    int(*t)[BIND_SLOTS] = pad ? l.pad : l.key;
    int old[BIND_SLOTS];
    std::copy(t[act], t[act] + BIND_SLOTS, old);
    std::string note;
    for (int a = 0; a < ACT_COUNT; a++) {
        if (a == act) continue;
        bool had = false;
        for (int s = 0; s < BIND_SLOTS; s++)
            if (t[a][s] == input) t[a][s] = -1, had = true;
        if (!had) continue;
        std::sort(t[a], t[a] + BIND_SLOTS, [](int x, int y) { return (x >= 0) > (y >= 0); });   // what is left first
        std::string what = pad ? pad_label(input) : key_label(input);
        if (t[a][0] < 0) {   // left with nothing: it takes the old ones (but not `input`)
            int s = 0;
            for (int o : old)
                if (o >= 0 && o != input) t[a][s++] = o;
            note = what + " was " + ACT[a].label + "'s: " + ACT[a].label + " is now " +
                   (t[a][0] >= 0 ? names(t[a], pad, " / ", true) : std::string("NONE"));
        } else {
            note = what + " was also " + ACT[a].label + "'s: " + ACT[a].label + " keeps " + names(t[a], pad, " / ", true);
        }
    }
    t[act][0] = input;
    for (int s = 1; s < BIND_SLOTS; s++) t[act][s] = -1;
    return note;
}

void take(int input) {
    if (!g_l.on || g_l.done_at >= 0 || now_s() - g_l.since < SETTLE) return;
    bool pad = g_l.pad;
    if (pad ? reserved_pad(input) : reserved_key(input)) {
        g_l.reject = (pad ? pad_label(input) : key_label(input)) + " is kept for the game: choose another";
        return;
    }
    std::string note = assign(pad, g_l.act, input);
    save();
    g_l.done = std::string(ACT[g_l.act].label) + ": " + (pad ? pad_label(input) : key_label(input));
    g_l.note = note;
    g_l.done_at = now_s();
    printf("[controls] %s %s = %s%s%s\n", pad ? "gamepad" : "keyboard", ACT[g_l.act].key,
           names(pad ? lay().pad[g_l.act] : lay().key[g_l.act], pad, ", ", false).c_str(), note.empty() ? "" : "; ",
           note.c_str());
}

// The window's time is up, or the key taken was shown long enough
void expire() {
    if (!g_l.on) return;
    double now = now_s();
    if (g_l.done_at >= 0 ? now - g_l.done_at >= SHOW_DONE : now - g_l.since >= LISTEN_FOR) g_l = Listen{};
}

}  // namespace

const int* bind_keys(int act) { return lay().key[std::clamp(act, 0, ACT_COUNT - 1)]; }
const int* bind_pad(int act) { return lay().pad[std::clamp(act, 0, ACT_COUNT - 1)]; }
void bind_pad_held(uint64_t mask) { g_pad_held = mask; }

bool bind_shows_pad(bool menu_pad) {
    static int choice = -1;   // DEVICE chosen by hand: 0 the keyboard, 1 the gamepads (-1: as the menu)
    static bool menu_pad_was = false;
    if (menu_pad != menu_pad_was) choice = -1;   // the other device drives the menu now: its rows
    menu_pad_was = menu_pad;
    if (g_l.on) choice = g_l.pad;   // (listening: that device's)
    if (g_device_toggle) {
        choice = choice >= 0 ? !choice : !menu_pad;
        g_device_toggle = false;
    }
    return choice >= 0 ? choice == 1 : menu_pad;
}

std::vector<WheelRow> bind_rows(bool pad) {
    expire();
    std::vector<WheelRow> rows;
    const std::string group = "CAR CONTROLS";
    WheelRow dev;
    dev.group = group;
    dev.label = "DEVICE";
    dev.value = pad ? "GAMEPAD" : "KEYBOARD";
    dev.hint = pad ? "The gamepads' buttons below (one layout for every pad). It follows what you use in the menu; "
                     "Left / Right show the keyboard's keys"
                   : "The keyboard's keys below. It follows what you use in the menu; Left / Right show the gamepads' buttons";
    rows.push_back(dev);
    for (int a : acts(pad)) {
        WheelRow r;
        r.group = group;
        r.label = ACT[a].label;
        const int* in = pad ? lay().pad[a] : lay().key[a];
        bool any = in[0] >= 0 || in[1] >= 0;
        r.value = any ? names(in, pad, " / ", true) : "NONE";
        r.hint = std::string(any ? "" : "Not set. ") + "Choose it and press the " + (pad ? "button or trigger" : "key") +
                 " you want for it" + (any ? pad ? "; Y clears it" : "; Delete clears it" : "") +
                 ". One taken from another action leaves it there; if that one is left with nothing they swap";
        if (pad && (a == ACT_GAS || a == ACT_BRAKE))
            r.hint += ". A trigger gives the pedal's travel, a button all of it";
        r.meter = 3;
        r.live = held_now(a, pad) ? 1.0f : 0.0f;
        rows.push_back(r);
    }
    WheelRow reset;
    reset.group = group;
    reset.label = "RESET TO DEFAULTS";
    reset.value = is_default(pad) ? "DEFAULT" : "CHANGED";
    reset.hint = pad ? "Choose it to put back the original buttons: RT / A accelerate, LT / B brake, RB / LB shift, "
                       "X the hand brake, Y VIEW CHANGE"
                     : "Choose it to put back the original keys: the arrows steer, accelerate and brake, A / Z shift, "
                       "Space the hand brake, V VIEW CHANGE";
    rows.push_back(reset);
    return rows;
}

void bind_row_input(int row, bool pad) {
    if (row == 0) {   // DEVICE: the other one's rows
        g_device_toggle = true;
        return;
    }
    row--;
    std::vector<int> a = acts(pad);
    if (row < 0 || row > int(a.size())) return;
    if (row == int(a.size())) {   // RESET TO DEFAULTS
        defaults(lay(), pad);
        save();
        printf("[controls] %s: the defaults\n", pad ? "gamepad" : "keyboard");
        return;
    }
    g_l = Listen{};
    g_l.on = true;
    g_l.pad = pad;
    g_l.act = a[size_t(row)];
    g_l.since = now_s();
}

void bind_row_clear(int row, bool pad) {
    row--;   // (after DEVICE)
    std::vector<int> a = acts(pad);
    if (row < 0 || row >= int(a.size())) return;
    int* in = pad ? lay().pad[a[size_t(row)]] : lay().key[a[size_t(row)]];
    std::fill(in, in + BIND_SLOTS, -1);
    save();
    printf("[controls] %s %s cleared\n", pad ? "gamepad" : "keyboard", ACT[a[size_t(row)]].key);
}

bool bind_listening() {
    expire();
    return g_l.on && g_l.done_at < 0;
}
bool bind_listening_pad() { return bind_listening() && g_l.pad; }
void bind_listen_cancel() { g_l = Listen{}; }
void bind_take_key(int vk) {
    if (g_l.on && !g_l.pad) take(vk);
}
void bind_take_pad(int input) {
    if (g_l.on && g_l.pad) take(input);
}

WheelPrompt bind_prompt() {
    expire();
    WheelPrompt p;
    if (!g_l.on) return p;
    const ActInfo& a = ACT[g_l.act];
    const bool pad = g_l.pad;
    p.on = true;
    p.kicker = pad ? "GAMEPAD" : "KEYBOARD";
    p.title = a.label;
    p.glyph = a.glyph;
    p.icon = g_l.act == ACT_GAS || g_l.act == ACT_BRAKE ? WheelPrompt::PEDAL
             : g_l.act == ACT_HANDBRAKE ? WheelPrompt::HAND_BRAKE : WheelPrompt::BUTTON;
    p.action = pad ? "PRESS A BUTTON" : "PRESS A KEY";
    const int* in = pad ? lay().pad[g_l.act] : lay().key[g_l.act];
    std::string now = in[0] >= 0 || in[1] >= 0 ? names(in, pad, " / ", true) : "NONE";
    p.hint = "Now: " + now + ".  " +
             (pad ? std::string("Any button or trigger. START or BACK cancels: they stay the pause and the coin")
                  : std::string("Esc cancels. Esc, 1, 2, 5, 6, F2, F3, F9, F11 and F12 stay the game's"));
    p.moving = !g_l.reject.empty() ? g_l.reject : pad ? "WAITING FOR A BUTTON..." : "WAITING FOR A KEY...";
    p.cancel_button = pad ? "MENU" : "";
    if (g_l.done_at >= 0) {
        p.icon = WheelPrompt::DONE;
        p.done = g_l.done;
        p.note = g_l.note;
        p.done_age = float(now_s() - g_l.done_at);
    } else {
        p.time_left = float(std::clamp(1.0 - (now_s() - g_l.since) / LISTEN_FOR, 0.0, 1.0));
    }
    return p;
}

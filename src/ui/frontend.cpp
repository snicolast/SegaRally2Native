#include "frontend.h"


#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <ctime>
#include <initializer_list>
#include <thread>

#include <shellapi.h>
#include <winhttp.h>

#include <wincodec.h>

namespace {

// Star Wars: the title's yellow (bright at the top, deeper below) with a dark
// outline, cool blue-white letters, and lightsaber blue for the lines and the
// glow behind the selected entry
constexpr uint32_t GOLD_TOP = 0xFFF06A, GOLD_BOTTOM = 0xF5C400, EDGE = 0x020308;
constexpr uint32_t PALE_TOP = 0xEAF2FF, PALE_BOTTOM = 0xAFC2DA, DIM = 0x56606E;
constexpr uint32_t HINT = 0xC4CAD2, FOOT = 0x7A8088;   // Sega Rally 2: neutral greys (the mock-up)
constexpr uint32_t SABER = 0xC8241E, PANEL = 0x040914;   // Sega Rally 2: the accent is the logo's red
constexpr uint32_t WHITE_TOP = 0xFFFFFF, WHITE_BOTTOM = 0xD8E6F6;   // the selected entry of the side menu

// UPDATE AVAILABLE: news.txt on the public repository's main branch (news.h)
constexpr const wchar_t* NEWS_HOST = L"raw.githubusercontent.com";
constexpr const wchar_t* NEWS_PATH = L"/snicolast/SegaRally2Native/main/news.txt";
constexpr const wchar_t* GAME_URL = L"https://github.com/snicolast/SegaRally2Native";

double now_seconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

float chan(uint32_t rgb, int shift) { return float((rgb >> shift) & 0xFF) / 255.0f; }

uint32_t mix(uint32_t a, uint32_t b, float t) {
    uint32_t r = 0;
    for (int s = 0; s <= 16; s += 8) {
        float v = chan(a, s) + (chan(b, s) - chan(a, s)) * t;
        r |= uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f) << s;
    }
    return r;
}

std::vector<char> read_file(const std::string& path) {
    std::vector<char> data;
    if (FILE* f = fopen(path.c_str(), "rb")) {
        fseek(f, 0, SEEK_END);
        data.resize(size_t(ftell(f)));
        fseek(f, 0, SEEK_SET);
        if (fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
        fclose(f);
    }
    return data;
}

std::wstring widen(const std::string& s) {   // UTF-8
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(std::max(n, 0)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4   // Windows 8.1 and later
#endif

// GET https://host/path into `body` (at most 256 KB); false on any failure.
// A few seconds at most when there is no connection
bool https_get(const wchar_t* host, const wchar_t* path, std::string& body) {
    body.clear();
    const wchar_t* agent = L"SegaRally2Native";
    HINTERNET session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 4000, 4000, 4000, 4000);
    HINTERNET connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                : nullptr;
    DWORD status = 0, size = sizeof(status);
    bool ok = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr) &&
              WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                  &status, &size, WINHTTP_NO_HEADER_INDEX) &&
              status == 200;
    while (ok) {
        DWORD avail = 0, got = 0;
        if (!WinHttpQueryDataAvailable(request, &avail)) ok = false;
        if (!ok || avail == 0) break;
        size_t at = body.size();
        if (at + avail > 256 * 1024) ok = false;
        else {
            body.resize(at + avail);
            ok = WinHttpReadData(request, body.data() + at, avail, &got) != FALSE;
            body.resize(at + got);
        }
    }
    for (HINTERNET h : {request, connect, session})
        if (h) WinHttpCloseHandle(h);
    if (!ok) body.clear();
    return ok;
}

// Frontend::Family: the font's family name (as Windows knows it) and weight
const wchar_t* const FACES[6] = {L"Saira ExtraCondensed ExCond", L"Barlow Condensed", L"Share Tech Mono",
                                 L"Barlow Condensed SemiBold", L"Barlow Condensed Medium", L"Barlow"};
const int WEIGHTS[6] = {FW_BOLD, FW_BOLD, FW_NORMAL, FW_SEMIBOLD, FW_MEDIUM, FW_NORMAL};

}  // namespace

// ---- Settings file ----

namespace {
// What each QUALITY level sets (the other options are left as they are).
// Measured on an RTX 5070 Ti at 1440p, the costly ones are the traced sun
// shadows (in the 3D pass), the ambient occlusion and the anti-aliasing
struct QualityLevel {
    int ray_tracing;   // 0 off, 1 sun shadows, 2 also ambient occlusion
    bool ao, shadows;
    int bloom;
    bool msaa;         // MSAA 4x, or off (DLAA, picked by hand, is left alone)
};
const QualityLevel QUALITY_LEVELS[4] = {
    {0, false, false, 1, false},   // LOW: laptops and integrated graphics
    {0, true, true, 1, true},      // MEDIUM: mid-range cards (RTX 3060, RX 6600): shadow map, no ray tracing
    {1, true, true, 2, true},      // HIGH: ray traced sun shadows
    {2, true, true, 2, true},      // ULTRA: everything (the defaults)
};
}  // namespace

void FrontendSettings::set_quality(int q) {
    if (q < LOW || q > ULTRA) return;
    const QualityLevel& l = QUALITY_LEVELS[q];
    ray_tracing = l.ray_tracing;
    ao = l.ao;
    shadows = l.shadows;
    bloom = l.bloom;
    if (aa != 1 && aa != 3) aa = l.msaa ? 0 : 2;
}

int FrontendSettings::quality() const {
    for (int q = LOW; q <= ULTRA; q++) {
        const QualityLevel& l = QUALITY_LEVELS[q];
        bool aa_same = aa == 1 || aa == 3 || aa == (l.msaa ? 0 : 2);
        if (ray_tracing == l.ray_tracing && ao == l.ao && shadows == l.shadows && bloom == l.bloom && aa_same) return q;
    }
    return CUSTOM;
}

// The ini's names of the difficulties, in the game's numbering (FrontendSettings::difficulty)
static const char* const DIFFICULTY_KEYS[5] = {"normal", "hard", "very_hard", "very_easy", "easy"};

void FrontendSettings::load(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char key[64], value[64];
        if (sscanf(line, " %63[^= ] = %63s", key, value) != 2) continue;
        std::string k = key, v = value;
        bool on = v == "1" || v == "on" || v == "true";
        if (k == "fullscreen") fullscreen = on;
        else if (k == "volume") volume = std::clamp(atoi(value), 0, 10);
        else if (k == "music") music = std::clamp(atoi(value), 0, 10);
        else if (k == "speakers") speakers = v == "surround" ? 1 : v == "headphones" ? 2 : 0;
        else if (k == "rear_boost") rear_boost = std::clamp(atoi(value) / 3 * 3, 0, 12);
        else if (k == "sample_interpolation") cubic_samples = v != "linear";
        else if (k == "aa") aa = v == "dlaa" ? 1 : v == "dlss" ? 3 : (v == "none" || v == "off") ? 2 : 0, aa_saved = true;
        else if (k == "haze") haze = on;
        else if (k == "bloom") bloom = v == "low" ? 1 : v == "medium" ? 2 : v == "high" ? 3 : on ? 2 : 0;
        else if (k == "ao") ao = on;
        else if (k == "sun") sun = on;
        else if (k == "shadows") shadows = on;
        else if (k == "ray_tracing") ray_tracing = v == "shadows" ? 1 : v == "off" || v == "0" ? 0 : 2;
        else if (k == "soft_particles") soft = on;
        else if (k == "hdr") hdr = on;
        else if (k == "rt_reflections") reflections = on;
        else if (k == "global_illumination") gi = v == "high" ? 2 : v == "off" || v == "0" ? 0 : 1;
        else if (k == "dust") dust = v != "original" && v != "0";
        else if (k == "sharpen") sharpen = on;
        else if (k == "hd_hud" || k == "hd_text") hd_hud = on;
        else if (k == "hd_liveries") hd_liveries = on;
        else if (k == "difficulty") {
            for (int d = 0; d < 5; d++)
                if (v == DIFFICULTY_KEYS[d]) difficulty = d;
        }   // (hd_text: before the HUD's pictures joined it)
        else if (k == "motion") smooth = v != "original";
        else if (k == "saturation") saturation = std::clamp(atoi(value), 0, 200);
        else if (k == "gamma") gamma = std::clamp(int(atof(value) * 100 + 0.5), 50, 150);
        else if (k == "vibration") vibration = v == "low" ? 1 : v == "medium" ? 2 : v == "high" ? 3 : 0;
        else if (k == "trigger_rumble") trigger_rumble = std::clamp(atoi(value) / 10 * 10, 0, 100);
        else if (k == "splash_screens") splash = on;
        else if (k == "menu_music") menu_music = on;
        else if (k == "check_updates") check_updates = on;
        else if (k == "news_seen") news_seen = std::max(0, atoi(value));
    }
    fclose(f);
}

std::string FrontendSettings::text() const {
    std::string out;
    char buf[2048];
    static const char* const AA[4] = {"msaa", "dlaa", "none", "dlss"};
    static const char* const BLOOM[4] = {"off", "low", "medium", "high"};   // also the vibration levels
    static const char* const SPEAKERS[3] = {"stereo", "surround", "headphones"};
    static const char* const RT_MODES[3] = {"off", "shadows", "ao"};
    snprintf(buf, sizeof(buf), "fullscreen = %d\nvolume = %d\nmusic = %d\naa = %s\nhaze = %d\nbloom = %s\nao = %d\nsun = %d\nshadows = %d\nsoft_particles = %d\nhdr = %d\nsharpen = %d\nhd_hud = %d\n"
               "saturation = %d\ngamma = %.2f\nmotion = %s\nspeakers = %s\nrear_boost = %d\nsample_interpolation = %s\n"
               "vibration = %s\nray_tracing = %s\n"
               "splash_screens = %d\nmenu_music = %d\n",
            fullscreen, volume, music, AA[aa], haze, BLOOM[bloom], ao, sun, shadows, soft, hdr, sharpen, hd_hud, saturation,
            gamma / 100.0, smooth ? "smooth" : "original", SPEAKERS[speakers], rear_boost, cubic_samples ? "cubic" : "linear",
            BLOOM[vibration], RT_MODES[ray_tracing],
            splash, menu_music);
    out += buf;
    snprintf(buf, sizeof(buf), "check_updates = %d\nnews_seen = %d\nrt_reflections = %d\nglobal_illumination = %s\ndust = %s\ndifficulty = %s\nhd_liveries = %d\ntrigger_rumble = %d\n", check_updates, news_seen, reflections, gi == 2 ? "high" : gi ? "low" : "off", dust ? "enhanced" : "original", DIFFICULTY_KEYS[std::clamp(difficulty, 0, 4)], hd_liveries, trigger_rumble);
    out += buf;
    return out;
}

void FrontendSettings::save(const char* path) const {
    FILE* f = fopen(path, "w");
    if (!f) return;
    fputs(text().c_str(), f);
    fclose(f);
}

// ---- Setup ----

bool Frontend::init(const std::string& font_dir) {
    static const char* const FILES[6] = {"SairaExtraCondensed-Bold.ttf", "BarlowCondensed-Bold.ttf",
                                         "ShareTechMono-Regular.ttf", "BarlowCondensed-SemiBold.ttf",
                                         "BarlowCondensed-Medium.ttf", "Barlow-Regular.ttf"};
    bool ok = true;
    for (const char* name : FILES) {
        font_data_.push_back(read_file(font_dir + "/" + name));
        auto& data = font_data_.back();
        DWORD count = 0;
        HANDLE h = data.empty() ? nullptr : AddFontMemResourceEx(data.data(), DWORD(data.size()), nullptr, &count);
        if (h) {
            font_handles_.push_back(h);
        } else {
            fprintf(stderr, "[menu] could not load the font %s/%s\n", font_dir.c_str(), name);
            ok = false;
        }
    }
    dc_ = CreateCompatibleDC(nullptr);
    // The splash video, else the splash picture; logo, credit and background:
    // your own in third_party/menu/ first, else the ones in res/menu/
    for (const wchar_t* p : {L"third_party/menu/splash_snicolast.mp4", L"res/menu/splash_snicolast.mp4"})
        if (video_.open(p)) {
            printf("[menu] splash video %ls\n", p);
            break;
        }
    auto load = [&](const wchar_t* name, Picture& pic) {
        for (const wchar_t* dir : {L"third_party/menu/", L"res/menu/"}) {
            std::wstring path = std::wstring(dir) + name;
            if (!load_image(path, pic.px, pic.w, pic.h)) continue;
            printf("[menu] %ls (%dx%d)\n", path.c_str(), pic.w, pic.h);
            return true;
        }
        return false;
    };
    Picture splash;
    if (load(L"splash_snicolast.png", splash)) splash_.push_back(std::move(splash));
    load(L"logo.png", logo_);
    load(L"snicolast.png", credit_);
    if (!load(L"background.png", backdrop_) && !load(L"background.jpg", backdrop_)) placeholder_backdrop();
    return ok;
}

// Any picture format Windows can read (WIC), as BGRA with straight alpha
bool Frontend::load_image(const std::wstring& path, std::vector<uint32_t>& px, int& w_out, int& h_out) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* wic = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    UINT w = 0, h = 0;
    bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&wic))) &&
              SUCCEEDED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                       WICDecodeMetadataCacheOnDemand, &dec)) &&
              SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
              SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                         WICBitmapPaletteTypeCustom)) &&
              SUCCEEDED(conv->GetSize(&w, &h)) && w > 0 && h > 0;
    if (ok) {
        px.resize(size_t(w) * h);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, UINT(px.size() * 4), reinterpret_cast<BYTE*>(px.data())));
    }
    for (IUnknown* p : std::initializer_list<IUnknown*>{conv, frame, dec, wic})
        if (p) p->Release();
    if (!ok) {
        px.clear();
        w_out = h_out = 0;
        return false;
    }
    w_out = int(w);
    h_out = int(h);
    return true;
}

void Frontend::shutdown() {
    video_.close();
    for (auto& [key, f] : fonts_) DeleteObject(f);
    fonts_.clear();
    for (HANDLE h : font_handles_) RemoveFontMemResourceEx(h);
    font_handles_.clear();
    if (dc_) DeleteDC(dc_);
    dc_ = nullptr;
}

// ---- Menu contents and navigation ----

const wchar_t* const Frontend::TAB_NAMES[Frontend::TAB_COUNT] = {L"DISPLAY", L"GRAPHICS", L"AUDIO", L"CONTROLS", L"GAME"};

// Every settings option: its tab, within the tab the order shown, and the
// heading of each group of rows (on its first one). This is the one place to
// put a new option (besides `all` in items()); an option missing here is not
// shown. Left out in Sega Rally 2: SOFT PARTICLES (only the spectators'
// camera flashes). A tab whose rows do not fit the page scrolls.
const Frontend::OptionTab Frontend::OPTION_TABS[] = {
    {OPT_QUALITY, DISPLAY_TAB, L"PRESET"},
    {OPT_DISPLAY, DISPLAY_TAB, L"SCREEN"}, {OPT_MOTION, DISPLAY_TAB}, {OPT_AA, DISPLAY_TAB},
    {OPT_SAT, DISPLAY_TAB, L"PICTURE"}, {OPT_GAMMA, DISPLAY_TAB}, {OPT_SHARPEN, DISPLAY_TAB},
    {OPT_UPDATES, DISPLAY_TAB, L"UPDATES"},
    {OPT_SUN, LIGHTING_TAB, L"LIGHTING"}, {OPT_SHADOWS, LIGHTING_TAB}, {OPT_HDR, LIGHTING_TAB}, {OPT_AO, LIGHTING_TAB},
    {OPT_BLOOM, LIGHTING_TAB}, {OPT_HAZE, LIGHTING_TAB},
    {OPT_RT, LIGHTING_TAB, L"RAY TRACING"}, {OPT_REFLECT, LIGHTING_TAB}, {OPT_GI, LIGHTING_TAB},
    {OPT_DUST, LIGHTING_TAB, L"ARCADE ART"}, {OPT_HUD, LIGHTING_TAB}, {OPT_LIVERIES, LIGHTING_TAB},
    {OPT_VOLUME, AUDIO_TAB, L"VOLUME"}, {OPT_MUSIC, AUDIO_TAB}, {OPT_MENU_MUSIC, AUDIO_TAB},
    {OPT_SPEAKERS, AUDIO_TAB, L"SOUND"}, {OPT_REAR, AUDIO_TAB}, {OPT_SAMPLES, AUDIO_TAB},
    {OPT_CONNECTED, CONTROLS_TAB, L"DEVICES"}, {OPT_VIBRATION, CONTROLS_TAB, L"GAMEPAD"}, {OPT_TRIGGERS, CONTROLS_TAB},
    {OPT_DIFFICULTY, GAME_TAB, L"ARCADE"},
};

// Under each section's name in the column at the left
static const wchar_t* const TAB_ABOUT[] = {L"Screen, smoothing, picture", L"Lighting, ray tracing, arcade art",
                                           L"Volume, speakers, sound", L"Devices, vibration", L"Difficulty"};

const wchar_t* Frontend::group_of(Id id) {
    for (const OptionTab& o : OPTION_TABS)
        if (o.id == id) return o.group;
    return nullptr;
}

const wchar_t* const Frontend::RECORD_TAB_NAMES[Frontend::RECORD_TAB_COUNT] = {L"ACHIEVEMENTS", L"STATS"};

int Frontend::tab_of(Id id) {
    for (const OptionTab& o : OPTION_TABS)
        if (o.id == id) return o.tab;
    return GAME_TAB;
}

void Frontend::set_tab(int t) {
    tab_ = (t + tab_count()) % tab_count();
    scroll_ = 0;
    record_scroll_ = 0;
    // the settings page: its first option (the column of sections is not a row)
    selected_ = screen_ == Screen::Options ? 1 : std::min(selected_, int(items().size()) - 1);
    hovered_ = -1;
    dirty_ = true;
}

// DEBUG: the courses and cars of a practice race, with the game's own index
// for each (runtime.cpp, debug_jump_frame: its course and car selects)
namespace {
struct DebugPick {
    int index;
    const wchar_t* name;
};
const DebugPick DEBUG_COURSES[] = {{0, L"DESERT"}, {1, L"MOUNTAIN"}, {3, L"SNOWY"}, {2, L"RIVIERA"}};
// In SELECT CAR's order, then the two hidden ones (its table at 0xF1F00 has eight)
// DIFFICULTY in the menu's order, easiest first: the game's number for each
// (FrontendSettings::difficulty)
const int DIFFICULTY_ORDER[5] = {3, 4, 0, 1, 2};
const wchar_t* const DIFFICULTY_NAMES[5] = {L"NORMAL", L"HARD", L"VERY HARD", L"VERY EASY", L"EASY"};
const DebugPick DEBUG_CARS[] = {{2, L"FORD ESCORT"},   {3, L"SUBARU IMPREZA"}, {1, L"TOYOTA COROLLA"},
                                {4, L"MITSUBISHI LANCER"}, {0, L"PEUGEOT 306"}, {5, L"LANCIA STRATOS"},
                                {7, L"LANCIA DELTA"},  {6, L"TOYOTA CELICA"}};
constexpr int DEBUG_COURSE_COUNT = int(sizeof(DEBUG_COURSES) / sizeof(DEBUG_COURSES[0]));
constexpr int DEBUG_CAR_COUNT = int(sizeof(DEBUG_CARS) / sizeof(DEBUG_CARS[0]));
}  // namespace

std::vector<Frontend::Item> Frontend::items() const {
    auto on_off = [](bool v) { return std::wstring(v ? L"ON" : L"OFF"); };
    static const wchar_t* const AA[4] = {L"MSAA 4X", L"DLAA", L"OFF", L"DLSS QUALITY"};
    static const wchar_t* const BLOOM_LEVELS[4] = {L"OFF", L"LOW", L"MEDIUM", L"HIGH"};
    static const wchar_t* const SPEAKERS[3] = {L"STEREO", L"SURROUND 4.0", L"HEADPHONES"};
    static const wchar_t* const SPEAKER_HINTS[3] = {
        L"Front and rear sound mixed on two speakers",
        L"Rear sound on your rear speakers, as on the cabinet (4.0, 5.1 or 7.1)",
        L"Two ears: gentle crossfeed, and the rear sound placed behind you"};
    switch (screen_) {
    case Screen::Main: {
        std::vector<Item> list = {{PLAY, L"PLAY", L"", L"The original arcade game"},
                                  {OPTIONS, L"OPTIONS", L"", L"Display, sound and controls"},
                                  {RECORDS, L"RECORDS", L"", L"Achievements and statistics"},
                                  {QUIT, L"QUIT", L"", L"Close the game"}};
        // DEBUG is for testing only: hidden unless SR2_DEBUG_MENU is set
        // (kShowDebugMenu true shows it always)
        constexpr bool kShowDebugMenu = false;
        static const bool debug_menu = kShowDebugMenu || getenv("SR2_DEBUG_MENU") != nullptr;
        if (debug_menu) list.insert(list.begin() + 1, {DEBUG, L"DEBUG", L"", L"Straight to a race of any course with any car (for testing)"});
        return list;
    }
    case Screen::Play:
        return {{ARCADE, L"ARCADE CLASSIC", L"", L"The original arcade game"},
                {GHOST, L"GHOST RACE", L"COMING SOON", L"Race against your own best lap (coming soon)", false},
                {BACK, L"BACK", L"", L"Return to the main menu"}};
    case Screen::Debug: {
        const wchar_t* lr = pad_prompts ? L"LEFT / RIGHT" : L"LEFT / RIGHT or ENTER";
        return {{DEBUG_COURSE, L"COURSE", DEBUG_COURSES[debug_course_].name, std::wstring(lr) + L" changes the course of the practice race"},
                {DEBUG_CAR, L"CAR", DEBUG_CARS[debug_car_].name, std::wstring(lr) + L" changes the car"},
                {DEBUG_GEARS, L"GEARS", debug_gears_ ? L"MANUAL" : L"AUTOMATIC", std::wstring(lr) + L" changes the transmission"},
                {DEBUG_TIME, L"TIME", debug_time_frozen ? L"INFINITE" : L"NORMAL",
                 std::wstring(lr) + L": INFINITE stops the time left, so no race ends with TIME IS UP (PLAY too, until set back)"},
                {DEBUG_GO, L"START RACE", L"", L"The game goes through its selects by itself, unseen, and the race starts"},
                {BACK, L"BACK", L"", L"Return to the main menu"}};
    }
    case Screen::Pause:
        return {{RESUME, L"RESUME", L"", L"Back to the game"},
                {SETTINGS, L"SETTINGS", L"", L"Display, sound and controls"},
                {QUIT_TO_MENU, L"QUIT TO MAIN MENU", L"", L"Leave this game and go back to the main menu"}};
    case Screen::Options: {
        std::wstring aa_hint = settings.aa != aa_at_launch
                                   ? L"Edge smoothing.  Takes effect the next time the game starts"
                                   : settings.aa == 3
                                         ? L"NVIDIA DLSS: the 3D drawn at two thirds of the size and rebuilt sharp, much lighter on the graphics card"
                                         : L"Edge smoothing (MSAA, NVIDIA DLAA or DLSS, or off).  Applies on the next start";
        wchar_t gamma_text[16];
        swprintf(gamma_text, 16, L"%.2f", settings.gamma / 100.0);
        static const wchar_t* const RT_MODES[3] = {L"OFF", L"SHADOWS", L"SHADOWS + AO"};
        static const wchar_t* const QUALITY_NAMES[5] = {L"LOW", L"MEDIUM", L"HIGH", L"ULTRA", L"CUSTOM"};
        static const wchar_t* const QUALITY_HINTS[5] = {
            L"Laptops and integrated graphics: no shadows, ambient occlusion or MSAA, low bloom",
            L"Mid-range cards (RTX 3060, RX 6600): no ray tracing, MSAA 4x, low bloom",
            L"Fast cards: ray traced sun shadows and every effect",
            L"Everything on: ray traced sun shadows and ambient occlusion",
            L"Your own mix of the options.  Pick a level to set the costly ones at once"};
        static const wchar_t* const GI_LEVELS[3] = {L"OFF", L"LOW", L"HIGH"};
        static const wchar_t* const GI_HINTS[3] = {
            L"Only the sun's direct light and the light of the sky",
            L"Sunlight bounces off the ground, the walls and the banks and tints the cars, the dust and what is next to them (with ray tracing on)",
            L"The bounced light finer and from twice as far: rock faces and far banks too (heavier on the graphics card)"};
        static const wchar_t* const RT_HINTS[3] = {
            L"Shadows from a shadow map and occlusion from the depth, as without ray tracing",
            L"Sun shadows traced through the world: every car and tree, soft with distance",
            L"Also the ambient occlusion: corners and the ground under things, even off screen"};
        // The pads and the wheel the game sees (app.cpp fills `gamepads`): the
        // row's value, cut to fit, and the whole list as its hint
        std::wstring connected = gamepads.empty() ? L"NONE" : gamepads;
        if (connected.size() > 24) connected = connected.substr(0, 22) + L"...";
        for (wchar_t& c : connected) c = wchar_t(towupper(c));   // as the other values
        std::vector<Item> all = {{OPT_DISPLAY, L"DISPLAY", settings.fullscreen ? L"FULLSCREEN" : L"WINDOW",
                 pad_prompts ? L"Window or full screen" : L"Window or full screen (also F11 or Alt+Enter)"},
                {OPT_MOTION, L"MOTION", settings.smooth ? L"SMOOTH" : L"ORIGINAL",
                 L"Smooth: the 3D is also drawn between the game's 57.5 frames, at your monitor's refresh rate"},
                {OPT_VOLUME, L"EFFECTS", std::to_wstring(settings.volume), L"Volume of the sound effects and voices"},
                {OPT_MUSIC, L"MUSIC", std::to_wstring(settings.music), L"Volume of the music"},
                {OPT_MENU_MUSIC, L"MENU MUSIC", on_off(settings.menu_music),
                 L"The game's Main Title in the menus, at the music volume (or your own third_party/menu/music.mp3)"},
                {OPT_SPEAKERS, L"SPEAKERS", SPEAKERS[settings.speakers], SPEAKER_HINTS[settings.speakers]},
                {OPT_REAR, L"REAR BOOST", settings.rear_boost ? L"+" + std::to_wstring(settings.rear_boost) + L" DB" : std::wstring(L"OFF"),
                 L"Louder rear speakers, as the cabinet's seat speakers"},
                {OPT_SAMPLES, L"SAMPLE QUALITY", settings.cubic_samples ? L"SMOOTH" : L"ORIGINAL",
                 L"Smooth interpolation of the instrument and voice samples, or the original chip's"},
                {OPT_QUALITY, L"QUALITY", QUALITY_NAMES[settings.quality()], QUALITY_HINTS[settings.quality()]},
                {OPT_AA, L"ANTI-ALIASING", AA[settings.aa], aa_hint},
                {OPT_HAZE, L"DISTANCE HAZE", on_off(settings.haze),
                 L"Far scenery fades into the sky instead of appearing suddenly"},
                {OPT_BLOOM, L"BLOOM", BLOOM_LEVELS[settings.bloom], L"Soft glow around the sun, lights and bright reflections"},
                {OPT_AO, L"AMBIENT OCCLUSION", on_off(settings.ao),
                 L"Soft shadows in corners, under the cars and on the ground around things"},
                {OPT_SUN, L"SUN", on_off(settings.sun), L"Stronger, warmer sunlight"},
                {OPT_SHADOWS, L"SUN SHADOWS", on_off(settings.shadows),
                 L"Cars, trees and scenery cast shadows away from the sun"},
                {OPT_RT, L"RAY TRACING", !rt_available ? L"N/A" : RT_MODES[settings.ray_tracing],
                 !rt_available ? L"Needs a graphics card with ray tracing (NVIDIA RTX, AMD RX 6000 or newer)"
                               : RT_HINTS[settings.ray_tracing],
                 rt_available},
                {OPT_SOFT, L"SOFT PARTICLES", on_off(settings.soft),
                 L"Dust, smoke and spray blend softly where they touch the ground and the cars"},
                {OPT_HDR, L"HDR LIGHTING", on_off(settings.hdr),
                 L"Bright light keeps its detail instead of clipping to white; exposure adapts gently"},
                {OPT_REFLECT, L"RT REFLECTIONS", !rt_available ? L"N/A" : on_off(settings.reflections),
                 !rt_available ? L"Needs a graphics card with ray tracing (NVIDIA RTX, AMD RX 6000 or newer)"
                               : L"The scenery, the stands and the other cars mirrored on the cars' paint and glass (with ray tracing on)",
                 rt_available},
                {OPT_GI, L"GLOBAL ILLUMINATION", !rt_available ? L"N/A" : GI_LEVELS[settings.gi],
                 !rt_available ? L"Needs a graphics card with ray tracing (NVIDIA RTX, AMD RX 6000 or newer)"
                               : GI_HINTS[settings.gi],
                 rt_available},
                {OPT_DUST, L"DUST", settings.dust ? L"ENHANCED" : L"ORIGINAL",
                 settings.dust ? L"Clouds of dust that billow and hang over the road, and stones thrown up by the wheels"
                               : L"The arcade's own flat fans of dust behind the wheels"},
                {OPT_SAT, L"COLOR SATURATION", std::to_wstring(settings.saturation) + L"%",
                 L"How vivid the colours are (100% = original)"},
                {OPT_GAMMA, L"GAMMA", gamma_text, L"Brightness of the mid-tones (1.00 = original)"},
                {OPT_SHARPEN, L"SHARPEN", on_off(settings.sharpen),
                 L"Brings back texture detail after the edge smoothing, without halos"},
                {OPT_UPDATES, L"CHECK FOR UPDATES", on_off(settings.check_updates),
                 L"At launch, look on GitHub for a new version and show what is new in it"},
                {OPT_HUD, L"HD HUD", on_off(settings.hd_hud),
                 settings.hd_hud ? L"Redrawn HUD: sharp fonts, rev counters, gear box and co-driver signs"
                                 : L"The arcade's original pixel HUD and screens"},
                {OPT_LIVERIES, L"HD LIVERIES", on_off(settings.hd_liveries),
                 settings.hd_liveries ? L"Repainted cars: sharp liveries, logos and race numbers"
                                      : L"The arcade's original car paint"},
                {OPT_DIFFICULTY, L"DIFFICULTY", DIFFICULTY_NAMES[settings.difficulty],
                 L"Arcade difficulty: the time you get at the start and at each checkpoint"},
                {OPT_CONNECTED, L"CONNECTED", connected,
                 gamepads.empty() ? L"No gamepad or wheel found. Plug one in: it shows here as soon as the game sees it"
                                  : L"Found: " + gamepads,
                 false},
                {OPT_VIBRATION, L"VIBRATION", BLOOM_LEVELS[settings.vibration],
                 L"The gamepad rumbles with the steering wheel's force feedback: bumps, slides and crashes"},
                {OPT_TRIGGERS, L"TRIGGER RUMBLE", settings.trigger_rumble ? std::to_wstring(settings.trigger_rumble) + L"%" : std::wstring(L"OFF"),
                 L"Xbox One / Series pads: the brake trigger trembles as you slow down, the accelerator as the wheels spin, "
                 L"both over bumps (a share of VIBRATION)"}};
        std::vector<Item> list = {{TABS, L"", L"", pad_prompts ? L"LB / RB change the section"
                                                               : L"Q / E or Tab change the section, or click one"}};
        for (const OptionTab& o : OPTION_TABS)   // the tab's options in their order, then any not listed
            for (const Item& it : all)
                if (it.id == o.id && o.tab == tab_) list.push_back(it);
        list.push_back(
            {BACK, L"BACK", L"", options_from_pause_ ? L"Return to the pause menu" : L"Return to the main menu"});
        return list;
    }
    case Screen::Records: {
        int got = 0;
        for (int64_t t : records.unlocked) got += t != 0;
        std::wstring summary = std::to_wstring(got) + L" of " + std::to_wstring(achievement_table().size()) +
                               (pad_prompts ? L" achievements unlocked.  Up / Down scrolls, LB / RB changes the tab"
                                            : L" achievements unlocked.  Up / Down scrolls, Q / E changes the tab");
        return {{TABS, L"", L"", summary}, {BACK, L"BACK", L"", L"Return to the main menu"}};
    }
    default:
        return {};
    }
}

void Frontend::open_splash() {
    if (video_.is_open() && settings.splash) {
        open(Screen::Splash);
        video_on_ = true;
        splash_from_ = now_seconds();
        skip_from_ = -1;
        video_end_ = -1;
        video_level_ = 0;
        video_.set_volume(float(settings.volume) / 10.0f);
        video_.play();
        return;
    }
    video_.close();
    if (splash_.empty() || !settings.splash) {
        open(Screen::Main);
        return;
    }
    open(Screen::Splash);
    splash_on_ = 0;
    splash_from_ = now_seconds();
    skip_from_ = -1;
    skip_next_ = false;
}

namespace {
// Windjammers' timing: out one after the other, gently then faster; in
// fast, slowing down all the way to the place. The main menu coming up
// (after the splash, back from the game) a little slower, once the picture
// is half in from black
constexpr double SLIDE_OUT_GAP = 0.035, SLIDE_OUT = 0.25, SLIDE_IN_GAP = 0.06, SLIDE_IN = 0.4;
constexpr double INTRO_GAP = 0.085, INTRO_IN = 0.55, INTRO_AFTER = 0.35;
}  // namespace

void Frontend::open(Screen s) {
    const Screen was = screen_;
    if (screen_ == Screen::Splash && s != Screen::Splash) {   // not needed again
        splash_px_ = {}, splash_px_on_ = -1;
        video_.close();
        video_on_ = false;
        video_px_ = {};
        splash_.clear();
    }
    screen_ = s;
    selected_ = 0;
    scroll_ = 0;
    hovered_ = -1;
    hit_.clear();
    dirty_ = true;
    // The main menu coming up (not back from one of its own pages): its
    // entries in from the right edge, as in Windjammers
    if (s == Screen::Main && was != Screen::Options && was != Screen::Records && was != Screen::Debug &&
        was != Screen::Play) {
        slide_from_ = menu_clock() + INTRO_AFTER;
        slide_out_ = false;
        slide_intro_ = true;
    }
}

void Frontend::select(int index) {
    if (index != selected_) dirty_ = true;
    selected_ = index;
}

void Frontend::activate(const Item& it, int dir) {
    dirty_ = true;
    switch (it.id) {
    case PLAY: slide_to(Screen::Play, 0); break;
    case ARCADE: push(Action::StartArcade); break;   // the arcade game as it was
    case GHOST: break;                               // coming soon
    case OPTIONS: options_from_pause_ = false; tab_ = 0; open(Screen::Options); selected_ = 1; break;
    case RECORDS: tab_ = 0; record_scroll_ = 0; open(Screen::Records); break;
    case QUIT: push(Action::Quit); break;
    case QUIT_TO_MENU: push(Action::QuitToMenu); break;
    case DEBUG: open(Screen::Debug); break;
    case DEBUG_COURSE: debug_course_ = (debug_course_ + dir + DEBUG_COURSE_COUNT) % DEBUG_COURSE_COUNT; break;
    case DEBUG_CAR: debug_car_ = (debug_car_ + dir + DEBUG_CAR_COUNT) % DEBUG_CAR_COUNT; break;
    case DEBUG_GEARS: debug_gears_ ^= 1; break;
    case DEBUG_TIME: debug_time_frozen = !debug_time_frozen; break;
    case DEBUG_GO:
        debug_choice = DEBUG_COURSES[debug_course_].index | DEBUG_CARS[debug_car_].index << 4 | debug_gears_ << 8;
        push(Action::StartDebug);
        break;
    case BACK: back(); break;
    case RESUME: push(Action::Resume); break;
    case SETTINGS: options_from_pause_ = true; tab_ = 0; open(Screen::Options); selected_ = 1; break;
    case OPT_DISPLAY: push(Action::ToggleFullscreen); break;
    case OPT_QUALITY: {
        int q = settings.quality();
        settings.set_quality(q == FrontendSettings::CUSTOM ? (dir > 0 ? FrontendSettings::LOW : FrontendSettings::ULTRA)
                                                           : (q + dir + 4) % 4);
        push(Action::SettingsChanged);
        break;
    }
    case OPT_VOLUME:
        settings.volume = dir > 0 && settings.volume == 10 && wrap_ ? 0 : std::clamp(settings.volume + dir, 0, 10);
        push(Action::SettingsChanged);
        break;
    case OPT_MUSIC:
        settings.music = dir > 0 && settings.music == 10 && wrap_ ? 0 : std::clamp(settings.music + dir, 0, 10);
        push(Action::SettingsChanged);
        break;
    case OPT_UPDATES: settings.check_updates = !settings.check_updates; push(Action::SettingsChanged); break;
    case OPT_MENU_MUSIC: settings.menu_music = !settings.menu_music; push(Action::SettingsChanged); break;
    case OPT_SPEAKERS: settings.speakers = (settings.speakers + dir + 3) % 3; push(Action::SettingsChanged); break;
    case OPT_REAR: settings.rear_boost = (settings.rear_boost + 3 * dir + 15) % 15; push(Action::SettingsChanged); break;
    case OPT_SAMPLES: settings.cubic_samples = !settings.cubic_samples; push(Action::SettingsChanged); break;
    case OPT_AA: {   // MSAA, DLAA, DLSS QUALITY, OFF
        static const int ORDER[4] = {0, 1, 3, 2};
        int at = 0;
        for (int i = 0; i < 4; i++)
            if (ORDER[i] == settings.aa) at = i;
        settings.aa = ORDER[(at + dir + 4) % 4];
        push(Action::SettingsChanged);
        break;
    }
    case OPT_HAZE: settings.haze = !settings.haze; push(Action::SettingsChanged); break;
    case OPT_BLOOM: settings.bloom = (settings.bloom + dir + 4) % 4; push(Action::SettingsChanged); break;
    // The two go together: traced AO is ambient occlusion done with rays, so
    // turning one on or off takes the other along (apart, SHADOWS + AO with the
    // occlusion off showed none, and the occlusion on with the rays off was
    // the screen-space one, which read as a bug)
    case OPT_AO:
        settings.ao = !settings.ao;
        if (!settings.ao && settings.ray_tracing == 2) settings.ray_tracing = 1;
        push(Action::SettingsChanged);
        break;
    case OPT_SUN: settings.sun = !settings.sun; push(Action::SettingsChanged); break;
    case OPT_SHADOWS: settings.shadows = !settings.shadows; push(Action::SettingsChanged); break;
    case OPT_RT:
        settings.ray_tracing = (settings.ray_tracing + dir + 3) % 3;
        if (settings.ray_tracing == 2) settings.ao = true;
        push(Action::SettingsChanged);
        break;
    case OPT_SOFT: settings.soft = !settings.soft; push(Action::SettingsChanged); break;
    case OPT_HDR: settings.hdr = !settings.hdr; push(Action::SettingsChanged); break;
    case OPT_REFLECT: settings.reflections = !settings.reflections; push(Action::SettingsChanged); break;
    case OPT_GI: settings.gi = (settings.gi + dir + 3) % 3; push(Action::SettingsChanged); break;
    case OPT_DUST: settings.dust = !settings.dust; push(Action::SettingsChanged); break;
    case OPT_SHARPEN: settings.sharpen = !settings.sharpen; push(Action::SettingsChanged); break;
    case OPT_HUD: settings.hd_hud = !settings.hd_hud; push(Action::SettingsChanged); break;
    case OPT_LIVERIES: settings.hd_liveries = !settings.hd_liveries; push(Action::SettingsChanged); break;
    case OPT_MOTION: settings.smooth = !settings.smooth; push(Action::SettingsChanged); break;
    case OPT_DIFFICULTY: {
        int at = 0;
        for (int i = 0; i < 5; i++)
            if (DIFFICULTY_ORDER[i] == settings.difficulty) at = i;
        settings.difficulty = DIFFICULTY_ORDER[(at + dir + 5) % 5];
        push(Action::SettingsChanged);
        break;
    }
    case OPT_CONNECTED: break;   // shows what is plugged in, nothing to change
    case OPT_VIBRATION: settings.vibration = (settings.vibration + dir + 4) % 4; push(Action::SettingsChanged); break;
    case OPT_TRIGGERS:   // 0..100 % in steps of 10; a click past the top starts again from OFF
        settings.trigger_rumble = dir > 0 && settings.trigger_rumble >= 100 && wrap_
                                      ? 0 : std::clamp(settings.trigger_rumble + 10 * dir, 0, 100);
        push(Action::SettingsChanged);
        break;
    case OPT_SAT:   // 0..200 % in steps of 10; a click past the top starts again from the bottom
        settings.saturation = dir > 0 && settings.saturation >= 200 && wrap_
                                  ? 0 : std::clamp(settings.saturation + 10 * dir, 0, 200);
        push(Action::SettingsChanged);
        break;
    case OPT_GAMMA:   // 0.50..1.50 in steps of 0.05
        settings.gamma = dir > 0 && settings.gamma >= 150 && wrap_
                             ? 50 : std::clamp(settings.gamma + 5 * dir, 50, 150);
        push(Action::SettingsChanged);
        break;
    }
}

int Frontend::index_of(Id id) const {
    std::vector<Item> list = items();
    for (size_t i = 0; i < list.size(); i++)
        if (list[i].id == id) return int(i);
    return 0;
}

void Frontend::back() {
    switch (screen_) {
    case Screen::Options:
        open(options_from_pause_ ? Screen::Pause : Screen::Main);
        selected_ = options_from_pause_ ? 1 : index_of(OPTIONS);   // where the options were opened from
        break;
    case Screen::Records: open(Screen::Main); selected_ = index_of(RECORDS); break;
    case Screen::Debug: open(Screen::Main); selected_ = index_of(DEBUG); break;
    case Screen::Play: slide_to(Screen::Main, 0); break;   // PLAY chosen
    case Screen::Pause: push(Action::Resume); break;
    case Screen::Main: selected_ = index_of(QUIT); dirty_ = true; break;
    default: break;
    }
}

bool Frontend::key(int vk) {
    if (!active()) return false;
    if (screen_ == Screen::Splash) {   // these hurry it on, the rest do nothing
        if (vk == VK_RETURN || vk == VK_SPACE || vk == VK_ESCAPE || vk == VK_BACK) skip_splash();
        return true;
    }
    if (starting) return true;   // nothing to choose while the game starts
    if (slide_from_ >= 0) finish_slide();   // the entries moving: in place at once, and the key does its job
    if (news_open_ && screen_ == Screen::Main) return news_key(vk);
    auto list = items();
    int n = int(list.size());
    bool option_row = selected_ < n && (list[selected_].id >= OPT_QUALITY || list[selected_].id == DEBUG_COURSE ||
                                        list[selected_].id == DEBUG_CAR || list[selected_].id == DEBUG_GEARS ||
                                        list[selected_].id == DEBUG_TIME);
    bool tabs_row = selected_ < n && list[selected_].id == TABS;
    if (screen_ == Screen::Records) switch (vk) {   // two tabs and a list to scroll
        case 'Q': case VK_PRIOR: case VK_LEFT: case 'A': set_tab(tab_ - 1); return true;
        case 'E': case VK_NEXT: case VK_RIGHT: case 'D': case VK_TAB: set_tab(tab_ + 1); return true;
        case VK_UP: case 'W': scroll_records(-1); return true;
        case VK_DOWN: case 'S': scroll_records(+1); return true;
        case VK_RETURN: case VK_SPACE: case VK_ESCAPE: case VK_BACK: back(); return true;
        default: return false;
        }
    if (screen_ == Screen::Options) switch (vk) {   // tab keys
        case 'Q': case VK_PRIOR: set_tab(tab_ - 1); return true;
        case 'E': case VK_NEXT: set_tab(tab_ + 1); return true;
        case VK_TAB: set_tab(tab_ + ((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1)); return true;
        }
    if (screen_ == Screen::Options && n > 1) switch (vk) {   // the rows: the column of sections is not one of them
        case VK_UP: case 'W': select(selected_ <= 1 ? n - 1 : selected_ - 1); return true;
        case VK_DOWN: case 'S': select(selected_ >= n - 1 ? 1 : selected_ + 1); return true;
        }
    switch (vk) {
    case VK_UP: case 'W': select((selected_ + n - 1) % n); return true;
    case VK_DOWN: case 'S': select((selected_ + 1) % n); return true;
    case VK_LEFT: case 'A':
        if (tabs_row) set_tab(tab_ - 1);
        else if (option_row) activate(list[selected_], -1);
        return true;
    case VK_RIGHT: case 'D':
        if (tabs_row) set_tab(tab_ + 1);
        else if (option_row) activate(list[selected_], +1);
        return true;
    case VK_RETURN: case VK_SPACE:
        hovered_ = -1;
        if (selected_ < n && list[selected_].enabled) activate(list[selected_], +1);
        return true;
    case VK_ESCAPE: case VK_BACK: back(); return true;
    }
    return false;
}

// ---- UPDATE AVAILABLE ----

void Frontend::check_for_news() {
    if (!settings.check_updates || news_download_) return;
    auto d = std::make_shared<NewsDownload>();
    news_download_ = d;
    if (const char* file = getenv("SR2_NEWS_FILE")) {   // testing aid: this file in place of GitHub's news.txt
        std::vector<char> text = read_file(file);
        d->text.assign(text.begin(), text.end());
        d->state = text.empty() ? 2 : 1;
        return;
    }
    std::thread([d] {   // on its own: the game may close before it ends
        std::string text;
        bool ok = https_get(NEWS_HOST, NEWS_PATH, text);
        {
            std::lock_guard<std::mutex> lock(d->mutex);
            d->text = std::move(text);
        }
        d->state = ok ? 1 : 2;
    }).detach();
}

void Frontend::take_news() {
    if (!news_download_) return;
    int state = news_download_->state.load();
    if (state == 0 || state == 3) return;   // still downloading, or done with
    news_download_->state = 3;
    if (state != 1) {
        printf("[menu] news: could not reach GitHub (no update check)\n");
        return;
    }
    std::string text;
    {
        std::lock_guard<std::mutex> lock(news_download_->mutex);
        text = news_download_->text;
    }
    std::vector<NewsEntry> remote = parse_news(text);
    std::vector<char> here = read_file("news.txt");   // what this version already has
    int local = news_latest(parse_news(std::string(here.begin(), here.end())));
    news_latest_ = news_latest(remote);
    // A new entry since the last LATER brings the window up; it then lists
    // everything this version is missing, the entries put off before too
    if (news_latest_ > std::max(local, settings.news_seen)) news_ = news_after(remote, local);
    printf("[menu] news: newest %d on GitHub, %d here, %d seen: %zu new\n", news_latest_, local, settings.news_seen,
           news_.size());
}

bool Frontend::news_key(int vk) {
    switch (vk) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_TAB:
    case 'A': case 'D': case 'W': case 'S':
        news_button_ ^= 1;
        dirty_ = true;
        return true;
    case VK_RETURN: case VK_SPACE: news_choose(news_button_); return true;
    case VK_ESCAPE: case VK_BACK: news_choose(1); return true;   // LATER
    }
    return true;   // nothing else while it is open
}

void Frontend::news_choose(int button) {
    news_open_ = false;
    dirty_ = true;
    if (button == 0) {
        start_update();
    } else {   // not again until a newer entry
        settings.news_seen = std::max(settings.news_seen, news_latest_);
        push(Action::SettingsChanged);   // saves it
    }
    news_.clear();
}

// update.bat -FromGame from the game's folder in its own window: it waits for
// the game to close, updates, rebuilds and starts it again. Without
// update.bat (an old folder) the GitHub page opens instead
void Frontend::start_update() {
    wchar_t module[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    std::wstring dir = module;
    dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
    std::wstring bat = dir + L"update.bat";
    DWORD a = GetFileAttributesW(bat.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY) &&
        INT_PTR(ShellExecuteW(nullptr, L"open", bat.c_str(), L"-FromGame", dir.c_str(), SW_SHOWNORMAL)) > 32) {
        printf("[menu] update: started %ls, closing the game\n", bat.c_str());
        push(Action::Quit);
        return;
    }
    printf("[menu] update: could not start %ls, opening the GitHub page\n", bat.c_str());
    ShellExecuteW(nullptr, L"open", GAME_URL, nullptr, nullptr, SW_SHOWNORMAL);
}

// A card over the darkened main menu: UPDATE AVAILABLE, the entries of
// news.txt (as many lines as fit nicely), the two buttons and what UPDATE NOW does
void Frontend::draw_news() {
    auto U = [&](float v) { return int(v * u_ + 0.5f); };
    int w = w_, h = h_, cx = w / 2;
    fill(0, 0, w, h, 0x000000, 0.62f);
    int half = std::min(U(600), w / 2 - U(16));
    int left = cx - half + U(60), text_w = 2 * half - U(120);
    int text_px = U(27), indent = U(30);

    enum Kind { HEAD, BULLET, MORE, PLAIN, GAP };
    struct Line { std::wstring s; Kind kind; };
    std::vector<Line> lines;
    for (const NewsEntry& e : news_) {
        if (!lines.empty()) lines.push_back({L"", GAP});
        if (!e.title.empty()) lines.push_back({widen(e.title), HEAD});
        for (const std::string& raw : e.lines) {
            std::wstring t = widen(raw);
            bool bullet = t.size() >= 2 && (t[0] == L'-' || t[0] == L'*') && t[1] == L' ';
            if (bullet) t.erase(0, 2);
            auto wrapped = wrap(ITEM, text_px, t, text_w - (bullet ? indent : 0));
            for (size_t i = 0; i < wrapped.size(); i++)
                lines.push_back({wrapped[i], !bullet ? PLAIN : i == 0 ? BULLET : MORE});
        }
    }
    const size_t MAX_LINES = 14;
    bool cut = lines.size() > MAX_LINES;
    if (cut) lines.resize(MAX_LINES - 1);
    auto line_h = [&](Kind k) { return k == GAP ? U(16) : U(36); };
    int content = cut ? U(36) : 0;
    for (const Line& l : lines) content += line_h(l.kind);

    int bh = U(66);
    int card_h = U(178) + content + U(34) + bh + U(26) + U(30) + U(34);
    int top = std::max(U(16), (h - card_h) / 2), bottom = top + card_h;
    fill(cx - half, top, cx + half, bottom, PANEL, 0.95f);
    saber(cx, top, half);
    saber(cx, bottom, half);
    text(TITLE, U(70), L"UPDATE AVAILABLE", cx, top + U(30), 1, {GOLD_TOP, GOLD_BOTTOM, EDGE, U(4), 1, U(4)});
    text(MONO, U(22), L"WHAT'S NEW", cx, top + U(124), 1, {0x9ED8FF, 0x6AB8F0});

    int y = top + U(178);
    for (const Line& l : lines) {
        switch (l.kind) {
        case HEAD: text(MONO, U(22), l.s, left, y + U(6), 0, {0x9ED8FF, 0x6AB8F0}); break;
        case BULLET: {
            int d = std::max(3, U(7));
            fill(left + U(6), y + U(17) - d / 2, left + U(6) + d, y + U(17) - d / 2 + d, GOLD_BOTTOM, 1);
            text(ITEM, text_px, l.s, left + indent, y, 0, {PALE_TOP, PALE_BOTTOM});
            break;
        }
        case MORE: text(ITEM, text_px, l.s, left + indent, y, 0, {PALE_TOP, PALE_BOTTOM}); break;
        case PLAIN: text(ITEM, text_px, l.s, left, y, 0, {PALE_TOP, PALE_BOTTOM}); break;
        case GAP: break;
        }
        y += line_h(l.kind);
    }
    if (cut) {
        text(ITEM, U(24), L"...and more: news.txt on the game's GitHub page", left, y + U(2), 0, {HINT, HINT});
        y += U(36);
    }

    // The buttons side by side, the chosen one lit
    static const wchar_t* const LABELS[2] = {L"UPDATE NOW", L"LATER"};
    int bw = std::min(U(340), half - U(40)), gap = U(40), by = y + U(34);
    for (int b = 0; b < 2; b++) {
        int x0 = b == 0 ? cx - gap / 2 - bw : cx + gap / 2;
        news_buttons_[b] = {x0, by, x0 + bw, by + bh};
        bool sel = b == news_button_;
        fill(x0, by, x0 + bw, by + bh, sel ? SABER : 0x000000, sel ? 0.34f : 0.45f);
        frame(x0, by, x0 + bw, by + bh, std::max(1, U(sel ? 3.0f : 1.5f)), sel ? 0xEAF6FF : SABER, sel ? 1.0f : 0.55f);
        const Style st = sel ? Style{GOLD_TOP, GOLD_BOTTOM, EDGE, U(2), 1, U(2)} : Style{PALE_TOP, PALE_BOTTOM, EDGE, U(2), 0.95f};
        text(TITLE, U(40), LABELS[b], x0 + bw / 2, by + bh / 2 - U(22), 1, st);
    }
    const wchar_t* about = news_button_ == 0
                               ? L"Closes the game, downloads the new version, builds it again (update.bat) and starts it"
                               : L"Back to the menu.  This news will not show again; update.bat updates any time";
    text(ITEM, U(22), about, cx, by + bh + U(26), 1, {HINT, HINT});
}

void Frontend::mouse_move(int x, int y) {
    if (screen_ == Screen::Splash) return;
    mouse_x_ = x;
    mouse_y_ = y;
    if (news_open_ && screen_ == Screen::Main) {   // only its two buttons
        int hot = -1;
        for (int b = 0; b < 2; b++)
            if (PtInRect(&news_buttons_[b], POINT{x, y})) hot = b;
        if (hot != news_hot_ || (hot >= 0 && hot != news_button_)) dirty_ = true;
        news_hot_ = hot;
        if (hot >= 0) news_button_ = hot;
        return;
    }
    int hit = -1;
    for (size_t i = 0; i < hit_.size(); i++) {
        POINT p{x, y};
        if (PtInRect(&hit_[i], p)) hit = int(i);
    }
    if (hit >= 0) select(hit);
    hovered_ = hit;
}

void Frontend::click(int x, int y) {
    if (slide_from_ >= 0) finish_slide();
    if (screen_ == Screen::Splash) {
        skip_splash();
        return;
    }
    if (starting) return;
    POINT p{x, y};
    if (news_open_ && screen_ == Screen::Main) {
        for (int b = 0; b < 2; b++)
            if (PtInRect(&news_buttons_[b], p)) news_choose(b);
        return;
    }
    if (screen_ == Screen::Records && (PtInRect(&more_up_, p) || PtInRect(&more_down_, p))) {
        scroll_records(PtInRect(&more_up_, p) ? -1 : 1);
        return;
    }
    if (PtInRect(&more_up_, p) || PtInRect(&more_down_, p)) {   // the scroll arrows of a long settings tab
        int n = int(items().size());
        int k = std::clamp(selected_ - 1, 0, n - 3) + (PtInRect(&more_up_, p) ? -1 : 1);
        select(1 + std::clamp(k, 0, n - 3));
        return;
    }
    mouse_move(x, y);
    auto list = items();
    if (hovered_ < 0 || hovered_ >= int(list.size()) || !list[hovered_].enabled) return;
    if (list[hovered_].id == TABS) {   // the tab under the pointer (the settings page: the section)
        for (int t = 0; t < int(tab_rects_.size()); t++)
            if (screen_ == Screen::Options ? PtInRect(&tab_rects_[t], p) != FALSE
                                           : x >= tab_rects_[t].left && x < tab_rects_[t].right)
                set_tab(t);
        return;
    }
    // Option rows: a click on the value side goes down left of the middle of the
    // value and up right of it (the arrows); on the label it steps up and wraps around
    int dir = +1;
    if (hovered_ < int(values_.size()) && values_[hovered_].left >= 0 && x >= values_[hovered_].left)
        dir = x < values_[hovered_].middle ? -1 : +1;
    else wrap_ = true;
    activate(list[hovered_], dir);
    wrap_ = false;
}

void Frontend::wheel(int x, int y, int delta) {
    if (starting || screen_ == Screen::Splash || (news_open_ && screen_ == Screen::Main)) return;
    mouse_move(x, y);
    if (screen_ == Screen::Records) {
        scroll_records(delta > 0 ? -1 : 1);
        return;
    }
    auto list = items();
    bool on_value = hovered_ >= 0 && hovered_ < int(values_.size()) && values_[hovered_].left >= 0 &&
                    x >= values_[hovered_].left;
    bool scrolls = screen_ == Screen::Options && options_scroll_;
    if (hovered_ >= 0 && hovered_ < int(list.size()) && list[hovered_].id >= OPT_QUALITY && (on_value || !scrolls)) {
        int keep = hovered_;   // over a value: change it
        hovered_ = -1;   // no wrap-around from the wheel
        activate(list[keep], delta > 0 ? +1 : -1);
        hovered_ = keep;
    } else if (scrolls) {   // a long tab, away from the values: scroll the list
        int k = std::clamp(selected_ - 1, 0, int(list.size()) - 3) + (delta > 0 ? -1 : 1);
        select(1 + std::clamp(k, 0, int(list.size()) - 3));
    }
}

std::vector<Frontend::Action> Frontend::take_actions() {
    std::vector<Action> a;
    a.swap(actions_);
    return a;
}

// ---- Records ----

namespace {

std::wstring grouped(int64_t v) {   // 12,345
    std::wstring d = std::to_wstring(v < 0 ? -v : v), r;
    for (size_t i = 0; i < d.size(); i++) {
        if (i && (d.size() - i) % 3 == 0) r += L',';
        r += d[i];
    }
    return v < 0 ? L"-" + r : r;
}

std::wstring race_time(int64_t ticks) {   // 1'02"34, as the game shows its times (1/6000 s)
    if (ticks <= 0) return L"-";
    int64_t cs = ticks / 60;
    wchar_t b[32];
    swprintf(b, 32, L"%lld'%02lld\"%02lld", (long long)(cs / 6000), (long long)(cs / 100 % 60), (long long)(cs % 100));
    return b;
}

std::wstring ordinal(int n) {   // 1st, 2nd, 3rd, 4th ... 11th, 12th, 13th
    const wchar_t* th = n % 100 / 10 == 1 ? L"th" : n % 10 == 1 ? L"st" : n % 10 == 2 ? L"nd" : n % 10 == 3 ? L"rd" : L"th";
    return std::to_wstring(n) + th;
}

std::wstring hours(int64_t frames) {   // 3 H 05 MIN
    int64_t m = int64_t(double(frames) / 57.524 / 60);
    wchar_t b[32];
    swprintf(b, 32, L"%lld H %02lld MIN", (long long)(m / 60), (long long)(m % 60));
    return b;
}

}  // namespace

std::vector<Frontend::RecordRow> Frontend::record_rows() const {
    std::vector<RecordRow> rows;
    const Stats& s = records.stats;
    if (tab_ == 0) {   // achievements: unlocked first (newest on top), then the rest in the table's order
        const auto& table = achievement_table();
        std::vector<int> order(table.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = int(i);
        auto when = [&](int i) { return size_t(i) < records.unlocked.size() ? records.unlocked[size_t(i)] : 0; };
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return (when(a) != 0) != (when(b) != 0) ? when(a) != 0 : when(a) > when(b);
        });
        for (int i : order) {
            const AchievementInfo& a = table[size_t(i)];
            RecordRow r{a.name, a.text, L"", when(i) != 0};
            if (r.lit) {
                time_t t = time_t(when(i));
                wchar_t b[32];
                wcsftime(b, 32, L"%d %b %Y", localtime(&t));
                r.value = b;
            } else if (a.goal > 1) {
                int64_t p = achievement_progress(s, i);
                r.value = std::string(a.key) == "veteran"
                              ? std::to_wstring(int(double(p) / 57.524 / 3600)) + L" / 5 H"
                              : grouped(p) + L" / " + grouped(a.goal);
            } else {
                r.value = L"LOCKED";
            }
            rows.push_back(r);
        }
        return rows;
    }
    auto row = [&](const std::wstring& label, const std::wstring& value, const std::wstring& detail = L"") {
        rows.push_back({label, detail, value, true});
    };
    row(L"GAMES PLAYED", grouped(s.games));
    row(L"TIME PLAYED", hours(s.play_frames));
    row(L"RACES FINISHED", grouped(s.races_finished), L"Of " + grouped(s.races_started) + L" started");
    row(L"STAGES CLEARED", grouped(s.stages_cleared), L"In the championship");
    row(L"CHAMPIONSHIPS", grouped(s.championships),
        s.championships ? L"Best position " + ordinal(s.best_champ_position) + L",  best total time " +
                              race_time(s.best_champ_time)
                        : L"Not finished yet");
    for (int i = 0; i < STAT_COURSES; i++) {   // the best lap of either mode on the right
        const Stats::Course& c = s.course[i];
        int64_t lap = c.best_lap[0] && (!c.best_lap[1] || c.best_lap[0] < c.best_lap[1]) ? c.best_lap[0] : c.best_lap[1];
        std::wstring detail;
        for (int m = 0; m < 2; m++)   // the best race time of each mode (the laps of each are in save/stats.ini)
            if (c.best_time[m])
                detail += std::wstring(detail.empty() ? L"Best time: " : L",  ") + (m ? L"practice " : L"championship ") +
                          race_time(c.best_time[m]);
        if (detail.empty()) detail = L"Not finished yet";
        row(std::wstring(STAT_COURSE_NAMES[i]) + L"  BEST LAP", race_time(lap), detail);
    }
    return rows;
}

void Frontend::scroll_records(int by) {
    int n = int(record_rows().size());
    int k = std::clamp(record_scroll_ + by, 0, std::max(0, n - VISIBLE_RECORDS));
    if (k != record_scroll_) dirty_ = true;
    record_scroll_ = k;
}

// The rows of the open records tab between x0 and x1, from y0 down
void Frontend::draw_records(int x0, int x1, int y0, int row_h) {
    auto U = [&](float v) { return int(v * u_ + 0.5f); };
    auto rows = record_rows();
    record_scroll_ = std::clamp(record_scroll_, 0, std::max(0, int(rows.size()) - VISIBLE_RECORDS));
    const Style lit{PALE_TOP, PALE_BOTTOM, 0x080806, U(2), 0.95f};
    const Style dark{DIM, DIM, 0x080806, U(2), 0.9f};
    const Style value{0xF4D27A, 0xE0A848, 0x080806, U(2), 0.95f};
    int label_px = U(32), detail_px = U(19);
    for (int k = 0; k < VISIBLE_RECORDS && record_scroll_ + k < int(rows.size()); k++) {
        const RecordRow& r = rows[size_t(record_scroll_ + k)];
        int y = y0 + k * row_h;
        if (k % 2 == 0) fill(x0 - U(20), y - U(4), x1 + U(20), y + row_h - U(4), 0xFFFFFF, 0.03f);
        text(ITEM, label_px, r.label, x0, y, 0, r.lit ? lit : dark);
        if (!r.detail.empty())
            text(MONO, detail_px, r.detail, x0, y + label_px + U(4), 0, {r.lit ? HINT : DIM, r.lit ? HINT : DIM});
        text(ITEM, label_px, r.value, x1, y + U(6), 2, r.lit ? value : dark);
    }
    if (record_scroll_ > 0) {   // more above / below: arrows that also scroll on a click
        triangle(x1 + U(36), y0 + U(10), U(20), -2, GOLD_BOTTOM, 1);
        more_up_ = {x1 + U(10), y0 - U(16), x1 + U(64), y0 + U(36)};
    }
    if (record_scroll_ + VISIBLE_RECORDS < int(rows.size())) {
        int yb = y0 + VISIBLE_RECORDS * row_h - U(20);
        triangle(x1 + U(36), yb, U(20), 2, GOLD_BOTTOM, 1);
        more_down_ = {x1 + U(10), yb - U(26), x1 + U(64), yb + U(26)};
    }
}

// ---- Drawing primitives (premultiplied BGRA canvas) ----

// Integer "over" on premultiplied BGRA (it runs for every pixel of panels and text)
static inline uint32_t over(uint32_t d, uint32_t rgb, int a) {   // a: 0..256
    int k = 256 - a;
    uint32_t rb = (((rgb & 0xFF00FFu) * a + (d & 0xFF00FFu) * k) >> 8) & 0xFF00FFu;
    uint32_t g = (((rgb & 0x00FF00u) * a + (d & 0x00FF00u) * k) >> 8) & 0x00FF00u;
    uint32_t da = ((255u * a + (d >> 24) * k) >> 8);
    return rb | g | (da << 24);
}

void Frontend::blend(int x, int y, uint32_t rgb, float a) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_ || a <= 0) return;
    uint32_t& d = px_[size_t(y) * w_ + x];
    d = over(d, rgb, int(std::min(a, 1.0f) * 256.0f + 0.5f));
}

void Frontend::fill(int x0, int y0, int x1, int y1, uint32_t rgb, float alpha) {
    x0 = std::max(x0, 0), y0 = std::max(y0, 0), x1 = std::min(x1, w_), y1 = std::min(y1, h_);
    int a = int(std::clamp(alpha, 0.0f, 1.0f) * 256.0f + 0.5f);
    for (int y = y0; y < y1; y++) {
        uint32_t* row = px_ + size_t(y) * w_;
        for (int x = x0; x < x1; x++) row[x] = over(row[x], rgb, a);
    }
}

// Horizontal band that fades out towards both ends
void Frontend::band(int cx, int y0, int y1, int half_w, uint32_t rgb, float alpha) {
    for (int x = std::max(cx - half_w, 0); x < std::min(cx + half_w, w_); x++) {
        float t = 1 - std::abs(float(x - cx)) / float(half_w);
        float a = alpha * std::min(1.0f, t * 2.5f);
        for (int y = std::max(y0, 0); y < std::min(y1, h_); y++) blend(x, y, rgb, a);
    }
}

// Small solid triangle pointing left (dir -1), right (1), up (-2) or down (2), centred on (x, y)
void Frontend::triangle(int x, int y, int size, int dir, uint32_t rgb, float alpha) {
    int half = size / 2;
    for (int dy = -half; dy <= half; dy++) {
        float len = float(size) * (1 - std::abs(float(dy)) / float(half + 1)) * 0.9f;
        for (int i = 0; i < int(len + 1); i++) {
            float a = std::clamp(len - float(i), 0.0f, 1.0f) * alpha;
            if (dir == 2 || dir == -2) {   // pointing down / up
                int py = dir > 0 ? y - half + i : y + half - i;
                blend(x + dy, py, rgb, a);
                continue;
            }
            int px = dir > 0 ? x - half + i : x + half - i;
            blend(px, y + dy, rgb, a);
        }
    }
}

HFONT Frontend::font(Family f, int px) {
    auto key = std::make_pair(int(f), px);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second;
    HFONT h = CreateFontW(-px, 0, 0, 0, WEIGHTS[f], FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, FACES[f]);
    fonts_[key] = h;
    return h;
}

const Frontend::Mask& Frontend::mask(Family f, int px, const std::wstring& s, int edge_px) {
    std::wstring key = std::to_wstring(int(f)) + L"|" + std::to_wstring(px) + L"|" + std::to_wstring(edge_px) + L"|" + s;
    auto it = masks_.find(key);
    if (it != masks_.end()) return it->second;
    if (masks_.size() > 800) masks_.clear();   // window resized many times
    Mask& m = masks_[key];
    SelectObject(dc_, font(f, px));
    const std::wstring& t = s;
    SIZE sz{};
    GetTextExtentPoint32W(dc_, t.c_str(), int(t.size()), &sz);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc_, &tm);
    m.pad = edge_px + 2 + px / 6;   // room for the outline and overhanging letters
    m.text_w = sz.cx;
    m.text_h = sz.cy;
    m.w = sz.cx + 2 * m.pad;
    m.h = sz.cy + 2 * m.pad;
    m.grad0 = m.pad + tm.tmInternalLeading + (tm.tmAscent - tm.tmInternalLeading) / 8;
    m.grad1 = m.pad + tm.tmAscent;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = m.w;
    bi.bmiHeader.biHeight = -m.h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return m;
    HGDIOBJ old = SelectObject(dc_, bmp);
    memset(bits, 0, size_t(m.w) * m.h * 4);
    SetBkMode(dc_, TRANSPARENT);
    SetTextColor(dc_, RGB(255, 255, 255));
    TextOutW(dc_, m.pad, m.pad, t.c_str(), int(t.size()));
    GdiFlush();
    m.a.resize(size_t(m.w) * m.h);
    const uint8_t* p = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < m.a.size(); i++) m.a[i] = p[i * 4 + 1];
    SelectObject(dc_, old);
    DeleteObject(bmp);
    if (edge_px > 0) {   // outline: the letters grown by a round brush
        m.e.assign(m.a.size(), 0);
        float r = float(edge_px);
        for (int dy = -edge_px - 1; dy <= edge_px + 1; dy++)
            for (int dx = -edge_px - 1; dx <= edge_px + 1; dx++) {
                float wgt = std::clamp(r + 1 - std::sqrt(float(dx * dx + dy * dy)), 0.0f, 1.0f);
                if (wgt <= 0) continue;
                for (int y = std::max(0, -dy); y < std::min(m.h, m.h - dy); y++) {
                    const uint8_t* src = &m.a[size_t(y + dy) * m.w];
                    uint8_t* dst = &m.e[size_t(y) * m.w];
                    for (int x = std::max(0, -dx); x < std::min(m.w, m.w - dx); x++) {
                        uint8_t v = uint8_t(float(src[x + dx]) * wgt);
                        if (v > dst[x]) dst[x] = v;
                    }
                }
            }
    }
    return m;
}

int Frontend::measure(Family f, int px, const std::wstring& s) {
    return mask(f, px, s, 0).text_w;
}

// Draws text with its box's top at y; align 0 = left of x, 1 = centred, 2 = right
int Frontend::text(Family f, int px, const std::wstring& s, int x, int y, int align, const Style& st) {
    const Mask& m = mask(f, px, s, st.edge_px);
    int x0 = x - (align == 1 ? m.text_w / 2 : align == 2 ? m.text_w : 0) - m.pad;
    int y0 = y - m.pad;
    auto paint = [&](const std::vector<uint8_t>& cov, int ox, int oy, bool gradient, uint32_t rgb, float alpha) {
        for (int j = 0; j < m.h; j++) {
            uint32_t row = rgb;
            if (gradient) {
                float t = std::clamp(float(j - m.grad0) / float(std::max(1, m.grad1 - m.grad0)), 0.0f, 1.0f);
                row = mix(st.top, st.bottom, t);
            }
            for (int i = 0; i < m.w; i++) {
                uint8_t c = cov[size_t(j) * m.w + i];
                if (c) blend(x0 + ox + i, y0 + oy + j, row, float(c) / 255.0f * alpha);
            }
        }
    };
    const auto& outer = st.edge_px > 0 ? m.e : m.a;
    if (st.shadow) paint(outer, st.shadow / 2, st.shadow, false, 0x000000, 0.55f * st.alpha);
    if (st.edge_px > 0) paint(m.e, 0, 0, false, st.edge, st.alpha);
    paint(m.a, 0, 0, true, 0, st.alpha);
    return m.text_w;
}

// A glowing line in the accent colour: a white-hot core in a soft glow,
// fading out towards both ends
void Frontend::saber(int cx, int y, int half_w) {
    int glow = std::max(4, int(16 * u_)), core = std::max(1, int(1.6f * u_ + 0.5f));
    for (int x = std::max(cx - half_w, 0); x < std::min(cx + half_w, w_); x++) {
        float t = 1 - std::abs(float(x - cx)) / float(half_w);
        float a = std::min(1.0f, t * 3.0f);
        for (int dy = -glow; dy <= glow; dy++) {
            float d = std::abs(float(dy)) / float(glow);
            blend(x, y + dy, SABER, a * 0.55f * std::exp(-d * d * 5.0f));
            if (std::abs(dy) <= core) blend(x, y + dy, 0xEAF6FF, a);
        }
    }
}

// ---- Shapes and pictures ----

void Frontend::ribbon(int x0, int y0, int x1, int y1, int slant, float a0, float a1, int gap, int sw, float alpha) {
    int h = std::max(1, y1 - y0);
    for (int y = y0; y < y1; y++) {
        int s = (y1 - 1 - y) * slant / h;
        hgrad(x0 + s, y, x1 + s, y + 1, SABER, a0 * alpha, a1 * alpha);
        if (sw > 0) fill(x0 + s - gap - sw, y, x0 + s - gap, y + 1, SABER, 0.9f * alpha);
    }
}

void Frontend::hgrad(int x0, int y0, int x1, int y1, uint32_t rgb, float a0, float a1) {
    if (x1 <= x0) return;
    for (int x = std::max(x0, 0); x < std::min(x1, w_); x++) {
        float t = float(x - x0) / float(x1 - x0);
        int a = int(std::clamp(a0 + (a1 - a0) * t, 0.0f, 1.0f) * 256.0f + 0.5f);
        for (int y = std::max(y0, 0); y < std::min(y1, h_); y++) {
            uint32_t& d = px_[size_t(y) * w_ + x];
            d = over(d, rgb, a);
        }
    }
}

void Frontend::vgrad(int x0, int y0, int x1, int y1, uint32_t rgb, float a0, float a1) {
    if (y1 <= y0) return;
    for (int y = std::max(y0, 0); y < std::min(y1, h_); y++)
        fill(x0, y, x1, y + 1, rgb, a0 + (a1 - a0) * float(y - y0) / float(y1 - y0));
}

void Frontend::frame(int x0, int y0, int x1, int y1, int t, uint32_t rgb, float alpha) {
    fill(x0, y0, x1, y0 + t, rgb, alpha);
    fill(x0, y1 - t, x1, y1, rgb, alpha);
    fill(x0, y0 + t, x0 + t, y1 - t, rgb, alpha);
    fill(x1 - t, y0 + t, x1, y1 - t, rgb, alpha);
}

// A box with half-round ends, smoothed
void Frontend::pill(int x0, int y0, int x1, int y1, uint32_t rgb, float alpha) {
    float r = (y1 - y0) * 0.5f, cy = (y0 + y1) * 0.5f;
    float lx = x0 + r, rx = x1 - r;
    if (rx < lx) lx = rx = (x0 + x1) * 0.5f;
    for (int y = std::max(y0, 0); y < std::min(y1, h_); y++)
        for (int x = std::max(x0, 0); x < std::min(x1, w_); x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float dx = px < lx ? lx - px : px > rx ? px - rx : 0.0f, dy = py - cy;
            float cov = std::clamp(r - std::sqrt(dx * dx + dy * dy) + 0.5f, 0.0f, 1.0f);
            if (cov > 0) blend(x, y, rgb, cov * alpha);
        }
}

// A picture scaled to cover the box (bilinear), what overhangs cut off (at the
// sides: anchor_x of it on the left, 0.5 the middle)
void Frontend::picture(const Picture& p, int x0, int y0, int x1, int y1, float anchor_x) {
    if (p.w <= 0 || x1 <= x0 || y1 <= y0) return;
    float sc = std::max(float(x1 - x0) / p.w, float(y1 - y0) / p.h);
    float ox = (p.w * sc - (x1 - x0)) * anchor_x, oy = (p.h * sc - (y1 - y0)) * 0.5f;
    for (int y = std::max(y0, 0); y < std::min(y1, h_); y++) {
        float v = std::clamp((y - y0 + oy + 0.5f) / sc - 0.5f, 0.0f, p.h - 1.001f);
        int iv = int(v);
        float fv = v - iv;
        for (int x = std::max(x0, 0); x < std::min(x1, w_); x++) {
            float u = std::clamp((x - x0 + ox + 0.5f) / sc - 0.5f, 0.0f, p.w - 1.001f);
            int iu = int(u);
            float fu = u - iu;
            const uint32_t* q = &p.px[size_t(iv) * p.w + iu];
            uint32_t c[4] = {q[0], q[1], q[p.w], q[p.w + 1]};
            uint32_t out = 0xFF000000u;
            for (int sh = 0; sh <= 16; sh += 8) {
                float a = chan(c[0], sh) + (chan(c[1], sh) - chan(c[0], sh)) * fu;
                float b = chan(c[2], sh) + (chan(c[3], sh) - chan(c[2], sh)) * fu;
                out |= uint32_t(std::clamp(a + (b - a) * fv, 0.0f, 1.0f) * 255.0f + 0.5f) << sh;
            }
            px_[size_t(y) * w_ + x] = out;
        }
    }
}

// A picture with transparency, area-averaged down to w x h at x0, y0
void Frontend::sprite(const Picture& p, int x0, int y0, int w, int h) {
    if (p.w <= 0 || w <= 0 || h <= 0) return;
    float sx = float(p.w) / w, sy = float(p.h) / h;
    for (int y = std::max(0, y0); y < std::min(h_, y0 + h); y++)
        for (int x = std::max(0, x0); x < std::min(w_, x0 + w); x++) {
            int u0 = int((x - x0) * sx), u1 = std::max(u0 + 1, std::min(p.w, int((x - x0 + 1) * sx)));
            int v0 = int((y - y0) * sy), v1 = std::max(v0 + 1, std::min(p.h, int((y - y0 + 1) * sy)));
            float rgb[3] = {0, 0, 0}, a = 0;   // colours weighted by their alpha
            for (int v = v0; v < v1; v++)
                for (int u = u0; u < u1; u++) {
                    uint32_t c = p.px[size_t(v) * p.w + u];
                    float ca = chan(c, 24);
                    for (int m = 0; m < 3; m++) rgb[m] += chan(c, 16 - 8 * m) * ca;
                    a += ca;
                }
            if (a <= 0) continue;
            uint32_t c = 0;
            for (int m = 0; m < 3; m++) c |= uint32_t(std::clamp(rgb[m] / a, 0.0f, 1.0f) * 255.0f + 0.5f) << (16 - 8 * m);
            blend(x, y, c, a / float((u1 - u0) * (v1 - v0)));
        }
}

const Frontend::Picture& Frontend::pad_icon(const std::wstring& button) {
    auto it = pad_icons_.find(button);
    if (it != pad_icons_.end()) return it->second;
    std::wstring name = button;
    for (wchar_t& c : name) c = towlower(c);
    Picture& p = pad_icons_[button];
    load_image(L"res/pad/" + name + L".png", p.px, p.w, p.h);
    return p;
}

namespace {
constexpr uint32_t PAD_A = 0x6FD17A, PAD_B = 0xE8655A, PAD_Y = 0xE8C85A, PAD_X = 0x5AA8E8, KEYCAP = 0xB8C6D8;
}  // namespace

void Frontend::prompt(int& x, int y, int h, const wchar_t* pad_button, const wchar_t* key, const wchar_t* what) {
    auto U = [&](float v) { return int(v * u_ + 0.5f); };
    if (pad_prompts && pad_button && pad_icon(pad_button).w > 0) {   // the button's picture
        const Picture& icon = pad_icon(pad_button);
        int iw = h * icon.w / icon.h;
        sprite(icon, x, y, iw, h);
        x += iw + U(10);
    } else if (pad_prompts && pad_button) {   // no picture: a coloured disc or a grey pill with its name
        std::wstring b = pad_button;
        uint32_t c = b == L"A" ? PAD_A : b == L"B" ? PAD_B : b == L"Y" ? PAD_Y : b == L"X" ? PAD_X : KEYCAP;
        int bw = std::max(h, measure(MONO, U(17), b) + U(20));
        pill(x, y, x + bw, y + h, c, 1);
        text(MONO, U(17), b, x + bw / 2, y + h / 2 - U(9), 1, {0x111111, 0x111111});
        x += bw + U(10);
    } else if (key) {   // the key in a keycap
        int kw = std::max(h, measure(MONO, U(17), key) + U(22));
        fill(x, y, x + kw, y + h, 0x000000, 0.45f);
        frame(x, y, x + kw, y + h, std::max(1, U(1.5f)), KEYCAP, 0.75f);
        text(MONO, U(17), key, x + kw / 2, y + h / 2 - U(9), 1, {0xE0E8F2, 0xE0E8F2});
        x += kw + U(10);
    }
    if (what && *what) x += text(ITEM, U(21), what, x, y + h / 2 - U(13), 0, {0xC8D2E0, 0xC8D2E0}) + U(40);
}

int Frontend::prompt_width(int h, const wchar_t* pad_button, const wchar_t* key, const wchar_t* what) {
    auto U = [&](float v) { return int(v * u_ + 0.5f); };
    int wd = 0;
    const Picture* icon = pad_prompts && pad_button ? &pad_icon(pad_button) : nullptr;
    if (icon && icon->w > 0) wd = h * icon->w / icon->h + U(10);
    else if (icon) wd = std::max(h, measure(MONO, U(17), pad_button) + U(20)) + U(10);
    else if (key) wd = std::max(h, measure(MONO, U(17), key) + U(22)) + U(10);
    if (what && *what) wd += measure(ITEM, U(21), what) + U(40);
    return wd;
}

// Lines of at most max_w pixels, broken between words
std::vector<std::wstring> Frontend::wrap(Family f, int px, const std::wstring& s, int max_w) {
    std::vector<std::wstring> lines;
    std::wstring line, word;
    auto put = [&] {
        if (word.empty()) return;
        std::wstring longer = line.empty() ? word : line + L" " + word;
        if (!line.empty() && measure(f, px, longer) > max_w) {
            lines.push_back(line);
            line = word;
        } else {
            line = longer;
        }
        word.clear();
    };
    for (wchar_t c : s) {
        if (c == L' ') put();
        else word += c;
    }
    put();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// The logo, LOGO_H of the window high from LOGO_TOP, centred LOGO_FROM_RIGHT
// (1080p pixels) from the right edge: above the menu's column
constexpr float LOGO_TOP = 0.09f, LOGO_H = 0.28f, LOGO_FROM_RIGHT = 420;   // the logo and the entries as one block

// A picture blurred: averaged down by 16 in each direction and smoothed a
// few times (stretched back up by whoever draws it)
static void blur16(const uint32_t* src, int w, int h, std::vector<uint32_t>& out, int& bw, int& bh) {
    const int F = 16;
    bw = std::max(4, w / F);
    bh = std::max(4, h / F);
    std::vector<float> acc(size_t(bw) * bh * 3, 0.0f);
    for (int y = 0; y < bh * F && y < h; y++)
        for (int x = 0; x < bw * F && x < w; x++) {
            uint32_t c = src[size_t(y) * w + x];
            float* d = &acc[(size_t(y / F) * bw + x / F) * 3];
            for (int m = 0; m < 3; m++) d[m] += chan(c, 16 - 8 * m) / (F * F);
        }
    for (int pass = 0; pass < 3; pass++) {   // 3x3 box, three times
        std::vector<float> from = acc;
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++)
                for (int m = 0; m < 3; m++) {
                    float sum = 0;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            int xx = std::clamp(x + dx, 0, bw - 1), yy = std::clamp(y + dy, 0, bh - 1);
                            sum += from[(size_t(yy) * bw + xx) * 3 + m];
                        }
                    acc[(size_t(y) * bw + x) * 3 + m] = sum / 9;
                }
    }
    out.resize(size_t(bw) * bh);
    for (size_t i = 0; i < out.size(); i++) {
        uint32_t c = 0xFF000000u;
        for (int m = 0; m < 3; m++)
            c |= uint32_t(std::clamp(acc[i * 3 + m], 0.0f, 1.0f) * 255.0f + 0.5f) << (16 - 8 * m);
        out[i] = c;
    }
}

// The background picture covering the window, darker towards the right where
// the menu goes and at the bottom, with the logo; blurred for the options.
// With the backdrop moving on the GPU (motion_), the picture is left out:
// only the shading and the logo over transparency
void Frontend::backdrop_background() {
    uint32_t* keep = px_;
    const uint32_t base = motion_ ? 0u : 0xFF000000u;
    bg_.assign(size_t(w_) * h_, base);
    bg_blur_.assign(size_t(w_) * h_, base);
    if (!motion_) {
        px_ = bg_.data();
        picture(backdrop_, 0, 0, w_, h_, 0.0f);   // narrower windows lose its right side: the ship stays whole
        Picture blur;
        blur16(bg_.data(), w_, h_, blur.px, blur.w, blur.h);
        px_ = bg_blur_.data();
        picture(blur, 0, 0, w_, h_);
    }
    px_ = bg_blur_.data();
    fill(0, 0, w_, h_, 0x000000, 0.3f);
    px_ = bg_.data();
    shade_backdrop();
    px_ = keep;
}

void Frontend::set_motion(bool on) {
    if (motion_ == on) return;
    motion_ = on;
    bg_.clear();   // drawn again, with or without the picture
    bg_blur_.clear();
    dirty_ = true;
}

int Frontend::motion_backdrop() const {
    if (!motion_ || !active() || splash() || over_game()) return -1;
    if (screen_ == Screen::Options || screen_ == Screen::Records) return 1;
    return 0;
}

void Frontend::backdrop_pixels(int slot, const uint32_t*& px, int& w, int& h) {
    if (slot == 1 && backdrop_blur_.px.empty() && backdrop_.w > 0)
        blur16(backdrop_.px.data(), backdrop_.w, backdrop_.h, backdrop_blur_.px, backdrop_blur_.w, backdrop_blur_.h);
    const Picture& p = slot == 1 ? backdrop_blur_ : backdrop_;
    px = p.px.empty() ? nullptr : p.px.data();
    w = p.w;
    h = p.h;
}

// No background picture yet (Sega Rally 2): a drawn one, so the menus keep
// the layout of the other ports (the column at the right under the logo).
// Dusk over a dirt track: a deep blue sky going orange at the horizon, the
// dark ground below it, and a few streaks of speed across it
void Frontend::placeholder_backdrop() {
    const int W = 1920, H = 1080;
    backdrop_.w = W, backdrop_.h = H;
    backdrop_.px.assign(size_t(W) * H, 0xFF000000u);
    auto mix = [](uint32_t a, uint32_t b, float t) {
        uint32_t out = 0xFF000000u;
        for (int sh = 0; sh <= 16; sh += 8) {
            float v = float((a >> sh) & 255) * (1 - t) + float((b >> sh) & 255) * t;
            out |= uint32_t(std::clamp(v, 0.0f, 255.0f)) << sh;
        }
        return out;
    };
    const float horizon = 0.62f;
    for (int y = 0; y < H; y++) {
        float fy = float(y) / H;
        uint32_t row = fy < horizon ? mix(0x0B1530, 0xE07A2A, std::pow(fy / horizon, 2.2f))
                                    : mix(0x4A2A14, 0x0A0604, std::pow((fy - horizon) / (1 - horizon), 0.6f));
        for (int x = 0; x < W; x++) backdrop_.px[size_t(y) * W + x] = row;
    }
    // the glow of the sun just under the horizon, at the left
    for (int y = 0; y < int(horizon * H); y++)
        for (int x = 0; x < W; x++) {
            float dx = (x - W * 0.28f) / (W * 0.45f), dy = (y - horizon * H) / (H * 0.35f);
            float g = std::max(0.0f, 1 - std::sqrt(dx * dx + dy * dy));
            uint32_t& c = backdrop_.px[size_t(y) * W + x];
            c = mix(c, 0xFFC46A, 0.55f * g * g);
        }
    // speed streaks over the ground
    uint32_t seed = 12345;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1u << 24); };
    for (int k = 0; k < 140; k++) {
        int y = int((horizon + 0.02f + 0.36f * rnd()) * H), x0 = int(rnd() * W), len = int((0.05f + 0.25f * rnd()) * W);
        float a = 0.05f + 0.12f * rnd();
        for (int x = x0; x < std::min(W, x0 + len); x++) {
            uint32_t& c = backdrop_.px[size_t(y) * W + x];
            c = mix(c, 0xD8A070, a * (1 - float(x - x0) / len));
        }
    }
}

void Frontend::shade_backdrop() {
    hgrad(int(w_ * 0.42f), 0, int(w_ * 0.66f), h_, 0x02040A, 0.0f, 0.55f);
    fill(int(w_ * 0.66f), 0, w_, h_, 0x02040A, 0.55f);
    hgrad(int(w_ * 0.66f), 0, w_, h_, 0x02040A, 0.0f, 0.5f);
    vgrad(0, int(h_ * 0.72f), w_, h_, 0x000000, 0.0f, 0.6f);
    if (logo_.w > 0) {
        int lh = int(LOGO_H * h_), lw = lh * logo_.w / logo_.h;
        sprite(logo_, int(w_ - LOGO_FROM_RIGHT * u_) - lw / 2, int(LOGO_TOP * h_), lw, lh);
    } else {   // no logo picture yet: the title in letters, where the logo goes
        auto U = [&](float v) { return int(v * u_ + 0.5f); };
        int cx = int(w_ - LOGO_FROM_RIGHT * u_), top = int(LOGO_TOP * h_) + U(40);
        text(TITLE, U(120), L"SEGA RALLY 2", cx, top, 1, {GOLD_TOP, GOLD_BOTTOM, EDGE, U(6), 1, U(8)});
        fill(cx - U(300), top + U(170), cx + U(300), top + U(174), 0xF0F4FF, 0.85f);
        text(MONO, U(32), L"C H A M P I O N S H I P", cx, top + U(188), 1, {0xFFFFFF, 0xD8E4F4, EDGE, U(2)});
    }
}

double Frontend::menu_clock() const {
    static const double fps = getenv("SR2_MENU_FPS") ? atof(getenv("SR2_MENU_FPS")) : 0;
    return fps > 0 ? double(menu_frames_) / fps : now_seconds();
}

void Frontend::slide_to(Screen s, int sel) {
    if (slide_from_ >= 0) return;
    slide_from_ = menu_clock();
    slide_out_ = true;
    slide_to_ = s;
    slide_sel_ = sel;
    dirty_ = true;
}

void Frontend::glide(Glide& g, long key, int y, int h, int& gy, int& gh) {
    constexpr double GLIDE = 0.13;
    double now = menu_clock();
    auto at = [&](float& cy, float& ch) {
        float k = g.t0 < 0 ? 1.0f : float(std::clamp((now - g.t0) / GLIDE, 0.0, 1.0));
        float e = 1 - (1 - k) * (1 - k) * (1 - k);
        cy = g.from + (g.to - g.from) * e;
        ch = g.from_h + (g.to_h - g.from_h) * e;
        return k;
    };
    if (g.key != key) {
        g = Glide{float(y), float(y), float(h), float(h), -1, key};
    } else if (float(y) != g.to || float(h) != g.to_h) {   // a new target: from where it is now (a fast repeat retargets)
        float cy, ch;
        at(cy, ch);
        g.from = cy, g.from_h = ch, g.to = float(y), g.to_h = float(h), g.t0 = now;
    }
    float cy, ch;
    if (at(cy, ch) >= 1) g.t0 = -1;
    else gliding_ = true;
    gy = int(std::lround(cy)), gh = int(std::lround(ch));
}

void Frontend::finish_slide() {
    if (slide_from_ < 0) return;
    if (slide_out_) {
        open(slide_to_);
        selected_ = slide_sel_;
    }
    slide_from_ = -1;
    slide_out_ = slide_intro_ = false;
    dirty_ = true;
}

int Frontend::slide_offset(int i) const {
    if (slide_from_ < 0) return 0;
    double t = menu_clock() - slide_from_;
    float in;
    if (slide_out_) {
        float x = float(std::clamp((t - SLIDE_OUT_GAP * i) / SLIDE_OUT, 0.0, 1.0));
        in = 1 - x * x * x;
    } else {
        double gap = slide_intro_ ? INTRO_GAP : SLIDE_IN_GAP, len = slide_intro_ ? INTRO_IN : SLIDE_IN;
        float x = 1 - float(std::clamp((t - gap * i) / len, 0.0, 1.0));
        in = 1 - x * x * x * x * x;
    }
    return int((1 - in) * 820 * u_);   // the stripe and the longest entry off the right edge
}

// Main, Play, Extra and Debug on the backdrop: the entries in a
// column at the right under the logo, the description under them; the
// records at the top left, the credits at the bottom
void Frontend::draw_side_menu(const std::vector<Item>& list) {
    auto U = [&](float v) { return int(v * u_ + 0.5f); };
    int w = w_, h = h_;
    int margin = U(110), col_w = U(720);
    int tx = w - margin, band0 = w - margin - col_w, band1 = w;
    int y0 = int((LOGO_TOP + LOGO_H) * h_) + U(36);
    const wchar_t* sub = screen_ == Screen::Debug ? L"DEBUG:  PRACTICE RACE" : nullptr;
    if (sub) {
        text(MONO, U(24), sub, tx, y0, 2, Style{0xFF8A7A, 0xD8402E});
        y0 += U(50);
    }
    if (screen_ == Screen::Main) {   // how far the player has got, at the top left
        int got = 0;
        for (int64_t t : records.unlocked) got += t != 0;
        int best = records.stats.best_champ_position;
        std::vector<std::pair<std::wstring, std::wstring>> chips = {
            {L"ACHIEVEMENTS", std::to_wstring(got) + L"/" + std::to_wstring(achievement_table().size())}};
        if (best > 0) chips.push_back({L"BEST CHAMPIONSHIP", ordinal(best)});
        int cy0 = U(56), ch = U(56);
        int x = U(70);
        for (const auto& c : chips) {
            int lw = measure(ITEM, U(21), c.first), vw = measure(ITEM, U(26), c.second);
            int cw = U(22) + lw + U(12) + vw + U(22);
            fill(x, cy0, x + cw, cy0 + ch, PANEL, 0.8f);
            frame(x, cy0, x + cw, cy0 + ch, std::max(1, U(1.5f)), SABER, 0.45f);
            text(ITEM, U(21), c.first, x + U(22), cy0 + ch / 2 - U(13), 0, {PALE_TOP, PALE_BOTTOM});
            text(ITEM, U(26), c.second, x + cw - U(22), cy0 + ch / 2 - U(16), 2, {GOLD_TOP, GOLD_BOTTOM});
            x += cw + U(16);
        }
    }

    // The entries share what is left of the height, the description under them
    int n = int(list.size());
    int spacing = std::clamp((h - U(310) - y0) / std::max(1, n), U(44), U(76));   // room for the description under the chosen one
    int item_px = std::min(U(58), spacing * 72 / 100);
    int y = y0;
    const bool on_main = screen_ == Screen::Main || screen_ == Screen::Play;
    auto row_sp = [&](int i) {   // the main menu: PLAY big, the others smaller under it
        return on_main && list[i].id == PLAY ? U(104) : on_main ? U(62) : spacing;
    };
    if (selected_ >= 0 && selected_ < n) {   // the chosen one's ribbon, gliding there from the last one
        const Item& it = list[selected_];
        int ty = y0;
        for (int i = 0; i < selected_; i++) ty += row_sp(i);
        int gy, gsp;
        glide(glide_menu_, long(screen_) * 100003 + h_, ty, row_sp(selected_), gy, gsp);
        const int off = slide_offset(selected_), ix = tx + off;
        float a = it.enabled ? 0.85f : 0.3f;
        int slant = gsp * 45 / 100, x0 = on_main ? ix - U(470) : band0 + U(120) + off;   // clear of the car
        for (int yy = gy; yy < gy + gsp; yy++) {
            int xs = x0 + (gy + gsp - yy) * slant / std::max(1, gsp);
            fill(xs, yy, w, yy + 1, SABER, a * 0.55f);
            hgrad(xs, yy, w, yy + 1, SABER, 0.0f, a * 0.45f);
            fill(xs - U(30), yy, xs - U(18), yy + 1, SABER, a * 0.9f);
        }
        int edge = std::max(3, U(7));
        fill(w - edge + off, gy, w + off, gy + gsp, 0xFF6A5A, it.enabled ? 1.0f : 0.5f);
    }
    for (int i = 0; i < n; i++) {
        const Item& it = list[i];
        const int off = slide_offset(i), ix = tx + off;   // (sliding: further right)
        bool sel = i == selected_;
        const bool hero = on_main && it.id == PLAY;
        const int sp = row_sp(i);
        const int px = hero ? U(92) : on_main ? U(46) : item_px;
        bool debug_row = it.id >= DEBUG && it.id <= DEBUG_GO;   // DEBUG and its rows in red: not part of the game
        const Style st = !it.enabled ? Style{DIM, DIM, EDGE, U(2), 0.9f, U(3)}
                         : sel       ? Style{GOLD_TOP, GOLD_BOTTOM, EDGE, U(2), 1, U(3)}
                         : debug_row ? Style{0xFF8A7A, 0xD8402E, EDGE, U(2), 0.95f, U(3)}
                                     : Style{PALE_TOP, PALE_BOTTOM, EDGE, U(2), 0.95f, U(3)};
        int ty = y + sp / 2 - px * 75 / 100;   // the capitals centred in the row (TITLE: they start 0.42 down, 0.67 high)
        int tw = text(TITLE, px, it.label, ix, ty, 2, st);
        if (sel && on_main && it.enabled && !gliding_)   // a chevron before the chosen one, once the ribbon is there
            triangle(ix - tw - U(28), y + sp / 2, U(22), +1, 0xFFE08A, 0.95f);
        if (!it.value.empty() && screen_ == Screen::Debug) {   // DEBUG's choices: between arrows, left of the name
            int vx = ix - tw - U(40), vy = y + sp / 2;
            triangle(vx, vy, U(18), +1, sel ? 0xFFE08A : 0xB0B8C8, 0.9f);
            int vw = text(ITEM, U(30), it.value, vx - U(22), vy - U(19), 2, {GOLD_TOP, GOLD_BOTTOM, EDGE, U(2)});
            triangle(vx - U(44) - vw, vy, U(18), -1, sel ? 0xFFE08A : 0xB0B8C8, 0.9f);
        } else if (!it.value.empty())   // e.g. COMING SOON
            text(MONO, U(20), it.value, ix - tw - U(sel && on_main ? 40 : 28), y + sp / 2 - U(12), 2, {0x7FB0D8, 0x6090B8});
        hit_.push_back({band0, y, band1, y + sp});
        values_.push_back({-1, 0});
        y += sp;
        if (sel) {   // its description right under it, the entries below move down
            y += U(4);
            for (const std::wstring& line : wrap(ITEM, U(24), it.hint, col_w - U(20))) {
                text(ITEM, U(24), line, ix, y, 2, {HINT, HINT});
                y += U(32);
            }
            y += U(10);
        }
    }

    // The bottom: the original game's credit at the left, the port's in the
    // middle (main menu), the keys at the right
    vgrad(0, h - U(130), w, h, 0x000000, 0.0f, 0.85f);
    int fy = h - U(58);
    text(ITEM, U(19), L"ORIGINAL ARCADE GAME © 1998 SEGA", U(70), fy - U(14), 0, {0xA8B4C4, 0x8E9AAC});
    text(ITEM, U(19), L"SEGA RALLY 2 © SEGA", U(70), fy + U(14), 0, {0x8E9AAC, 0x7A889A});
    if (credit_.w > 0) {   // on every side menu
        int lw = U(250), lh = lw * credit_.h / credit_.w, ly = h - U(34) - lh;
        sprite(credit_, (w - lw) / 2, ly, lw, lh);
        text(ITEM, U(17), L"PC REMASTER BY", w / 2, ly - U(26), 1, {FOOT, FOOT});
    }
    // The keys at the right, each in a keycap (or the gamepad's button), ending at the right edge
    struct Key { const wchar_t *pad, *key, *what; };
    std::vector<Key> keys = {{L"A", L"ENTER", L"SELECT"}};
    if (screen_ != Screen::Main) keys.push_back({L"B", L"ESC", L"BACK"});
    int kh = U(36), ky = fy + U(12) - kh / 2, x = w - U(70) + U(40);
    for (const Key& k : keys) x -= prompt_width(kh, k.pad, k.key, k.what);
    for (const Key& k : keys) prompt(x, ky, kh, k.pad, k.key, k.what);
}

// ---- Splash screens ----

namespace {
constexpr double SPLASH_GAP = 0.25;    // black before each one
constexpr double SPLASH_IN = 0.7;      // fading in
constexpr double SPLASH_HOLD = 1.8;    // on screen
constexpr double SPLASH_OUT = 0.7;     // fading out
constexpr double SPLASH_SKIP = 0.25;   // fading out when hurried
constexpr double MENU_IN = 0.8;        // the main menu fading in after them
}  // namespace

void Frontend::skip_splash() {
    if (video_on_) {   // faded out with its sound, unless it is going anyway
        if (skip_from_ < 0 && video_end_ < 0) skip_level_ = video_level_, skip_from_ = now_seconds();
        return;
    }
    if (skip_from_ >= 0) {   // already going: the next one goes too
        skip_next_ = true;
        return;
    }
    double t = now_seconds() - splash_from_;
    if (t >= SPLASH_GAP + SPLASH_IN + SPLASH_HOLD) return;   // fading out anyway
    skip_level_ = float(std::clamp((t - SPLASH_GAP) / SPLASH_IN, 0.0, 1.0));
    skip_from_ = now_seconds();
}

bool Frontend::draw_splash(std::vector<uint32_t>& out) {
    if (video_on_) return draw_splash_video(out);
    double now = now_seconds();
    float level = 0;   // brightness: 0 black .. 1 the picture
    for (;;) {
        double t = now - splash_from_;
        bool ended;
        if (skip_from_ >= 0) {
            double k = (now - skip_from_) / SPLASH_SKIP;
            level = skip_level_ * float(1 - std::min(k, 1.0));
            ended = k >= 1;
        } else {
            double out_at = SPLASH_GAP + SPLASH_IN + SPLASH_HOLD;
            level = float(std::clamp(std::min((t - SPLASH_GAP) / SPLASH_IN, 1 - (t - out_at) / SPLASH_OUT), 0.0, 1.0));
            ended = t >= out_at + SPLASH_OUT;
        }
        if (!ended) break;
        if (++splash_on_ >= int(splash_.size())) return false;
        splash_from_ = now;
        skip_from_ = -1;
        if (skip_next_) {   // pressed twice: this one is gone before it shows
            skip_next_ = false;
            skip_from_ = now - SPLASH_SKIP;
            skip_level_ = 0;
        }
    }
    // The picture as big as fits the window, white around it, scaled once per size
    const Picture& p = splash_[splash_on_];
    if (splash_px_on_ != splash_on_ || splash_px_w_ != w_ || splash_px_h_ != h_) {
        splash_px_.assign(size_t(w_) * h_, 0xFFFFFFFFu);
        float sc = std::min(float(w_) / p.w, float(h_) / p.h);
        int pw = std::max(1, int(p.w * sc + 0.5f)), ph = std::max(1, int(p.h * sc + 0.5f));
        int x0 = (w_ - pw) / 2, y0 = (h_ - ph) / 2;
        px_ = splash_px_.data();
        picture(p, x0, y0, x0 + pw, y0 + ph);
        splash_px_on_ = splash_on_, splash_px_w_ = w_, splash_px_h_ = h_;
    }
    out.resize(splash_px_.size());
    uint32_t k = uint32_t(level * 256 + 0.5f);
    if (k >= 256) out = splash_px_;
    else
        for (size_t i = 0; i < out.size(); i++) {   // towards black, opaque
            uint32_t c = splash_px_[i];
            out[i] = 0xFF000000u | ((((c & 0xFF00FFu) * k) >> 8) & 0xFF00FFu) | ((((c & 0x00FF00u) * k) >> 8) & 0x00FF00u);
        }
    px_ = out.data();
    return true;
}

// The video: in from black as it starts, played to the end (Windows plays the
// sound), its last picture faded out. Hurried: picture and sound fade out
// together. As big as fits the window, its own background colour around it
bool Frontend::draw_splash_video(std::vector<uint32_t>& out) {
    double now = now_seconds(), t = now - splash_from_;
    if (video_.failed() || (t > 5 && video_.position() <= 0)) return false;   // it will not play: to the menu
    if (video_end_ < 0 && video_.ended()) video_end_ = now;
    float level = float(std::clamp(t / SPLASH_IN, 0.0, 1.0));
    if (skip_from_ >= 0) {
        double k = std::min((now - skip_from_) / SPLASH_SKIP, 1.0);
        if (k >= 1) return false;
        level = skip_level_ * float(1 - k);
        video_.set_volume(float(settings.volume) / 10.0f * float(1 - k));
    } else if (video_end_ >= 0) {
        double k = (now - video_end_) / SPLASH_OUT;
        if (k >= 1) return false;
        level = std::min(level, float(1 - k));
    }
    video_level_ = level;

    out.assign(size_t(w_) * h_, 0xFF000000u);
    px_ = out.data();
    int vw = 0, vh = 0;
    if (!video_.size(vw, vh)) return true;   // still loading: black
    float sc = std::min(float(w_) / vw, float(h_) / vh);
    int pw = std::max(1, int(vw * sc + 0.5f)), ph = std::max(1, int(vh * sc + 0.5f));
    bool resized = pw != video_pw_ || ph != video_ph_;
    if (video_.frame(pw, ph, video_px_, resized)) video_pw_ = pw, video_ph_ = ph;
    if (video_px_.size() != size_t(video_pw_) * video_ph_ || video_pw_ <= 0 || video_pw_ > w_ || video_ph_ > h_) return true;
    uint32_t k = uint32_t(level * 256 + 0.5f);
    auto dim = [k](uint32_t c) {   // towards black, opaque
        if (k >= 256) return c | 0xFF000000u;
        return 0xFF000000u | ((((c & 0xFF00FFu) * k) >> 8) & 0xFF00FFu) | ((((c & 0x00FF00u) * k) >> 8) & 0x00FF00u);
    };
    uint32_t around = dim(video_px_[video_pw_ / 2]);   // the middle of its top edge: its background
    int x0 = (w_ - video_pw_) / 2, y0 = (h_ - video_ph_) / 2;
    for (int y = 0; y < h_; y++) {
        uint32_t* row = &out[size_t(y) * w_];
        if (y < y0 || y >= y0 + video_ph_) {
            std::fill(row, row + w_, around);
            continue;
        }
        const uint32_t* src = &video_px_[size_t(y - y0) * video_pw_];
        std::fill(row, row + x0, around);
        for (int x = 0; x < video_pw_; x++) row[x0 + x] = dim(src[x]);
        std::fill(row + x0 + video_pw_, row + w_, around);
    }
    return true;
}

// ---- The settings page ----

namespace {
constexpr uint32_t PAGE_TEXT = 0xEEF3FA, PAGE_DIM = 0x8A98AC, PAGE_FAINT = 0x56627A, PAGE_LINE = 0x3A4458;
constexpr uint32_t GROUP_RED = 0xD8564A, VALUE = 0xE8D278, TAG = 0x26324A, TAG_TEXT = 0xAFC6E6;

std::wstring title_case(const wchar_t* s) {   // DISPLAY -> Display
    std::wstring t = s;
    for (size_t i = 1; i < t.size(); i++)
        if (t[i - 1] != L' ') t[i] = wchar_t(towlower(t[i]));
    return t;
}
}  // namespace

void Frontend::draw_options_page(const std::vector<Item>& list, bool over_game) {
    auto U = [&](float v) { return int(v * u_ + 0.5f); };
    const int w = w_, h = h_, n = int(list.size());
    const int n_opts = n - 2;   // list: the sections, the options of the open one, BACK
    // The page darkens what is behind it: the blurred backdrop, or the paused game
    // (the game's HUD must not show through: blurred under the pause settings, else nearly black)
    fill(0, 0, w, h, over_game && !pause_blur ? 0x000000 : PANEL, over_game && !pause_blur ? 0.9f : 0.6f);

    // Columns: as wide as the window allows, up to 1728 units (1080p: a 96 pixel margin)
    const int mx = std::max(U(56), (w - U(1728)) / 2);
    const int wide_u = int(float(w - 2 * mx) / u_);   // the content's width in 1080p units
    const bool wide = wide_u >= 1600;
    const int rail_w = U(wide ? 300.0f : 250.0f), gap = U(wide ? 48.0f : 32.0f), panel_w = U(wide ? 600.0f : 430.0f);
    const int rail_x = mx, rows_x0 = mx + rail_w + gap, panel_x0 = w - mx - panel_w, rows_x1 = panel_x0 - gap;
    const int top = U(196), bottom = h - U(100);   // the columns, between the title and the key help

    // Title, the red mark under it, where this is, and the logo at the top right
    text(TITLE, U(80), over_game ? L"SETTINGS" : L"OPTIONS", mx, U(34), 0, {0xFFFFFF, 0xE2EAF4, 0, 0, 1, U(3)});
    fill(mx + U(2), U(134), mx + U(74), U(139), SABER, 1);
    std::wstring crumb = std::wstring(over_game ? L"Pause" : L"Main menu") + L"  ›  " +
                         (over_game ? L"Settings" : L"Options") + L"  ›  " + title_case(TAB_NAMES[tab_]);
    text(ROW_MEDIUM, U(24), crumb, mx + U(92), U(120), 0, {PAGE_DIM, PAGE_DIM});
    if (logo_.w > 0) {
        int lw = U(wide ? 300.0f : 240.0f), lh = lw * logo_.h / logo_.w;
        sprite(logo_, w - mx - lw, U(26), lw, lh);
    }

    hit_.assign(size_t(n), RECT{});
    values_.assign(size_t(n), ValueArea{-1, 0});

    // The sections, one under the other; the open one with a red band
    const int sec_h = U(92);
    tab_rects_.clear();
    {   // the open section's ribbon, gliding there from the last one
        int ty = top + tab_ * sec_h;
        int gy, gh;
        glide(glide_tab_, long(options_from_pause_) * 100003 + h_, ty - U(8), sec_h - U(8), gy, gh);
        RECT r{rail_x - U(20), gy, rail_x + rail_w, gy + gh};
        int sl = gh * 30 / 100;
        ribbon(r.left - sl / 2, r.top, r.right, r.bottom, sl, 0.85f, 0.0f, U(10), U(8), 1);
        if (!gliding_) triangle(rail_x + U(8), ty + U(28), U(18), +1, 0xFFE08A, 0.95f);
    }
    for (int t = 0; t < TAB_COUNT; t++) {
        int y = top + t * sec_h;
        bool open_tab = t == tab_;
        RECT r{rail_x - U(20), y - U(8), rail_x + rail_w, y + sec_h - U(16)};
        if (open_tab) {   // (its ribbon drawn above)
        } else if (selected_ == 0 && hovered_ == 0 && mouse_y_ >= r.top && mouse_y_ < r.bottom) {
            hgrad(r.left, r.top, r.right, r.bottom, 0xFFFFFF, 0.08f, 0.0f);   // under the pointer
        }
        text(TITLE, U(44), TAB_NAMES[t], rail_x + U(26), y - U(6), 0,
             {open_tab ? 0xFFFFFFu : PAGE_DIM, open_tab ? 0xE2EAF4u : PAGE_DIM, 0, 0, 1, U(2)});
        text(BODY, U(17), TAB_ABOUT[t], rail_x + U(28), y + U(46), 0,
             {open_tab ? 0xD2DAE6u : PAGE_FAINT, open_tab ? 0xD2DAE6u : PAGE_FAINT});
        tab_rects_.push_back(r);
    }
    hit_[0] = {rail_x - U(20), top - U(8), rail_x + rail_w, top + TAB_COUNT * sec_h - U(16)};
    {   // BACK, under the sections
        int y = top + TAB_COUNT * sec_h + U(28);
        bool sel = selected_ == n - 1;
        RECT r{rail_x - U(20), y - U(6), rail_x + rail_w, y + U(58)};
        if (sel) {
            int hh = r.bottom - r.top, sl = hh * 30 / 100;
            ribbon(r.left - sl / 2, r.top, r.right, r.bottom, sl, 0.85f, 0.0f, U(10), U(8), 1);
        }
        triangle(rail_x + U(20), y + U(26), U(16), -1, sel ? GOLD_BOTTOM : PAGE_DIM, 1);
        text(TITLE, U(40), L"BACK", rail_x + U(40), y - U(2), 0,
             sel ? Style{GOLD_TOP, GOLD_BOTTOM, 0, 0, 1, U(2)} : Style{PAGE_DIM, PAGE_DIM, 0, 0, 1, U(2)});
        hit_[n - 1] = r;
    }

    // The options in their groups. Rows that do not fit scroll, keeping the chosen one in view
    const int row_h = U(46), head_h = U(38), head_gap = U(10), label_px = U(30);
    std::vector<const wchar_t*> group(size_t(std::max(0, n_opts)), nullptr);   // the heading over each row, if any
    std::vector<const wchar_t*> group_name(size_t(std::max(0, n_opts)), L"");  // the group each row is in
    const wchar_t* current = L"";
    for (int k = 0; k < n_opts; k++) {
        if (const wchar_t* g = group_of(list[1 + k].id)) current = group[k] = g;
        group_name[k] = current;
    }
    auto height = [&](int k, bool first) {   // a row with its heading (the first one shown always has one)
        bool head = first ? *group_name[k] != 0 : group[k] != nullptr;
        return row_h + (head ? head_h + (first ? 0 : head_gap) : 0);
    };
    auto fits = [&](int from, int to) {
        int sum = 0;
        for (int k = from; k <= to; k++) sum += height(k, k == from);
        return sum <= bottom - top;
    };
    int k_sel = selected_ - 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, n_opts - 1));
    if (k_sel >= 0 && k_sel < n_opts) {
        if (k_sel < scroll_) scroll_ = k_sel;
        while (scroll_ < k_sel && !fits(scroll_, k_sel)) scroll_++;
    }
    while (scroll_ > 0 && fits(scroll_ - 1, n_opts - 1)) scroll_--;   // no room left empty at the bottom
    options_scroll_ = n_opts > 0 && (scroll_ > 0 || !fits(0, n_opts - 1));
    more_up_ = more_down_ = RECT{};

    const int text_h = mask(ROW, label_px, L"Ag", 0).text_h;
    const Style gold{GOLD_TOP, GOLD_BOTTOM, 0, 0, 1, U(2)};
    bool row_ribbon = false;
    if (k_sel >= scroll_ && k_sel < n_opts) {   // the chosen row's ribbon, gliding there from the last one
        int ry = top, k = scroll_;
        for (; k <= k_sel; k++) {
            if (ry + height(k, k == scroll_) > bottom) break;
            ry += height(k, k == scroll_);
        }
        if (k > k_sel) {   // shown
            ry -= row_h;
            const Item& it = list[1 + k_sel];
            int gy, gh;
            glide(glide_row_, long(tab_) * 100003 + h_ + long(options_from_pause_) * 7, ry, row_h, gy, gh);
            int sl = row_h * 40 / 100;
            ribbon(rows_x0 - U(30) - sl / 2, gy, rows_x1 + U(18) - sl, gy + gh, sl, 0.45f, 0.75f, U(6), U(5), it.enabled ? 1.0f : 0.4f);
            if (!gliding_) triangle(rows_x0 - U(16), ry + row_h / 2, U(14), +1, 0xFFE08A, it.enabled ? 0.95f : 0.4f);
            row_ribbon = true;
        }
    }
    if (!row_ribbon) glide_row_.key = -1;   // next time straight there
    int y = top, last_shown = scroll_ - 1;
    for (int k = scroll_; k < n_opts; k++) {
        if (y + height(k, k == scroll_) > bottom) break;
        const Item& it = list[1 + k];
        const wchar_t* head = k == scroll_ ? (*group_name[k] ? group_name[k] : nullptr) : group[k];
        if (head) {
            if (k != scroll_) y += head_gap;
            int gw = text(ROW_MEDIUM, U(20), head, rows_x0, y + U(6), 0, {GROUP_RED, GROUP_RED});
            fill(rows_x0 + gw + U(14), y + U(20), rows_x1, y + U(20) + std::max(1, U(1)), PAGE_LINE, 1);
            y += head_h;
        }
        bool sel = 1 + k == selected_;
        // (the chosen one's ribbon drawn above)
        int ty = y + (row_h - text_h) / 2, cy = y + row_h / 2;
        uint32_t lc = it.enabled ? PAGE_TEXT : PAGE_FAINT;
        int lw = text(ROW, label_px, it.label, rows_x0, ty, 0, {lc, lc, 0, 0, 1, U(2)});
        if (it.id == OPT_RT || it.id == OPT_REFLECT || it.id == OPT_GI) {   // needs a graphics card with ray tracing
            int tx = rows_x0 + lw + U(14), tw = measure(ROW, U(15), L"RT") + U(16);
            fill(tx, cy - U(11), tx + tw, cy + U(11), TAG, 1);
            text(ROW, U(15), L"RT", tx + tw / 2, cy - U(10), 1, {TAG_TEXT, TAG_TEXT});
        }
        int right = rows_x1 - U(20), vx0;
        if (it.id == OPT_VOLUME || it.id == OPT_MUSIC) {   // ten segments
            int seg_w = U(15), sgap = U(5), seg_h = U(20);
            vx0 = right - 10 * seg_w - 9 * sgap;
            for (int s = 0; s < 10; s++) {
                int x0 = vx0 + s * (seg_w + sgap);
                bool lit = s < (it.id == OPT_MUSIC ? settings.music : settings.volume);
                uint32_t c = lit ? mix(0xBFE8FF, SABER, float(s) / 9) : 0x2A3240;
                fill(x0 - 1, cy - seg_h / 2 - 1, x0 + seg_w + 1, cy + seg_h / 2 + 1, 0x000000, 0.8f);
                fill(x0, cy - seg_h / 2, x0 + seg_w, cy + seg_h / 2, c, sel || !lit ? 1.0f : 0.8f);
            }
        } else {
            uint32_t vc = it.enabled ? VALUE : PAGE_FAINT;
            vx0 = right - text(ROW, label_px, it.value, right, ty, 2, sel && it.enabled ? gold : Style{vc, vc, 0, 0, 1, U(2)});
        }
        if (sel && it.enabled) {
            triangle(vx0 - U(20), cy, U(14), -1, GOLD_BOTTOM, 1);
            triangle(right + U(13), cy, U(14), +1, GOLD_BOTTOM, 1);
        }
        hit_[1 + k] = {rows_x0 - U(18), y, rows_x1 + U(4), y + row_h};
        values_[1 + k] = {vx0 - U(40), (vx0 + right) / 2};
        y += row_h;
        last_shown = k;
    }
    if (scroll_ > 0) {   // more rows above / below: arrows past the right end of the rows
        triangle(rows_x1 + U(30), top + U(14), U(18), -2, GOLD_BOTTOM, 1);
        more_up_ = {rows_x1 + U(10), top - U(8), rows_x1 + U(52), top + U(36)};
    }
    if (last_shown < n_opts - 1) {
        triangle(rows_x1 + U(30), bottom - U(14), U(18), 2, GOLD_BOTTOM, 1);
        more_down_ = {rows_x1 + U(10), bottom - U(36), rows_x1 + U(52), bottom + U(8)};
    }

    // The panel at the right: what the chosen row is and does
    const int px1 = w - mx, pad = U(wide ? 36.0f : 26.0f);
    fill(panel_x0, top - U(8), px1, bottom, PANEL, over_game ? 0.95f : 0.62f);
    frame(panel_x0, top - U(8), px1, bottom, std::max(1, U(1)), PAGE_LINE, 1);
    fill(panel_x0, top - U(8), px1, top - U(8) + std::max(2, U(4)), SABER, 1);
    if (selected_ >= 0 && selected_ < n) {
        const Item& it = list[selected_];
        bool section = it.id == TABS, back = it.id == BACK;
        int x = panel_x0 + pad, py = top + U(22), inner = px1 - pad - x;
        const wchar_t* kicker = section ? L"SECTION" : back || n_opts <= 0 ? L"" : group_name[std::clamp(selected_ - 1, 0, n_opts - 1)];
        if (kicker && *kicker) text(ROW_MEDIUM, U(20), kicker, x, py, 0, {GROUP_RED, GROUP_RED});
        py += U(30);
        std::wstring title = section ? std::wstring(TAB_NAMES[tab_]) : it.label;
        int title_px = U(60);
        while (title_px > U(36) && measure(TITLE, title_px, title) > inner) title_px -= U(4);
        text(TITLE, title_px, title, x, py, 0, {0xFFFFFF, 0xE2EAF4, 0, 0, 1, U(3)});
        py += title_px + U(22);
        if (!section && !back && !it.value.empty() && it.id != OPT_VOLUME && it.id != OPT_MUSIC) {
            text(ROW, U(34), it.value, x, py, 0, it.enabled ? gold : Style{PAGE_FAINT, PAGE_FAINT});
            py += U(52);
        } else if (it.id == OPT_VOLUME || it.id == OPT_MUSIC) {
            text(ROW, U(34), std::to_wstring(it.id == OPT_MUSIC ? settings.music : settings.volume) + L" / 10", x, py, 0, gold);
            py += U(52);
        }
        fill(x, py, px1 - pad, py + std::max(1, U(1)), PAGE_LINE, 1);
        py += U(24);
        std::wstring about = section ? std::wstring(TAB_ABOUT[tab_]) + L".  " + it.hint : it.hint;
        for (const std::wstring& line : wrap(BODY, U(24), about, inner)) {
            if (py + U(34) > bottom - U(16)) break;
            text(BODY, U(24), line, x, py, 0, {0xD6DEEA, 0xD6DEEA});
            py += U(34);
        }
    }

    // The key help along the bottom
    fill(0, h - U(76), w, h, 0x000000, 0.5f);
    fill(0, h - U(76), w, h - U(76) + std::max(1, U(1)), PAGE_LINE, 1);
    struct Key { const wchar_t *pad, *key, *what; };
    std::vector<Key> keys = pad_prompts
        ? std::vector<Key>{{L"LS", nullptr, L"SELECT / CHANGE"}, {L"LB", nullptr, L""}, {L"RB", nullptr, L"SECTION"},
                           {L"B", nullptr, L"BACK"}}
        : std::vector<Key>{{nullptr, L"UP / DOWN", L"SELECT"}, {nullptr, L"LEFT / RIGHT", L"CHANGE"},
                           {nullptr, L"Q / E", L"SECTION"}, {nullptr, L"ESC", L"BACK"}};
    int kh = U(34), kx = mx;
    for (const Key& k : keys) prompt(kx, h - U(38) - kh / 2, kh, k.pad, k.key, k.what);
}

bool Frontend::draw(int w, int h, std::vector<uint32_t>& out) {
    if (!active() || w <= 0 || h <= 0) return false;
    menu_frames_++;
    if (slide_from_ >= 0) {   // PLAY's page and back: out, then the other page in
        int n = int(items().size());
        double t = menu_clock() - slide_from_;
        if (slide_out_ && t >= SLIDE_OUT_GAP * std::max(0, n - 1) + SLIDE_OUT) {
            open(slide_to_);
            selected_ = slide_sel_;
            slide_out_ = false;
            slide_from_ = menu_clock();
        } else if (!slide_out_ && t >= (slide_intro_ ? INTRO_GAP : SLIDE_IN_GAP) * std::max(0, n - 1) +
                                             (slide_intro_ ? INTRO_IN : SLIDE_IN)) {
            slide_from_ = -1;
            slide_intro_ = false;
        }
        dirty_ = true;
    }
    take_news();
    if (!news_.empty() && !news_open_ && screen_ == Screen::Main && !starting && menu_in_from_ < 0) {
        news_open_ = dirty_ = true;   // once the main menu is up (after the splash and its fade)
        news_button_ = 0;
        news_hot_ = -1;
    }
    if (!dirty_ && w == w_ && h == h_ && out.size() == size_t(w) * h) return false;
    gliding_ = false;
    if (w != w_ || h != h_) {
        w_ = w;
        h_ = h;
        u_ = std::min(h / 1080.0f, w / 1440.0f);
        bg_.clear();
        bg_blur_.clear();
    }
    if (screen_ == Screen::Splash) {
        if (draw_splash(out)) {
            dirty_ = true;   // it fades: drawn again every refresh
            return true;
        }
        open(Screen::Main);   // the last one ended: the menu comes in from black
        menu_in_from_ = now_seconds();
    }
    auto U = [&](float v) { return int(v * u_ + 0.5f); };
    bool over_game = this->over_game();
    bool page = screen_ == Screen::Options || screen_ == Screen::Records;
    if (over_game) out.assign(size_t(w) * h, 0);
    else {
        if (bg_.empty()) backdrop_background();
        out = page ? bg_blur_ : bg_;
    }
    px_ = out.data();
    hit_.clear();
    values_.clear();
    int cx = w / 2;
    // After the splash: the whole picture towards black, less every refresh
    auto fade_in = [&] {
        if (menu_in_from_ < 0) return;
        double k = (now_seconds() - menu_in_from_) / MENU_IN;
        if (k >= 1 || over_game) {
            menu_in_from_ = -1;
            return;
        }
        fill(0, 0, w, h, 0x000000, float(1 - k * k * (3 - 2 * k)));
        dirty_ = true;
    };
    if (starting) {   // PLAY pressed: the logo and a notice only, nothing to choose
        text(MONO, U(34), L"STARTING...  PLEASE WAIT", w - U(110), int((LOGO_TOP + LOGO_H) * h_) + U(60), 2,
             {GOLD_TOP, GOLD_BOTTOM, EDGE, U(2)});
        dirty_ = false;
        fade_in();
        return true;
    }
    auto list = items();
    if (screen_ == Screen::Options) {   // the settings page, over the backdrop or the paused game
        draw_options_page(list, over_game);
        dirty_ = gliding_;
        fade_in();
        return true;
    }
    bool on_records = screen_ == Screen::Records;
    bool options = screen_ == Screen::Options || on_records;   // records use the settings' card and tab row
    if (!over_game && !options) {   // Main, Debug, More: on the backdrop
        draw_side_menu(list);
        if (news_open_ && screen_ == Screen::Main) draw_news();
        dirty_ = slide_from_ >= 0 || gliding_;   // moving: drawn again every refresh
        fade_in();
        return true;
    }
    const Style gold{GOLD_TOP, GOLD_BOTTOM, EDGE, U(3)};
    const Style pale{PALE_TOP, PALE_BOTTOM, 0x080806, U(2), 0.95f};
    const Style dim{DIM, DIM, 0x080806, U(2), 0.9f};

    int y_items, spacing, item_px, hint_y, foot_y;
    int hint_room = 0;   // room for a second hint line below hint_y (else it grows upwards)
    if (!over_game) {   // the options (or the records): a card over the blurred backdrop
        int top = U(70), bottom = h - U(70), half = U(580);
        fill(cx - half, top, cx + half, bottom, PANEL, 0.74f);
        saber(cx, top, half);
        saber(cx, bottom, half);
        text(TITLE, U(96), on_records ? L"RECORDS" : L"OPTIONS", cx, top + U(34), 1,
             {GOLD_TOP, GOLD_BOTTOM, EDGE, U(5), 1, U(5)});
        y_items = top + U(196);
        spacing = U(70);
        item_px = U(46);
        hint_y = bottom - U(116);
        foot_y = bottom - U(62);
    } else {
        fill(0, 0, w, h, 0x000000, 0.62f);   // the frozen game shows through, darkened
        int n = options ? VISIBLE_OPTIONS + 2 : int(list.size());   // same panel for every settings tab
        spacing = options ? U(56) : U(80);
        item_px = options ? U(44) : U(58);
        hint_room = U(34);   // BACK sits right above the hint: two lines grow down, not into it
        int panel_h = U(170) + n * spacing + U(130) + hint_room;   // title, items, hint and key help
        int top = (h - panel_h) / 2;
        int half = options ? U(560) : U(460);
        fill(cx - half, top, cx + half, top + panel_h, PANEL, 0.92f);
        saber(cx, top, half);
        saber(cx, top + panel_h, half);
        text(TITLE, U(110), options ? L"SETTINGS" : L"PAUSED", cx, top + U(22), 1,
             {GOLD_TOP, GOLD_BOTTOM, EDGE, U(5), 1, U(5)});
        y_items = top + U(170);
        hint_y = top + panel_h - U(96) - hint_room;
        foot_y = top + panel_h - U(46);
    }
    // Settings tabs show at most VISIBLE_OPTIONS rows; longer ones scroll (the
    // tab row and BACK stay put), keeping the selected row in view
    int n_opts = options ? int(list.size()) - 2 : 0, shown_opts = n_opts;
    more_up_ = more_down_ = RECT{};
    if (options) {
        shown_opts = std::min(n_opts, VISIBLE_OPTIONS);
        scroll_ = std::clamp(scroll_, 0, std::max(0, n_opts - shown_opts));
        int k = selected_ - 1;   // selected option (0-based), if it is one
        if (k >= 0 && k < n_opts) {
            if (k < scroll_) scroll_ = k;
            if (k >= scroll_ + shown_opts) scroll_ = k - shown_opts + 1;
        }
    }
    if (on_records) {   // the list of records fills the rows of the options
        shown_opts = VISIBLE_OPTIONS;
        draw_records(cx - U(420), cx + U(400), y_items + spacing + U(24), VISIBLE_OPTIONS * spacing / VISIBLE_RECORDS);
    }
    auto slot_of = [&](int i) {   // screen row of list entry i, -1 when scrolled out of view
        if (!options || i == 0) return i;
        if (i == int(list.size()) - 1) return shown_opts + 1;   // BACK
        int k = i - 1 - scroll_;
        return k >= 0 && k < shown_opts ? k + 1 : -1;
    };

    for (int i = 0; i < int(list.size()); i++) {
        const Item& it = list[i];
        bool sel = i == selected_;
        int slot = slot_of(i);
        int y = y_items + slot * spacing + (options && slot > 0 ? U(24) : 0);   // room under the tabs
        int row_h = spacing;
        int half_w = options ? U(470) : U(320);
        if (slot < 0) {   // scrolled out of view: no hit area
            hit_.push_back(RECT{});
            values_.push_back({-1, 0});
            continue;
        }
        hit_.push_back({cx - half_w, y - (row_h - item_px) / 2 - U(6), cx + half_w, y + item_px + (row_h - item_px) / 2});
        values_.push_back({-1, 0});
        if (options && slot == 1 && scroll_ > 0) {   // more rows above / below: arrows at the right edge
            triangle(cx + half_w + U(16), y - U(4), U(20), -2, GOLD_BOTTOM, 1);
            more_up_ = {cx + half_w - U(10), y - U(30), cx + half_w + U(44), y + U(16)};
        }
        if (options && slot == shown_opts && scroll_ + shown_opts < n_opts) {
            triangle(cx + half_w + U(16), y + item_px + U(8), U(20), 2, GOLD_BOTTOM, 1);
            more_down_ = {cx + half_w - U(10), y + item_px - U(8), cx + half_w + U(44), y + item_px + U(34)};
        }
        if (sel) band(cx, y - U(8), y + item_px + U(14), half_w, SABER, it.enabled ? 0.22f : 0.10f);
        // DEBUG and its lists in red: not part of the game
        const Style red{0xFF8A7A, 0xD8402E, EDGE, U(2), 0.95f};
        bool debug_row = false;
        const Style& st = !it.enabled ? dim : sel ? gold : debug_row ? red : pale;
        if (it.id == TABS) {   // tab names side by side, the open one underlined
            int gap = U(44), total = 0, tab_px = item_px * 17 / 20;
            for (int t = 0; t < tab_count(); t++) total += measure(TITLE, tab_px, tab_name(t)) + gap;
            if (total - gap > 2 * half_w - U(80)) {   // more tabs than fit: smaller names
                tab_px = tab_px * (2 * half_w - U(80)) / (total - gap);
                gap = gap * (2 * half_w - U(80)) / (total - gap);
                total = 0;
                for (int t = 0; t < tab_count(); t++) total += measure(TITLE, tab_px, tab_name(t)) + gap;
            }
            int x = cx - (total - gap) / 2;
            tab_rects_.clear();
            for (int t = 0; t < tab_count(); t++) {
                bool open_tab = t == tab_;
                const Style& ts = open_tab ? gold : Style{0x8A98AC, 0x6E7C90, 0x020308, U(2), 0.95f};
                int ty0 = y + (item_px - tab_px) / 2;
                int tw = text(TITLE, tab_px, tab_name(t), x, ty0, 0, ts);
                if (open_tab) fill(x, ty0 + tab_px + U(6), x + tw, ty0 + tab_px + U(6) + std::max(2, U(4)), GOLD_BOTTOM, 1);
                tab_rects_.push_back({x - gap / 2, y, x + tw + gap / 2, y + item_px});
                x += tw + gap;
            }
            if (sel) {
                int ty = y + item_px * 6 / 10;
                triangle(tab_rects_.front().left - U(10), ty, U(18), -1, GOLD_BOTTOM, 1);
                triangle(tab_rects_.back().right + U(10), ty, U(18), +1, GOLD_BOTTOM, 1);
            }
            continue;
        }
        if (!options || it.id == BACK) {
            int tw = text(TITLE, item_px, it.label, cx, y, 1, st);
            int ty = y + item_px * 6 / 10;
            if (sel && it.enabled) {
                triangle(cx - tw / 2 - U(34), ty, U(20), +1, GOLD_BOTTOM, 1);
                triangle(cx + tw / 2 + U(34), ty, U(20), -1, GOLD_BOTTOM, 1);
            }
            if (!it.value.empty())   // e.g. COMING SOON
                text(MONO, U(20), it.value, cx + tw / 2 + U(it.enabled ? 62.0f : 18.0f), y + item_px / 2 - U(6), 0,
                     {0x7FB0D8, 0x6090B8});   // past the arrow of a selected entry
            continue;
        }
        int left = cx - U(410), right = cx + U(410);   // symmetric about the centre
        text(TITLE, item_px, it.label, left, y, 0, st);
        int ty = y + item_px * 6 / 10;
        int vx0;
        if (it.id == OPT_VOLUME || it.id == OPT_MUSIC) {   // ten segments
            int seg_w = U(20), gap = U(6), seg_h = U(26);
            vx0 = right - 10 * seg_w - 9 * gap;
            for (int s = 0; s < 10; s++) {
                int x0 = vx0 + s * (seg_w + gap);
                bool lit = s < (it.id == OPT_MUSIC ? settings.music : settings.volume);
                uint32_t c = lit ? mix(0xBFE8FF, SABER, float(s) / 9) : 0x2A3240;
                fill(x0 - 1, ty - seg_h / 2 - 1, x0 + seg_w + 1, ty + seg_h / 2 + 1, 0x000000, 0.8f);
                fill(x0, ty - seg_h / 2, x0 + seg_w, ty + seg_h / 2, c, sel || !lit ? 1.0f : 0.8f);
            }
        } else {
            const Style& vs = sel ? gold : Style{0xFFE87A, 0xE8BC2A, 0x020308, U(2), 0.95f};
            vx0 = right - text(ITEM, item_px, it.value, right, y, 2, vs);
        }
        values_.back() = {vx0 - U(48), (vx0 + right) / 2};
        if (sel) {
            triangle(vx0 - U(24), ty, U(18), -1, GOLD_BOTTOM, 1);
            triangle(right + U(24), ty, U(18), +1, GOLD_BOTTOM, 1);
        }
    }

    if (selected_ < int(list.size()))
    {   // wrapped to the card (two lines at most, the last one where a single line goes)
        auto lines = wrap(MONO, U(24), list[selected_].hint, U(980));
        if (lines.size() > 2) lines.resize(2);
        int ly = lines.size() > 1 ? hint_y + hint_room - (int(lines.size()) - 1) * U(30)
                                  : hint_y + hint_room / 2;   // one line: centred in the room
        for (const std::wstring& line : lines) {
            text(MONO, U(24), line, cx, ly, 1, {HINT, HINT});
            ly += U(34);
        }
    }
    if (pad_prompts) {   // the gamepad's buttons, centred
        struct Key { const wchar_t *pad, *what; };
        std::vector<Key> keys;
        if (on_records) keys = {{L"LS", L"SCROLL"}, {L"LB", L""}, {L"RB", L"TAB"}, {L"B", L"BACK"}};
        else if (options) keys = {{L"LS", L"SELECT / CHANGE"}, {L"LB", L""}, {L"RB", L"TAB"}, {L"B", L"BACK"}};
        else keys = {{L"LS", L"SELECT"}, {L"A", L"CONFIRM"}, {L"B", L"BACK"}};
        int kh = U(34), ky = foot_y + U(12) - kh / 2, x = cx + U(40) / 2;
        for (const Key& k : keys) x -= prompt_width(kh, k.pad, nullptr, k.what) / 2;
        for (const Key& k : keys) prompt(x, ky, kh, k.pad, nullptr, k.what);
    } else {
        const wchar_t* foot = on_records   ? L"ARROWS  SCROLL / TAB      WHEEL  SCROLL      ESC  BACK"
                              : options ? L"ARROWS  SELECT / CHANGE      Q / E  TAB      ESC  BACK"
                                      : L"ARROWS  SELECT      ENTER / CLICK  CONFIRM      ESC  BACK";
        text(MONO, U(21), foot, cx, foot_y, 1, {FOOT, FOOT});
    }
    if (news_open_ && screen_ == Screen::Main) draw_news();
    dirty_ = false;
    fade_in();
    return true;
}

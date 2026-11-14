// PC front end: main menu (Play, Options, Quit) shown at launch instead of
// the arcade attract loop, and the pause menu Esc opens during the game.
//
// Drawn on the CPU into a full-window BGRA picture (premultiplied alpha) with
// the OFL fonts in third_party/fonts and the game's yellow-to-orange letter
// colours; video/ui_overlay.h puts it on screen. Driven by the keyboard
// (arrows, Enter, Esc), by the mouse/gun (point and click, wheel) and by the
// gamepads (app.cpp turns what they send into keys).
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../runtime/stats.h"
#include "news.h"
#include "splash_video.h"

struct FrontendSettings {
    bool fullscreen = false;   // (written as display_mode != window, for older versions)
    int display_mode = 0;      // MODE: 0 window, 1 borderless (the desktop's resolution), 2 exclusive full screen
    bool display_mode_saved = false;   // load() found it (else from fullscreen: borderless, as before)
    int res_w = 0, res_h = 0;  // RESOLUTION: the window's inside, or the full-screen mode (0: default window / the desktop's)
    int render_scale = 100;    // RENDER SCALE, %: the 3D drawn at this share of the screen's size (--scale; next launch)
    int vsync = 1;             // VSYNC: 1 on (FIFO), 2 adaptive (FIFO_RELAXED), 0 off (MAILBOX, else IMMEDIATE)
    int max_fps = 0;           // MAX FPS: 0 the monitor's refresh rate, -1 UNLIMITED, else pictures a second at most
    bool background_fps = true;   // BACKGROUND FPS: 30 a second at most while another window has the focus
    int volume = 5;    // 0..10 (output gain): the sound effects and voices
    int music = 5;     // 0..10: the music board's gain
    int speakers = 0;  // 0 stereo, 1 surround 4.0, 2 headphones (AudioStream::Speakers)
    int rear_boost = 6;  // dB on the rear pair: 0, 3, 6, 9, 12
    bool cubic_samples = true;   // smooth (cubic) sample interpolation instead of the original linear one
    int aa = 0;        // 0 MSAA, 1 DLAA, 2 off, 3 DLSS quality (takes effect on the next launch)
    bool aa_saved = false;   // load() found it in the file (not there on the first run: chosen then for the GPU and the screen, app.cpp)
    bool haze = true;  // distance haze
    int bloom = 2;     // glow around bright things: 0 off, 1 low, 2 medium, 3 high
    bool ao = true;    // ambient occlusion
    bool sun = true;   // a stronger, warmer sunlight in clear outdoor scenes
    bool shadows = true;   // sun shadows
    int ray_tracing = 2;   // traced through the world (GPUs with ray tracing): 0 off, 1 sun shadows, 2 also ambient occlusion
    bool soft = true;      // soft particles
    bool hdr = true;       // HDR lighting
    bool reflections = true;   // the world mirrored on the cars (ray tracing)
    int gi = 1;                // global illumination, the sun's light bounced off the world (ray tracing): 0 off, 1 on
    bool dust = true;      // Sega Rally 2: dust and stones as particles (false: the game's own flat fans)
    bool sharpen = true;   // contrast adaptive sharpening
    bool hd_hud = true;    // redrawn HUD and game screens (false: the ROM's own graphics)
    bool hd_liveries = true;   // the cars' redone paint, textures/*.png (false: the ROM's own textures)
    // The arcade's difficulty, in the game's own numbering (bits 19-21 of the
    // operator settings word, RAM 0x4A9D18): 0 NORMAL, 1 HARD, 2 VERY HARD,
    // 3 VERY EASY, 4 EASY. It sets the time at the start and at each checkpoint
    int difficulty = 0;
    bool smooth = true;     // the 3D drawn between game frames at the monitor's refresh rate
    int saturation = 100;   // colour saturation in % (100 = original, the default)
    int gamma = 100;        // gamma x 100 (100 = original, higher = brighter)
    int vibration = 2;      // gamepad vibration: 0 off, 1 low, 2 medium, 3 high
    int trigger_rumble = 100;   // the triggers' own motors (Xbox One / Series pads), % of VIBRATION's strength, steps of 10
    bool splash = true;     // the logo video at launch (splash_screens = 0: straight to the menu)
    bool menu_music = true; // the game's Main Title in the menus
    bool check_updates = true;     // at launch, news.txt on GitHub: a window with what is new and UPDATE NOW
    int news_seen = 0;             // the newest news.txt entry the player closed with LATER

    // save/settings.ini; missing keys keep their current value
    void load(const char* path);
    // QUALITY: the graphics options that cost the most, set together.
    // quality() is the level they match, CUSTOM if none
    enum Quality { LOW, MEDIUM, HIGH, ULTRA, CUSTOM };
    void set_quality(int q);
    int quality() const;
    void save(const char* path) const;
    std::string text() const;   // what save() writes: one "key = value" line per setting
};

class Frontend {
public:
    enum class Screen { None, Splash, Main, Options, Pause, Records, Debug, Play };
    enum class Action { None, StartArcade, Resume, Quit, ToggleFullscreen, SettingsChanged,
                        QuitToMenu, StartDebug, DisplayChanged };   // DisplayChanged: MODE / RESOLUTION to the window

    bool init(const std::string& font_dir);
    void shutdown();

    void open(Screen s);
    Screen screen() const { return screen_; }
    bool active() const { return screen_ != Screen::None; }
    // At launch: the logo video with its sound (third_party/menu/ or res/menu/
    // splash_snicolast.mp4; if Windows cannot play it, splash_snicolast.png),
    // skipped with Enter / Esc / a click / START, then the main menu fades in
    void open_splash();
    bool splash() const { return screen_ == Screen::Splash; }
    // The menu is over the frozen game (pause and its settings)
    bool over_game() const { return screen_ == Screen::Pause || (screen_ == Screen::Options && options_from_pause_); }
    bool pause_settings() const { return screen_ == Screen::Options && options_from_pause_; }
    // The pause settings stand on the game blurred on the GPU (ui_overlay): a
    // lighter shade then, as OPTIONS over the blurred backdrop. App sets it
    bool pause_blur = false;

    // Window input; true when the menu used it
    bool key(int vk);
    void mouse_move(int x, int y);
    void click(int x, int y);
    void wheel(int x, int y, int delta);
    // Actions picked since the last call, oldest first
    std::vector<Action> take_actions();
    void request(Action a) { actions_.push_back(a); }   // as if chosen in the menu (SR2_AUTOQUIT)

    FrontendSettings settings;
    int aa_at_launch = 0;   // anti-aliasing the renderer was started with
    int scale_at_launch = 100;   // RENDER SCALE the renderer was started with
    std::vector<std::pair<int, int>> resolutions;   // the monitor's (app.cpp, VkPresenter::display_modes)
    int client_w = 0, client_h = 0, desktop_w = 0, desktop_h = 0;   // the window's inside, the desktop (app.cpp)
    int refresh_hz = 60;                            // the monitor's (MAX FPS MONITOR)
    bool rt_available = false;   // the GPU can trace rays (RAY TRACING option)
    std::wstring gamepads;  // the pads connected, for the CONTROLS tab ("" = none)
    bool pad_prompts = false;   // a gamepad drives the menu: its buttons in the key help and hints, not the keyboard's

    // At launch: news.txt from GitHub in the background (when the settings
    // allow it). An entry newer than the news.txt here and than the last one
    // closed with LATER opens a window over the main menu listing every entry
    // this version lacks (see news.h)
    void check_for_news();

    bool starting = false;    // PLAY pressed: the game still gets to its mission select (a notice instead of the hint)

    // DEBUG (testing): a practice race of any course with any car. StartDebug
    // reads debug_choice = course | car << 4 | transmission << 8 (runtime.cpp, debug_jump_frame)
    int debug_choice = -1;
    // DEBUG: the race's time left held where it is (no TIME IS UP); app.cpp hands it
    // to the game thread every frame (runtime.cpp, debug_time_frame)
    bool debug_time_frozen = getenv("SR2_FREEZE_TIME") != nullptr;

    // RECORDS screen (runtime/stats.h): the window keeps this copy up to date while it is open
    StatsView records;

    // Redraws into `out` when something changed (or the size did); true if it did
    bool draw(int w, int h, std::vector<uint32_t>& out);
    // The backdrop moving on the GPU (video/ui_overlay.cpp): draw() then leaves
    // it out (transparent there) and only shades it. App sets it (SR2_MENU_STILL: off)
    void set_motion(bool on);
    // The backdrop under the picture draw() made: 0 the picture, 1 blurred
    // (the settings), -1 none (the picture is opaque: the splash, the pause)
    int motion_backdrop() const;
    // The backdrop's pixels for the GPU (BGRA): 0 the picture, 1 blurred
    void backdrop_pixels(int slot, const uint32_t*& px, int& w, int& h);
    void invalidate() { dirty_ = true; }

private:
    enum Id {
        PLAY, OPTIONS, QUIT, BACK, RESUME, SETTINGS, QUIT_TO_MENU, TABS,
        RECORDS, ARCADE, GHOST,   // ARCADE, GHOST: PLAY's page
        DEBUG, DEBUG_COURSE, DEBUG_CAR, DEBUG_GEARS, DEBUG_TIME, DEBUG_GO,
        OPT_QUALITY, OPT_DISPLAY, OPT_VOLUME, OPT_MUSIC, OPT_SPEAKERS, OPT_REAR, OPT_SAMPLES, OPT_AA, OPT_HAZE, OPT_BLOOM, OPT_AO, OPT_SUN, OPT_SHADOWS, OPT_SOFT, OPT_HDR, OPT_SHARPEN, OPT_HUD, OPT_LIVERIES,
        OPT_SAT, OPT_GAMMA, OPT_MOTION, OPT_VIBRATION, OPT_RT, OPT_MENU_MUSIC, OPT_REFLECT, OPT_GI, OPT_DUST, OPT_UPDATES,
        OPT_DIFFICULTY, OPT_CONNECTED, OPT_TRIGGERS, OPT_RESOLUTION, OPT_SCALE, OPT_VSYNC, OPT_MAXFPS, OPT_BGFPS,
    };
    struct Item {
        Id id;
        std::wstring label, value, hint;
        bool enabled = true;
        int index = -1;
    };
    struct Mask {   // text coverage (0..255) with `pad` pixels around the text box
        int w = 0, h = 0, pad = 0, text_w = 0, text_h = 0;
        int grad0 = 0, grad1 = 0;   // rows of the colour gradient (top of the capitals to the baseline)
        std::vector<uint8_t> a, e;  // letters, letters grown by the outline width
    };
    struct Style {
        uint32_t top, bottom;   // gradient (0xRRGGBB)
        uint32_t edge = 0;      // outline colour
        int edge_px = 0;
        float alpha = 1;
        int shadow = 0;         // drop shadow offset (pixels), black
    };
    // TITLE: Saira ExtraCondensed Bold (headings), ITEM: Barlow Condensed Bold,
    // MONO: Share Tech Mono, and for the settings page ROW: Barlow Condensed
    // SemiBold (its rows), ROW_MEDIUM: Barlow Condensed Medium (group names, key
    // help), BODY: Barlow Regular (descriptions)
    enum Family { TITLE, ITEM, MONO, ROW, ROW_MEDIUM, BODY };

    std::vector<Item> items() const;
    void activate(const Item& it, int dir);
    void back();
    int index_of(Id id) const;   // the entry with that id in the current screen (0 if none)
    void push(Action a) { actions_.push_back(a); }
    void select(int index);

    HFONT font(Family f, int px);
    const Mask& mask(Family f, int px, const std::wstring& text, int edge_px);
    int text(Family f, int px, const std::wstring& s, int x, int y, int align, const Style& st);
    int measure(Family f, int px, const std::wstring& s);
    void fill(int x0, int y0, int x1, int y1, uint32_t rgb, float alpha);
    void band(int cx, int y0, int y1, int half_w, uint32_t rgb, float alpha);
    void triangle(int x, int y, int size, int dir, uint32_t rgb, float alpha);
    void blend(int x, int y, uint32_t rgb, float a);
    void saber(int cx, int y, int half_w);   // a glowing lightsaber-blue line
    void hgrad(int x0, int y0, int x1, int y1, uint32_t rgb, float a0, float a1);
    // A slanted red stripe, as on the cars: left edge leaning right by `slant`
    // at the top (the right edge too), alpha a0 to a1 across, and a thin one
    // (width sw) gap before it
    void ribbon(int x0, int y0, int x1, int y1, int slant, float a0, float a1, int gap, int sw, float alpha);
    void vgrad(int x0, int y0, int x1, int y1, uint32_t rgb, float a0, float a1);
    void frame(int x0, int y0, int x1, int y1, int t, uint32_t rgb, float alpha);
    void pill(int x0, int y0, int x1, int y1, uint32_t rgb, float alpha);
    std::vector<std::wstring> wrap(Family f, int px, const std::wstring& s, int max_w);

    // Pictures of the menus (res/menu/, or your own of the same name in
    // third_party/menu/): the backdrop, the title logo over it, the credit
    struct Picture {
        std::vector<uint32_t> px;   // BGRA, straight alpha
        int w = 0, h = 0;
    };
    Picture backdrop_, logo_, credit_;   // (backdrop_: always one, the drawn placeholder if no picture)
    void picture(const Picture& p, int x0, int y0, int x1, int y1, float anchor_x = 0.5f);   // covering the box, opaque
    void sprite(const Picture& p, int x0, int y0, int w, int h);      // with its transparency
    // The gamepad's buttons in the key help (res/pad/<button>.png, CC0); w = 0:
    // none, a drawn disc or pill instead
    std::map<std::wstring, Picture> pad_icons_;
    const Picture& pad_icon(const std::wstring& button);
    // One entry of the key help at (x, y), h high: the gamepad's button when
    // pad_prompts (else the key, in a keycap), then what it does. x moves past
    // it. prompt_width: how far it moves x
    void prompt(int& x, int y, int h, const wchar_t* pad_button, const wchar_t* key, const wchar_t* what);
    int prompt_width(int h, const wchar_t* pad_button, const wchar_t* key, const wchar_t* what);
    void backdrop_background();
    // Main, Play, Boss, Survival and Debug on the backdrop: a column at the right under the logo
    void draw_side_menu(const std::vector<Item>& list);
    // PLAY's page and back: the entries go out to the right one after the
    // other, then the new page's come in (as Windjammers' menus). Drawn every
    // refresh meanwhile; keys and clicks wait
    double slide_from_ = -1;          // menu_clock() when the current half began (-1: still)
    bool slide_out_ = false;          // going out (else coming in)
    bool slide_intro_ = false;        // the main menu coming up: only in, a little slower
    Screen slide_to_ = Screen::Main;  // the page coming in
    int slide_sel_ = 0;               // its entry chosen
    uint64_t menu_frames_ = 0;        // draw() calls (SR2_MENU_FPS: the clock goes by them)
    double menu_clock() const;
    void slide_to(Screen s, int sel);
    void finish_slide();              // a key or a click meanwhile: at once to the end
    int slide_offset(int i) const;    // how far right entry i is, in pixels
    // The chosen entry's ribbon gliding to the next one (0.13 s, easing
    // out) instead of jumping; `key` changes (another page or section, a new
    // window size): straight there. Drawn every refresh while it moves
    struct Glide {
        float from = 0, to = 0, from_h = 0, to_h = 0;
        double t0 = -1;
        long key = -1;
    };
    Glide glide_menu_, glide_tab_, glide_row_;
    bool gliding_ = false;            // one of them is moving (this picture)
    void glide(Glide& g, long key, int y, int h, int& gy, int& gh);
    // KEEP THESE SETTINGS?: after MODE FULLSCREEN or a new RESOLUTION, back
    // to the previous ones by itself after 10 s unless KEEP is chosen
    bool keep_open_ = false;
    double keep_from_ = 0;
    int keep_button_ = 0;   // 0 KEEP, 1 REVERT
    int keep_mode_ = 0, keep_w_ = 0, keep_h_ = 0;
    RECT keep_buttons_[2]{};
    void ask_keep(int mode, int w, int h);
    void keep_choose(int button);
    bool keep_key(int vk);
    void draw_keep();
    // RESOLUTION's list: Enter or a click opens it over the description panel
    bool drop_open_ = false;
    int drop_sel_ = 0, drop_top_ = 0;     // the highlighted one, the first shown (resolutions, largest first)
    std::vector<RECT> drop_rects_;
    RECT panel_rect_{};                   // the settings page's description panel (draw_options_page)
    void open_drop();
    void drop_pick(int i);
    bool drop_key(int vk);
    void draw_drop();
    void placeholder_backdrop();      // no background picture: a drawn one (Sega Rally 2)
    void shade_backdrop();            // darker at the right and the bottom, the logo: on px_
    // UPDATE AVAILABLE: the window over the main menu with the entries of
    // news.txt this version lacks. UPDATE NOW starts update.bat (it waits for the game to close,
    // updates, rebuilds and starts it again) and closes the game; LATER (Esc)
    // keeps it closed until a newer entry
    struct NewsDownload {                  // shared with the thread that downloads it (left to finish on its own)
        std::mutex mutex;
        std::string text;                  // news.txt from GitHub
        std::atomic<int> state{0};         // 0 downloading, 1 there, 2 failed, 3 taken (take_news)
    };
    std::shared_ptr<NewsDownload> news_download_;
    std::vector<NewsEntry> news_;          // the entries shown
    int news_latest_ = 0;                  // the newest one on GitHub
    bool news_open_ = false;
    int news_button_ = 0;                  // 0 UPDATE NOW, 1 LATER
    int news_hot_ = -1;                    // the button under the pointer
    RECT news_buttons_[2]{};
    void take_news();                      // the download, once it is there
    void draw_news();
    bool news_key(int vk);
    void news_choose(int button);
    void start_update();

    // The splash screens: their pictures, which one is on and since when
    std::vector<Picture> splash_;
    int splash_on_ = 0;
    double splash_from_ = 0;
    double skip_from_ = -1;     // hurried out at this time (-1: not)
    float skip_level_ = 1;      // from this brightness
    bool skip_next_ = false;    // pressed again while hurrying: the next one goes too
    std::vector<uint32_t> splash_px_;
    int splash_px_on_ = -1, splash_px_w_ = 0, splash_px_h_ = 0;
    void skip_splash();
    bool draw_splash(std::vector<uint32_t>& out);   // false: the last one ended
    // The splash video, when there is one Windows can open: it replaces the pictures
    SplashVideo video_;
    bool video_on_ = false;
    std::vector<uint32_t> video_px_;
    int video_pw_ = 0, video_ph_ = 0;
    double video_end_ = -1;
    float video_level_ = 0;
    bool draw_splash_video(std::vector<uint32_t>& out);
    double menu_in_from_ = -1;   // after the splash: the main menu fading in from black since then

    Screen screen_ = Screen::None;
    bool options_from_pause_ = false;
    int selected_ = 0, hovered_ = -1;
    bool dirty_ = true;
    std::vector<Action> actions_;
    std::vector<RECT> hit_;   // item rectangles of the last draw
    struct ValueArea { int left, middle; };   // option rows: value column (left < 0: none)
    std::vector<ValueArea> values_;
    bool wrap_ = false;
    // Settings sections (tab_of in frontend.cpp says where each option goes),
    // shown as a column at the left of the settings page
    enum { DISPLAY_TAB, LIGHTING_TAB, AUDIO_TAB, CONTROLS_TAB, GAME_TAB };   // LIGHTING_TAB is shown as GRAPHICS
    static constexpr int TAB_COUNT = 5;
    static const wchar_t* const TAB_NAMES[TAB_COUNT];
    static int tab_of(Id id);
    struct OptionTab {
        Id id;
        int tab;
        const wchar_t* group = nullptr;   // the first of a group of rows: its heading
    };
    static const wchar_t* group_of(Id id);   // the heading the option starts, if any
    static const OptionTab OPTION_TABS[];   // frontend.cpp: where each option goes
    void set_tab(int t);
    int tab_ = 0;   // open tab of the settings, or of the records
    // DEBUG: the race chosen (indexes into DEBUG_COURSES / DEBUG_CARS in frontend.cpp), kept while the game runs
    int debug_course_ = 0, debug_car_ = 0, debug_gears_ = 0;
    // The tab row of the screen on: settings tabs, or ACHIEVEMENTS / STATS
    static constexpr int RECORD_TAB_COUNT = 2;
    static const wchar_t* const RECORD_TAB_NAMES[RECORD_TAB_COUNT];
    int tab_count() const { return screen_ == Screen::Records ? RECORD_TAB_COUNT : TAB_COUNT; }
    const wchar_t* tab_name(int t) const { return screen_ == Screen::Records ? RECORD_TAB_NAMES[t] : TAB_NAMES[t]; }
    // Records: the rows of the open tab and the first one shown
    struct RecordRow {
        std::wstring label, detail, value;
        bool lit = true;   // an achievement: unlocked
    };
    std::vector<RecordRow> record_rows() const;
    static constexpr int VISIBLE_RECORDS = 7;
    int record_scroll_ = 0;
    void scroll_records(int by);
    void draw_records(int x0, int x1, int y0, int row_h);
    // The records show this many rows of the old settings card
    static constexpr int VISIBLE_OPTIONS = 6;
    int scroll_ = 0;           // first option shown (the settings page scrolls only when its rows do not fit)
    bool options_scroll_ = false;   // the settings page's rows did not fit in the last draw
    // The settings page (Options, also from the pause menu): the sections in a
    // column at the left, the open one's options in groups in the middle, what
    // the chosen one does in a panel at the right, the key help at the bottom
    void draw_options_page(const std::vector<Item>& list, bool over_game);
    RECT more_up_{}, more_down_{};   // clickable "more" arrows of a scrolled tab
    std::vector<RECT> tab_rects_;   // a step past the end starts again (clicks on the label)
    int mouse_x_ = -1, mouse_y_ = -1;

    std::vector<HANDLE> font_handles_;
    std::vector<std::vector<char>> font_data_;
    std::map<std::pair<int, int>, HFONT> fonts_;
    std::map<std::wstring, Mask> masks_;
    HDC dc_ = nullptr;

    int w_ = 0, h_ = 0;
    float u_ = 1;   // pixels per unit of a 1920x1080 layout
    uint32_t* px_ = nullptr;
    std::vector<uint32_t> bg_;        // main menu backdrop for the current size
    std::vector<uint32_t> bg_blur_;   // ... blurred and darkened, behind the options
    bool motion_ = false;             // set_motion
    Picture backdrop_blur_;           // backdrop_ blurred, for the GPU
    static bool load_image(const std::wstring& path, std::vector<uint32_t>& px, int& w, int& h);
};

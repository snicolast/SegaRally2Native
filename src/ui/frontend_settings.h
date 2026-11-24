// The PC settings the menus change and save/settings.ini keeps (PC EDITION at
// the top of the file, ARCADE ORIGINAL in its [arcade] section). Plain data
// with no Windows header, so tests can read and write it.
#pragma once
#include <string>

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
    int aa = 0;        // 0 MSAA, 1 DLAA, 2 off, 3 DLSS QUALITY, 4 BALANCED, 5 PERFORMANCE, 6 ULTRA PERFORMANCE, 7 FXAA (next launch)
    // settings.ini only (dlss_model = auto / k / m / l), for advanced users: the
    // model DLAA and DLSS use. 0 AUTO (dlss::preset_for), 1 K, 2 M, 3 L
    int dlss_model = 0;
    bool aa_saved = false;   // load() found it in the file (not there on the first run: chosen then for the GPU and the screen, app.cpp)
    bool haze = true;  // distance haze
    int bloom = 2;     // glow around bright things: 0 off, 1 low, 2 medium, 3 high
    bool ao = true;    // ambient occlusion
    bool sun = true;   // a stronger, warmer sunlight in clear outdoor scenes
    bool shadows = true;   // sun shadows
    bool car_probes = true;    // the cars darken in the shade: light probes on them (with the sun shadows)
    bool car_shadows = true;   // ... and the world's sun shadows fall on them pixel by pixel
    int ray_tracing = 2;   // traced through the world (GPUs with ray tracing): 0 off, 1 sun shadows, 2 also ambient occlusion
    bool soft = true;      // soft particles
    bool hdr = true;       // HDR lighting
    bool reflections = true;   // the world mirrored on the cars (ray tracing)
    int gi = 1;                // global illumination, the sun's light bounced off the world (ray tracing): 0 off, 1 on
    bool dust = true;      // Sega Rally 2: dust and stones as particles (false: the game's own flat fans)
    // OBJECT DISTANCE: metres ahead of the camera the spectators, cameramen, signs,
    // cones... appear from: 250, 200, 150 or 100 (the arcade's)
    int object_distance = 250;
    bool sharpen = true;   // contrast adaptive sharpening
    bool hd_hud = true;    // redrawn HUD and game screens (false: the ROM's own graphics)
    bool hd_liveries = true;   // the cars' redone paint, textures/*.png (false: the ROM's own textures)
    // The arcade's difficulty, in the game's own numbering (bits 19-21 of the
    // operator settings word, RAM 0x4A9D18): 0 NORMAL, 1 HARD, 2 VERY HARD,
    // 3 VERY EASY, 4 EASY. It sets the time at the start and at each checkpoint
    int difficulty = 0;
    // PLAY > ARCADE CLASSIC's race set-up, the other operator settings (GAME
    // ASSIGNMENTS; runtime.cpp): GAME MODE 0 NORMAL / 1 SPECIAL, DEFAULT VIEW
    // 0 DRIVER / 1 BEHIND, SPEED 1 KM/H / 0 MPH (the operator's defaults)
    int game_mode = 0, default_view = 0, speed_kmh = 1;
    // ASPECT: 0 AUTO (fills the window, up to 32:9), 1 32:9, 2 21:9, 3 16:9, 4 4:3 (the
    // original picture, the HUD in its place). The widest picture: a wider window gets black bars
    int aspect = 3;
    // HUD: false WIDE (its corners out at the picture's sides, up to a 16:9 one's),
    // true 4:3 (in its place in the middle, as on the cabinet, whatever ASPECT is).
    // With ASPECT 4:3 the picture is 4:3 anyway, so it changes nothing there
    bool hud_43 = false;
    bool free_play = true;   // ARCADE ORIGINAL (its pause menu): FREE PLAY, or a coin per game
    // EXPERIENCE (the file's first section only): 0 PC EDITION, 1 ARCADE ORIGINAL. Each
    // experience keeps its own settings: PC EDITION's at the top of the file, ARCADE
    // ORIGINAL's in its [arcade] section
    int experience = 0;
    bool smooth = true;     // the 3D drawn between game frames at the monitor's refresh rate
    int saturation = 100;   // colour saturation in % (100 = original, the default)
    int gamma = 100;        // gamma x 100 (100 = original, higher = brighter)
    int vibration = 2;      // gamepad vibration: 0 off, 1 low, 2 medium, 3 high
    int trigger_rumble = 100;   // the triggers' own motors (Xbox One / Series pads), % of VIBRATION's strength, steps of 10
    bool splash = true;     // the logo video at launch (splash_screens = 0: straight to the menu)
    bool menu_music = true; // the game's Main Title in the menus
    // settings.ini only: UDP telemetry for SimHub and motion rigs (runtime/telemetry.h),
    // to 127.0.0.1 on this port. Off by default
    bool telemetry = false;
    int telemetry_port = 20777;
    int telemetry_format = 0;        // dirt or ogs-ts (TelemetryFormat, runtime/telemetry.h)

    // save/settings.ini; missing keys keep their current value. section: "" the
    // file's first one (PC EDITION and the keys of the whole game), "arcade" ARCADE ORIGINAL's
    void load(const char* path, const char* section = "");
    // settings.ini's text (save_settings() in app.cpp writes it): one
    // "key = value" line per setting (section: without the keys of the whole
    // game and EXPERIENCE)
    std::string text(bool section = false) const;
    // ARCADE ORIGINAL's settings before it has a section of its own: these (the
    // screen, the sound, the controls) with the arcade's own look: the ROM's HUD
    // and paint, no ray tracing or new effects, MOTION SMOOTH, ASPECT 4:3
    FrontendSettings arcade_defaults() const;
    // Those base values of ARCADE ORIGINAL (its [arcade] section's first time, and RESTORE ORIGINAL):
    // everything but the screen (MODE, RESOLUTION, RENDER SCALE, VSYNC, MAX FPS...), the
    // speakers and the controls, which stay the player's
    void arcade_base();
    // ASPECT as numbers: the widest picture (window width over height) and the
    // shape of a pixel (4:3: the cabinet's 496 x 384 on a 4:3 monitor)
    static float aspect_ratio(int aspect);
    static float pixel_aspect(int aspect) { return aspect == 4 ? (4.0f / 3.0f) / (496.0f / 384.0f) : 1.0f; }
};

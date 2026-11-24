// save/settings.ini (frontend_settings.cpp) and its bridge to the command-line
// options (settings_bridge.cpp): what is written reads back the same, the
// [arcade] section keeps its own values, older keys still load, and the
// options carry the settings over and back
#include <cmath>
#include <cstdio>
#include <string>

#include "frontend_settings.h"
#include "settings_bridge.h"

static int g_failed = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAILED line %d: %s\n", __LINE__, #cond);            \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

static const char* const FILE_NAME = "settings_test.ini";

static void write(const std::string& text) {
    FILE* f = fopen(FILE_NAME, "w");
    if (!f) return;
    fputs(text.c_str(), f);
    fclose(f);
}

// `text` as a file, read into default settings
static FrontendSettings read(const std::string& text, const char* section = "") {
    write(text);
    FrontendSettings s;
    s.load(FILE_NAME, section);
    return s;
}

// Settings with every value away from its default
static FrontendSettings changed() {
    FrontendSettings s;
    s.display_mode = 2, s.fullscreen = true, s.res_w = 2560, s.res_h = 1440, s.render_scale = 150;
    s.vsync = 2, s.max_fps = 144, s.background_fps = false;
    s.volume = 7, s.music = 3, s.speakers = 2, s.rear_boost = 9, s.cubic_samples = false;
    s.aa = 4, s.dlss_model = 2;
    s.haze = false, s.bloom = 3, s.ao = false, s.sun = false, s.shadows = false, s.ray_tracing = 1;
    s.car_probes = false, s.car_shadows = false;
    s.soft = false, s.hdr = false, s.reflections = false, s.gi = 0, s.dust = false, s.object_distance = 150;
    s.sharpen = false, s.hd_hud = false, s.hd_liveries = false;
    s.difficulty = 3, s.game_mode = 1, s.default_view = 1, s.speed_kmh = 0;
    s.aspect = 2, s.hud_43 = true, s.free_play = false, s.experience = 1;
    s.smooth = false, s.saturation = 120, s.gamma = 130, s.vibration = 1, s.trigger_rumble = 40;
    s.splash = false, s.menu_music = false;
    s.telemetry = true, s.telemetry_port = 5555, s.telemetry_format = 1;
    return s;
}

int main() {
    {   // the defaults, written and read back
        FrontendSettings a;
        FrontendSettings b = read(a.text());
        CHECK(b.text() == a.text());
        CHECK(b.aa_saved && b.display_mode_saved);
    }
    {   // every value changed, written and read back
        FrontendSettings a = changed();
        FrontendSettings b = read(a.text());
        CHECK(b.text() == a.text());
        CHECK(b.display_mode == 2 && b.res_w == 2560 && b.res_h == 1440 && b.max_fps == 144 && b.vsync == 2);
        CHECK(b.aa == 4 && b.dlss_model == 2 && b.bloom == 3 && b.ray_tracing == 1 && b.object_distance == 150);
        CHECK(b.difficulty == 3 && b.aspect == 2 && b.hud_43 && b.gamma == 130 && b.telemetry_port == 5555);
    }
    {   // the [arcade] section: its own values; the keys of the whole game only at the top
        FrontendSettings top = changed(), arcade;
        top.experience = 1;
        arcade.arcade_base();
        arcade.telemetry_port = 1234;   // not written in a section
        const std::string text = top.text() + "[arcade]\n" + arcade.text(true) + "experience = pc\ntelemetry = 0\n";
        FrontendSettings t = read(text), a = read(text, "arcade");
        CHECK(t.experience == 1 && t.telemetry && t.telemetry_port == 5555 && t.volume == 7 && t.aspect == 2);
        CHECK(a.experience == 0 && !a.telemetry && a.telemetry_port == 20777);   // (the defaults: not read from the section)
        CHECK(a.aspect == 4 && a.aa == 2 && !a.hd_hud && a.ray_tracing == 0 && a.object_distance == 100);
    }
    {   // ARCADE ORIGINAL's base leaves the screen, the speakers and the controls alone
        FrontendSettings s = changed();
        s.arcade_base();
        CHECK(s.display_mode == 2 && s.res_w == 2560 && s.max_fps == 144 && s.speakers == 2 && s.vibration == 1);
        CHECK(s.volume == 5 && s.music == 5 && s.aa == 2 && s.aspect == 4 && !s.cubic_samples && s.free_play);
    }
    {   // older keys and values
        FrontendSettings s = read("fullscreen = 1\nhd_text = 0\nframe_rate = half\nglobal_illumination = high\naa = dlss\n");
        CHECK(s.display_mode == 1 && !s.display_mode_saved);   // fullscreen = 1 was the borderless one
        CHECK(!s.hd_hud && s.max_fps == 60 && s.gi == 1 && s.aa == 3);
        FrontendSettings w = read("fullscreen = 0\n");
        CHECK(w.display_mode == 0);
    }
    {   // out of range values are clamped
        FrontendSettings s = read("volume = 40\nrender_scale = 10\nobject_distance = 999\nrear_boost = 7\ngamma = 9\n");
        CHECK(s.volume == 10 && s.render_scale == 50 && s.object_distance == 250 && s.rear_boost == 6 && s.gamma == 150);
    }
    {   // the options: settings -> options -> settings keeps them
        FrontendSettings a = changed();
        a.experience = 0;   // PC EDITION: the race set-up goes through
        Options o;
        options_from_settings(o, a);
        CHECK(o.dlaa && o.dlss == 2 && std::fabs(o.bloom - BLOOM_STRENGTH[3]) < 1e-6f && o.haze == 0.0f);
        CHECK(o.difficulty == 3 && o.free_play == -1 && !o.arcade);
        FrontendSettings b;
        settings_from_options(b, o);
        CHECK(b.aa == a.aa && b.bloom == a.bloom && b.haze == a.haze && b.object_distance == a.object_distance);
        CHECK(b.difficulty == a.difficulty && b.saturation == a.saturation && b.gamma == a.gamma && b.render_scale == a.render_scale);
        a.experience = 1;   // ARCADE ORIGINAL: the operator's own settings
        options_from_settings(o, a);
        CHECK(o.arcade && o.difficulty == -1 && o.free_play == 0);
    }
    remove(FILE_NAME);
    if (g_failed) {
        printf("%d check(s) failed\n", g_failed);
        return 1;
    }
    printf("settings: all checks passed\n");
    return 0;
}

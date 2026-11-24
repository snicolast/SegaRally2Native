// save/settings.ini <-> FrontendSettings (frontend_settings.h)
#include "frontend_settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

// The ini's names of the difficulties, in the game's numbering (FrontendSettings::difficulty)
static const char* const DIFFICULTY_KEYS[5] = {"normal", "hard", "very_hard", "very_easy", "easy"};

void FrontendSettings::load(const char* path, const char* section) {
    FILE* f = fopen(path, "r");
    if (!f) return;
    struct ModeFromFullscreen {   // an older file: fullscreen = 1 was the borderless one
        FrontendSettings& s;
        ~ModeFromFullscreen() { if (!s.display_mode_saved) s.display_mode = s.fullscreen ? 1 : 0; }
    } mode_from_fullscreen{*this};
    const bool top = !section || !*section;
    std::string in_section;   // "" the first section, else the name of the last [name] line
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char name[64];
        if (sscanf(line, " [%63[^]]]", name) == 1) {
            in_section = name;
            continue;
        }
        if (top ? !in_section.empty() : in_section != section) continue;
        char key[64], value[64];
        if (sscanf(line, " %63[^= ] = %63s", key, value) != 2) continue;
        std::string k = key, v = value;
        bool on = v == "1" || v == "on" || v == "true";
        // (the keys of the whole game only in the first section)
        if (!top && (k == "experience" || k == "telemetry" || k == "telemetry_port" || k == "telemetry_format")) continue;
        if (k == "experience") experience = v == "arcade" ? 1 : 0;
        else if (k == "aspect") {
            static const char* const NAMES[5] = {"auto", "32:9", "21:9", "16:9", "4:3"};
            for (int a = 0; a < 5; a++)
                if (v == NAMES[a]) aspect = a;
        }
        else if (k == "hud") hud_43 = v == "4:3";
        else if (k == "game_mode") game_mode = v == "special" ? 1 : 0;
        else if (k == "default_view") default_view = v == "behind" ? 1 : 0;
        else if (k == "speed") speed_kmh = v == "mph" ? 0 : 1;
        else if (k == "free_play") free_play = on;
        else if (k == "fullscreen") fullscreen = on;
        else if (k == "display_mode") display_mode = v == "fullscreen" ? 2 : v == "borderless" ? 1 : 0, display_mode_saved = true;
        else if (k == "resolution") { if (sscanf(value, "%dx%d", &res_w, &res_h) != 2) res_w = res_h = 0; }
        else if (k == "render_scale") render_scale = std::clamp(atoi(value), 50, 200);
        else if (k == "frame_rate") max_fps = v == "half" || v == "60" ? 60 : 0;   // (an earlier test version's)
        else if (k == "vsync") vsync = v == "adaptive" ? 2 : on ? 1 : 0;
        else if (k == "max_fps") max_fps = v == "unlimited" ? -1 : v == "monitor" ? 0 : std::clamp(atoi(value), 0, 1000);
        else if (k == "background_fps") background_fps = on;
        else if (k == "volume") volume = std::clamp(atoi(value), 0, 10);
        else if (k == "music") music = std::clamp(atoi(value), 0, 10);
        else if (k == "speakers") speakers = v == "surround" ? 1 : v == "headphones" ? 2 : 0;
        else if (k == "rear_boost") rear_boost = std::clamp(atoi(value) / 3 * 3, 0, 12);
        else if (k == "sample_interpolation") cubic_samples = v != "linear";
        else if (k == "aa") {   // ("dlss" is DLSS QUALITY, as before the other modes)
            aa = v == "dlaa" ? 1 : v == "dlss" || v == "dlss_quality" ? 3 : v == "dlss_balanced" ? 4
               : v == "dlss_performance" ? 5 : v == "dlss_ultra" ? 6 : (v == "none" || v == "off") ? 2 : v == "fxaa" ? 7 : 0;
            aa_saved = true;
        } else if (k == "dlss_model") dlss_model = v == "k" ? 1 : v == "m" ? 2 : v == "l" ? 3 : 0;
        else if (k == "haze") haze = on;
        else if (k == "bloom") bloom = v == "low" ? 1 : v == "medium" ? 2 : v == "high" ? 3 : on ? 2 : 0;
        else if (k == "ao") ao = on;
        else if (k == "sun") sun = on;
        else if (k == "shadows") shadows = on;
        else if (k == "car_light_probes") car_probes = on;
        else if (k == "car_shadows") car_shadows = on;
        else if (k == "ray_tracing") ray_tracing = v == "shadows" ? 1 : v == "off" || v == "0" ? 0 : 2;
        else if (k == "soft_particles") soft = on;
        else if (k == "hdr") hdr = on;
        else if (k == "rt_reflections") reflections = on;
        // (on or off: a "high" saved before that level was dropped turns into on)
        else if (k == "global_illumination") gi = v == "off" || v == "0" ? 0 : 1;
        else if (k == "dust") dust = v != "original" && v != "0";
        else if (k == "object_distance") object_distance = std::clamp((atoi(value) + 25) / 50 * 50, 100, 250);
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
        else if (k == "telemetry") telemetry = on;
        else if (k == "telemetry_port") telemetry_port = std::clamp(atoi(value), 1, 65535);
        else if (k == "telemetry_format") telemetry_format = v == "ogs-ts" || v == "ogs" ? 1 : 0;
    }
    fclose(f);
}

std::string FrontendSettings::text(bool section) const {
    std::string out;
    char buf[2048];
    if (!section) out += experience ? "experience = arcade\n" : "experience = pc\n";
    static const char* const AA[8] = {"msaa", "dlaa", "none", "dlss", "dlss_balanced", "dlss_performance", "dlss_ultra", "fxaa"};
    static const char* const MODELS[4] = {"auto", "k", "m", "l"};
    static const char* const BLOOM[4] = {"off", "low", "medium", "high"};   // also the vibration levels
    static const char* const SPEAKERS[3] = {"stereo", "surround", "headphones"};
    static const char* const RT_MODES[3] = {"off", "shadows", "ao"};
    snprintf(buf, sizeof(buf), "fullscreen = %d\nvolume = %d\nmusic = %d\naa = %s\nhaze = %d\nbloom = %s\nao = %d\nsun = %d\nshadows = %d\nsoft_particles = %d\nhdr = %d\nsharpen = %d\nhd_hud = %d\n"
               "saturation = %d\ngamma = %.2f\nmotion = %s\nspeakers = %s\nrear_boost = %d\nsample_interpolation = %s\n"
               "vibration = %s\nray_tracing = %s\n"
               "splash_screens = %d\nmenu_music = %d\n",
            display_mode != 0, volume, music, AA[std::clamp(aa, 0, 7)], haze, BLOOM[bloom], ao, sun, shadows, soft, hdr, sharpen, hd_hud, saturation,
            gamma / 100.0, smooth ? "smooth" : "original", SPEAKERS[speakers], rear_boost, cubic_samples ? "cubic" : "linear",
            BLOOM[vibration], RT_MODES[ray_tracing],
            splash, menu_music);
    out += buf;
    static const char* const MODES[3] = {"window", "borderless", "fullscreen"};
    std::string fps = max_fps < 0 ? "unlimited" : max_fps == 0 ? "monitor" : std::to_string(max_fps);
    snprintf(buf, sizeof(buf), "display_mode = %s\nresolution = %dx%d\nrender_scale = %d\nvsync = %s\nmax_fps = %s\nbackground_fps = %d\n",
             MODES[std::clamp(display_mode, 0, 2)], res_w, res_h, render_scale, vsync == 2 ? "adaptive" : vsync ? "on" : "off", fps.c_str(), background_fps);
    out += buf;
    snprintf(buf, sizeof(buf), "dlss_model = %s\n", MODELS[std::clamp(dlss_model, 0, 3)]);
    out += buf;
    if (!section) {
        static const char* const FORMATS[2] = {"dirt", "ogs-ts"};
        snprintf(buf, sizeof(buf), "telemetry = %d\ntelemetry_format = %s\ntelemetry_port = %d\n",
                 telemetry, FORMATS[std::clamp(telemetry_format, 0, 1)], telemetry_port);
        out += buf;
    }
    snprintf(buf, sizeof(buf), "car_light_probes = %d\ncar_shadows = %d\n", car_probes, car_shadows);
    out += buf;
    snprintf(buf, sizeof(buf), "rt_reflections = %d\nglobal_illumination = %s\ndust = %s\ndifficulty = %s\nhd_liveries = %d\ntrigger_rumble = %d\nobject_distance = %d\n", reflections, gi ? "low" : "off", dust ? "enhanced" : "original", DIFFICULTY_KEYS[std::clamp(difficulty, 0, 4)], hd_liveries, trigger_rumble, object_distance);
    out += buf;
    static const char* const ASPECTS[5] = {"auto", "32:9", "21:9", "16:9", "4:3"};
    snprintf(buf, sizeof(buf), "aspect = %s\nhud = %s\ngame_mode = %s\ndefault_view = %s\nspeed = %s\nfree_play = %d\n",
             ASPECTS[std::clamp(aspect, 0, 4)], hud_43 ? "4:3" : "wide", game_mode ? "special" : "normal", default_view ? "behind" : "driver",
             speed_kmh ? "kmh" : "mph", free_play);
    out += buf;
    return out;
}

float FrontendSettings::aspect_ratio(int aspect) {
    static const float RATIOS[5] = {32.0f / 9.0f, 32.0f / 9.0f, 64.0f / 27.0f, 16.0f / 9.0f, 4.0f / 3.0f};
    return RATIOS[std::clamp(aspect, 0, 4)];
}

FrontendSettings FrontendSettings::arcade_defaults() const {
    FrontendSettings a = *this;   // the screen, the speakers and the controls as in PC EDITION
    a.experience = 1;
    a.arcade_base();
    return a;
}

void FrontendSettings::arcade_base() {
    volume = FrontendSettings{}.volume;   // the effects and the music at their defaults, whatever PC EDITION has
    music = FrontendSettings{}.music;
    aa = 2;                               // no anti-aliasing: the Model 3 had none
    aa_saved = true;                      // (not the first run's choice for the GPU, app.cpp)
    hd_hud = hd_liveries = false;         // the ROM's own HUD, screens and car paint
    smooth = true;                        // MOTION SMOOTH (Sergi's choice; PC EDITION's default too)
    ray_tracing = 0;
    reflections = false;
    gi = 0;
    dust = false;
    object_distance = 100;                // the spectators appear 100 m ahead, as on the cabinet
    sun = shadows = hdr = ao = haze = soft = sharpen = false;
    car_probes = car_shadows = false;     // the cars lit as the game lights them, shade or not
    bloom = 0;
    saturation = gamma = 100;
    cubic_samples = false;                // SAMPLE QUALITY ORIGINAL: the sound chip's own interpolation
    aspect = 4;                           // the cabinet's 4:3 screen
    free_play = true;
}

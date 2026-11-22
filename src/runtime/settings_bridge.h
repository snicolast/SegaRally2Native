// Between the menu's settings (save/settings.ini, FrontendSettings) and the
// command-line Options: at launch the file fills the options, then the
// command line wins, and the menu shows what came out of both
#pragma once
#include "options.h"

struct FrontendSettings;

extern const float BLOOM_STRENGTH[4];   // menu levels OFF, LOW, MEDIUM, HIGH -> Options::bloom

// The graphics, sound and display options the menu keeps (the controls are
// set by app.cpp: they live outside Options)
void options_from_settings(Options& o, const FrontendSettings& ini);
void settings_from_options(FrontendSettings& s, const Options& o);

#pragma once

#include <string>

inline constexpr std::string_view SETTINGS_PATH = "sdmc:/3ds/Canary3DS/settings.ini";

enum class RepeatMode { OFF, ALL };

struct Settings {
    int volume = 8;  // 1-10 (maps to 0.1 – 1.0 for ndsp mix)
    RepeatMode repeat = RepeatMode::OFF;
    bool showCoverArt = true;
    bool sleepAllowed = false;  // false = keep playing when lid is closed
    std::string startPath = "sdmc:/Music/";
};

// Global settings instance — populated by loadSettings() at startup.
extern Settings g_settings;

// Load from SETTINGS_PATH; returns true on success, false if the file is
// missing or unreadable (in which case g_settings keeps its default values).
bool loadSettings();

// Persist g_settings to SETTINGS_PATH; returns true on success.
bool saveSettings();

// Push the current volume setting to the NDSP channel.
// Call once after audioInit() and again whenever volume changes.
void applyVolume();

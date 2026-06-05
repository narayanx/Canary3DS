#pragma once

#include <string>

inline constexpr std::string_view SETTINGS_PATH = "sdmc:/3ds/Canary/settings.ini";

enum class RepeatMode { OFF, ALL };

struct Settings {
    int volume = 8;  // 1-10 levels
    RepeatMode repeat = RepeatMode::OFF;
    bool showCoverArt = true;
    bool sleepAllowed = false;
    std::string startPath = "sdmc:/Music/";
    int brightness = 3;                  // 1-5 levels
    int seekSeconds = 10;                // seek duration in seconds
    bool lockToStartPath = true;         // prevent navigating above start path
    std::string accentColor = "Blue";    // palette name or "custom"
    std::string accentColor2 = "Green";  // palette name or "custom"
    unsigned int accentColorHex = 0;     // used when accentColor == "custom", stored as 0xRRGGBB
    unsigned secondaryColorHex = 0;      // used when accentColor2 == "custom", stored as 0xRRGGBB

    // Advanced
    int queueSize = 500;           // max entries in playQueue
    int historySize = 30;          // max entries in playHistory
    int maxDepth = 20;             // max entries saved in fileHistory
    bool showDebugScreen = false;  // initial value for showLog on startup
};

inline constexpr int SEEK_PRESETS[4] = {1, 5, 10, 30};

// Global settings instance - populated by loadSettings() at startup.
extern Settings g_settings;

// Load from SETTINGS_PATH; returns true on success, false if the file is
// missing or unreadable (in which case g_settings keeps its default values).
bool loadSettings();

// Persist g_settings to SETTINGS_PATH; returns true on success.
bool saveSettings();

// Push the current volume setting to the NDSP channel.
// Call once after audioInit() and again whenever volume changes.
void applyVolume();

// Push brightness setting to the LCD hardware.
void applyBrightness();

// Push accent / secondary color indices to the renderer globals.
void applyAccentColor();
void applySecondaryColor();

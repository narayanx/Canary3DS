#pragma once

#include <string>

inline constexpr std::string_view SETTINGS_PATH = "sdmc:/3ds/Canary/settings.ini";

struct Settings {
    int volumePercent = 100;        // 0-VOLUME_MAX_PERCENT
    int brightness = 3;             // 1-5 levels
    int seekSeconds = 10;           // seek duration in seconds
    int speedPercent = 100;         // playback speed, SPEED_MIN_PERCENT-SPEED_MAX_PERCENT
    int pitchSemitones = 0;         // pitch shift, PITCH_MIN_SEMITONES-PITCH_MAX_SEMITONES
    bool linkedSpeedPitch = false;  // when true, pitch follows speed
    std::string startPath = "sdmc:/Music/";
    bool lockToStartPath = true;  // prevent navigating above start path
    bool loopFolder = true;
    bool showCoverArt = true;
    bool allowClosedLidPlayback = true;
    bool autoSwitchToPlayer = true;    // switch to player screen automatically
    bool lockShoulderButtons = false;  // disable L/R multi-tap shortcuts
    bool pauseOnHeadphoneDisconnect = true;
    bool enableScrobbling = false;       // write finished tracks to .scrobbler.log
    std::string accentColor = "Blue";    // palette name or "custom"
    std::string accentColor2 = "Green";  // palette name or "custom"
    unsigned int accentColorHex = 0;     // used when accentColor == "custom", stored as 0xRRGGBB
    unsigned int secondaryColorHex = 0;  // used when accentColor2 == "custom", stored as 0xRRGGBB

    // Advanced
    int queueSize = 500;           // max entries in playQueue
    int historySize = 30;          // max entries in playHistory
    int maxDepth = 20;             // max entries saved in fileHistory
    bool showDebugScreen = false;  // initial value for showLog on startup
};

inline constexpr int SEEK_PRESETS[4] = {1, 5, 10, 30};

inline constexpr int VOLUME_STEP = 5;
inline constexpr int VOLUME_MAX_PERCENT = 200;

inline constexpr int SPEED_STEP = 5;
inline constexpr int SPEED_MIN_PERCENT = 25;
inline constexpr int SPEED_MAX_PERCENT = 400;

inline constexpr int PITCH_MIN_SEMITONES = -12;
inline constexpr int PITCH_MAX_SEMITONES = 12;

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

// Push the current speed/pitch settings to the audio thread's DSP
// processor. Call whenever they change, and once per song/seek so a fresh
// stream picks up the current values.
void applySpeedPitch();

// Push brightness setting to the LCD hardware.
void applyBrightness();

// Restore whatever brightness was active before program changed.
void restoreBrightness();

// Push accent / secondary color indices to the renderer globals.
void applyAccentColor();
void applySecondaryColor();

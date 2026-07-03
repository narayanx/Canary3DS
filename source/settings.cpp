#include "settings.h"

#include <3ds.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <string>
#include <vector>

#include "gfx.h"

Settings g_settings;

// Helpers
static void trim(std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    s = (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

// Ensure the directory that holds the settings file exists.
static void ensureDir() {
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/Canary", 0777);
}

bool loadSettings() {
    FILE *f = fopen(std::string(SETTINGS_PATH).c_str(), "r");
    if (!f) {
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        trim(s);
        if (s.empty() || s[0] == '#' || s[0] == ';') {
            continue;
        }
        size_t eq = s.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = s.substr(0, eq);
        std::string val = s.substr(eq + 1);
        trim(key);
        trim(val);

        if (key == "volume_percent") {
            try {
                int v = std::stoi(val);
                if (v >= 0 && v <= VOLUME_MAX_PERCENT) {
                    g_settings.volumePercent = v;
                }
            } catch (...) {
            }
        } else if (key == "loop_folder") {
            g_settings.loopFolder = (val == "1");
        } else if (key == "show_cover_art") {
            g_settings.showCoverArt = (val != "0");
        } else if (key == "allow_closed_lid_playback") {
            g_settings.allowClosedLidPlayback = (val == "1");
        } else if (key == "auto_switch_to_player") {
            g_settings.autoSwitchToPlayer = (val != "0");
        } else if (key == "pause_on_headphone_disconnect") {
            g_settings.pauseOnHeadphoneDisconnect = (val == "1");
        } else if (key == "music_folder") {
            if (!val.empty()) {
                if (val.back() != '/') {
                    val += '/';
                }
                g_settings.startPath = val;
            }
        } else if (key == "brightness") {
            try {
                int v = std::stoi(val);
                if (v >= 1 && v <= 5) {
                    g_settings.brightness = v;
                }
            } catch (...) {
            }
        } else if (key == "seek_seconds") {
            try {
                int v = std::stoi(val);
                if (v >= 1 && v <= 999) {
                    g_settings.seekSeconds = v;
                }
            } catch (...) {
            }
        } else if (key == "lock_to_music_folder") {
            g_settings.lockToStartPath = (val != "0");
        } else if (key == "accent_color") {
            if (val == "custom") {
                g_settings.accentColor = "custom";
            } else {
                for (int i = 0; i < ACCENT_COLOR_COUNT; i++) {
                    if (val == ACCENT_COLOR_NAMES[i]) {
                        g_settings.accentColor = val;
                        break;
                    }
                }
            }
        } else if (key == "accent_custom") {
            std::string h = val;
            if (!h.empty() && h[0] == '#') {
                h = h.substr(1);
            }
            if (h.size() == 6) {
                try {
                    g_settings.accentColorHex = (unsigned int) std::stoul(h, nullptr, 16);
                } catch (...) {
                }
            }
        } else if (key == "accent_color2") {
            if (val == "custom") {
                g_settings.accentColor2 = "custom";
            } else {
                for (int i = 0; i < SECONDARY_COLOR_COUNT; i++) {
                    if (val == SECONDARY_COLOR_NAMES[i]) {
                        g_settings.accentColor2 = val;
                        break;
                    }
                }
            }
        } else if (key == "accent2_custom") {
            std::string h = val;
            if (!h.empty() && h[0] == '#') {
                h = h.substr(1);
            }
            if (h.size() == 6) {
                try {
                    g_settings.secondaryColorHex = (unsigned int) std::stoul(h, nullptr, 16);
                } catch (...) {
                }
            }
        } else if (key == "queue_size") {
            try {
                int v = std::stoi(val);
                if (v >= 10 && v <= 9999) {
                    g_settings.queueSize = v;
                }
            } catch (...) {
            }
        } else if (key == "history_size") {
            try {
                int v = std::stoi(val);
                if (v >= 5 && v <= 200) {
                    g_settings.historySize = v;
                }
            } catch (...) {
            }
        } else if (key == "max_folder_history_depth") {
            try {
                int v = std::stoi(val);
                if (v >= 1 && v <= 50) {
                    g_settings.maxDepth = v;
                }
            } catch (...) {
            }
        } else if (key == "show_debug") {
            g_settings.showDebugScreen = (val == "1");
        }
    }

    fclose(f);
    return true;
}

bool saveSettings() {
    ensureDir();

    // In default write order
    static constexpr std::array<std::string_view, 18> KEYS = {{
        "volume_percent",
        "brightness",
        "seek_seconds",
        "music_folder",
        "lock_to_music_folder",
        "loop_folder",
        "show_cover_art",
        "allow_closed_lid_playback",
        "auto_switch_to_player",
        "pause_on_headphone_disconnect",
        "accent_color",
        "accent_custom",
        "accent_color2",
        "accent2_custom",
        "queue_size",
        "history_size",
        "max_folder_history_depth",
        "show_debug",
    }};

    // Returns the current formatted value for a known key
    auto valFor = [](std::string_view key) -> std::string {
        auto hex6 = [](unsigned int v) -> std::string {
            char buf[7];
            snprintf(buf, sizeof(buf), "%06X", v & 0xFFFFFFu);
            return buf;
        };

        if (key == "volume_percent") {
            return std::to_string(g_settings.volumePercent);
        }
        if (key == "loop_folder") {
            return g_settings.loopFolder ? "1" : "0";
        }
        if (key == "show_cover_art") {
            return g_settings.showCoverArt ? "1" : "0";
        }
        if (key == "allow_closed_lid_playback") {
            return g_settings.allowClosedLidPlayback ? "1" : "0";
        }
        if (key == "auto_switch_to_player") {
            return g_settings.autoSwitchToPlayer ? "1" : "0";
        }
        if (key == "pause_on_headphone_disconnect") {
            return g_settings.pauseOnHeadphoneDisconnect ? "1" : "0";
        }
        if (key == "music_folder") {
            return g_settings.startPath;
        }
        if (key == "brightness") {
            return std::to_string(g_settings.brightness);
        }
        if (key == "seek_seconds") {
            return std::to_string(g_settings.seekSeconds);
        }
        if (key == "lock_to_music_folder") {
            return g_settings.lockToStartPath ? "1" : "0";
        }
        if (key == "accent_color") {
            return g_settings.accentColor;
        }
        if (key == "accent_custom") {
            return hex6(g_settings.accentColorHex);
        }
        if (key == "accent_color2") {
            return g_settings.accentColor2;
        }
        if (key == "accent2_custom") {
            return hex6(g_settings.secondaryColorHex);
        }
        if (key == "queue_size") {
            return std::to_string(g_settings.queueSize);
        }
        if (key == "history_size") {
            return std::to_string(g_settings.historySize);
        }
        if (key == "max_folder_history_depth") {
            return std::to_string(g_settings.maxDepth);
        }
        if (key == "show_debug") {
            return g_settings.showDebugScreen ? "1" : "0";
        }
        return {};
    };

    std::vector<std::string> lines;
    std::array<bool, KEYS.size()> written = {};

    std::ifstream rf(std::string{SETTINGS_PATH});
    if (rf.is_open()) {
        std::string raw;
        while (std::getline(rf, raw)) {
            // Strip trailing CR for files with CRLF line endings
            if (!raw.empty() && raw.back() == '\r') {
                raw.pop_back();
            }
            std::string trimmed = raw;
            trim(trimmed);
            if (!trimmed.empty() && trimmed[0] != '#' && trimmed[0] != ';') {
                const auto eq = trimmed.find('=');
                if (eq != std::string::npos) {
                    std::string key = trimmed.substr(0, eq);
                    trim(key);
                    const auto it = std::find(KEYS.begin(), KEYS.end(), key);
                    if (it != KEYS.end()) {
                        const auto idx = static_cast<std::size_t>(it - KEYS.begin());
                        written[idx] = true;
                        raw = key + "=" + valFor(key);
                    }
                }
            }
            lines.push_back(std::move(raw));
        }
    } else {
        lines.emplace_back("# Canary Settings");
    }

    for (std::size_t i = 0; i < KEYS.size(); ++i) {
        if (written[i]) {
            continue;
        }
        const auto key = KEYS[i];

        lines.push_back(std::string{key} + "=" + valFor(key));
    }

    std::ofstream wf(std::string{SETTINGS_PATH});
    if (!wf.is_open()) {
        return false;
    }
    for (const auto &line : lines) {
        wf << line << '\n';
    }
    wf.flush();
    return wf.good();
}

void applyVolume() {
    float pct = (float) g_settings.volumePercent;
    float vol;
    if (pct <= 0.0f) {
        vol = 0.0f;
    } else if (pct <= 100.0f) {
        // dB scaling matches perceived loudness
        // Floor of -20dB at 0%, ramping up to unity (0dB) at 100%
        constexpr float MIN_DB = -20.0f;
        float dB = MIN_DB * (1.0f - pct / 100.0f);
        vol = powf(10.0f, dB / 20.0f);
    } else {
        // Plain linear headroom above unity, this is a boost past normal loudness
        vol = 1.0f + (pct - 100.0f) / 100.0f;
    }

    float mix[12] = {};
    mix[0] = vol;  // left  -> left
    mix[1] = vol;  // left  -> right
    mix[2] = vol;  // right -> left
    mix[3] = vol;  // right -> right
    ndspChnSetMix(0, mix);
}

static u32 s_originalBrightness = 0;
static bool s_originalBrightnessSaved = false;

void applyBrightness() {
    static const u32 BRIGHTNESS_RAW[5] = {16, 55, 95, 141, 182};
    int idx = g_settings.brightness - 1;
    if (idx < 0) {
        idx = 0;
    }
    if (idx > 4) {
        idx = 4;
    }
    if (R_SUCCEEDED(gspLcdInit())) {
        if (!s_originalBrightnessSaved) {
            // Remember original brightness, so we can put it back on exit
            s_originalBrightnessSaved =
                R_SUCCEEDED(GSPLCD_GetBrightness(GSPLCD_SCREEN_TOP, &s_originalBrightness));
        }
        GSPLCD_SetBrightnessRaw(GSPLCD_SCREEN_BOTH, BRIGHTNESS_RAW[idx]);
        gspLcdExit();
    }
}

void restoreBrightness() {
    if (!s_originalBrightnessSaved) {
        return;
    }
    if (R_SUCCEEDED(gspLcdInit())) {
        GSPLCD_SetBrightnessRaw(GSPLCD_SCREEN_BOTH, s_originalBrightness);
        gspLcdExit();
    }
}

void applyAccentColor() {
    if (g_settings.accentColor == "custom") {
        u8 r = (u8) ((g_settings.accentColorHex >> 16) & 0xFF);
        u8 g = (u8) ((g_settings.accentColorHex >> 8) & 0xFF);
        u8 b = (u8) (g_settings.accentColorHex & 0xFF);
        g_accentColor = C2D_Color32(r, g, b, 0xFF);
    } else {
        for (int i = 0; i < (int) ACCENT_COLOR_COUNT; i++) {
            if (g_settings.accentColor == ACCENT_COLOR_NAMES[i]) {
                g_accentColor = ACCENT_COLORS[i];
                return;
            }
        }
    }
}

void applySecondaryColor() {
    if (g_settings.accentColor2 == "custom") {
        u8 r = (u8) ((g_settings.secondaryColorHex >> 16) & 0xFF);
        u8 g = (u8) ((g_settings.secondaryColorHex >> 8) & 0xFF);
        u8 b = (u8) (g_settings.secondaryColorHex & 0xFF);
        g_secondaryColor = C2D_Color32(r, g, b, 0xFF);
    } else {
        for (int i = 0; i < (int) SECONDARY_COLOR_COUNT; i++) {
            if (g_settings.accentColor2 == SECONDARY_COLOR_NAMES[i]) {
                g_secondaryColor = SECONDARY_COLORS[i];
                return;
            }
        }
    }
}

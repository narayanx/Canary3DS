#include "settings.h"

#include <3ds.h>

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>

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
    mkdir("sdmc:/3ds/Canary3DS", 0777);
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

        if (key == "volume") {
            try {
                int v = std::stoi(val);
                if (v >= 1 && v <= 10) {
                    g_settings.volume = v;
                }
            } catch (...) {
            }
        } else if (key == "loop_folder") {
            g_settings.repeat = (val == "1") ? RepeatMode::ALL : RepeatMode::OFF;
        } else if (key == "show_cover_art") {
            g_settings.showCoverArt = (val != "0");
        } else if (key == "sleep_allowed") {
            g_settings.sleepAllowed = (val == "1");
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
    FILE *f = fopen(std::string(SETTINGS_PATH).c_str(), "w");
    if (!f) {
        return false;
    }

    fprintf(f, "# Canary3DS Settings\n");
    fprintf(f, "volume=%d\n", g_settings.volume);
    fprintf(f, "loop_folder=%d\n", g_settings.repeat == RepeatMode::ALL ? 1 : 0);
    fprintf(f, "show_cover_art=%d\n", g_settings.showCoverArt ? 1 : 0);
    fprintf(f, "sleep_allowed=%d\n", g_settings.sleepAllowed ? 1 : 0);
    fprintf(f, "music_folder=%s\n", g_settings.startPath.c_str());
    fprintf(f, "brightness=%d\n", g_settings.brightness);
    fprintf(f, "seek_seconds=%d\n", g_settings.seekSeconds);
    fprintf(f, "lock_to_music_folder=%d\n", g_settings.lockToStartPath ? 1 : 0);
    fprintf(f, "accent_color=%s\n", g_settings.accentColor.c_str());
    if (g_settings.accentColor == "custom") {
        fprintf(f, "accent_custom=%06X\n", g_settings.accentColorHex & 0xFFFFFFu);
    }
    fprintf(f, "accent_color2=%s\n", g_settings.accentColor2.c_str());
    if (g_settings.accentColor2 == "custom") {
        fprintf(f, "accent2_custom=%06X\n", g_settings.secondaryColorHex & 0xFFFFFFu);
    }
    fprintf(f, "queue_size=%d\n", g_settings.queueSize);
    fprintf(f, "history_size=%d\n", g_settings.historySize);
    fprintf(f, "max_folder_history_depth=%d\n", g_settings.maxDepth);
    fprintf(f, "show_debug=%d\n", g_settings.showDebugScreen ? 1 : 0);

    fclose(f);
    return true;
}

void applyVolume() {
    float vol = (float) g_settings.volume / 10.0f;
    float mix[12] = {};
    mix[0] = vol;  // left  -> left
    mix[1] = vol;  // left  -> right
    mix[2] = vol;  // right -> left
    mix[3] = vol;  // right -> right
    ndspChnSetMix(0, mix);
}

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
        GSPLCD_SetBrightnessRaw(GSPLCD_SCREEN_BOTH, BRIGHTNESS_RAW[idx]);
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

#include "settings.h"

#include <3ds.h>

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>

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

// loadSettings
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
            int v = std::stoi(val);
            if (v >= 1 && v <= 10) {
                g_settings.volume = v;
            }
        } else if (key == "repeat") {
            g_settings.repeat = (val == "1") ? RepeatMode::ALL : RepeatMode::OFF;
        } else if (key == "show_cover_art") {
            g_settings.showCoverArt = (val != "0");
        } else if (key == "sleep_allowed") {
            g_settings.sleepAllowed = (val == "1");
        } else if (key == "start_path") {
            if (!val.empty()) {
                // Ensure trailing slash
                if (val.back() != '/') {
                    val += '/';
                }
                g_settings.startPath = val;
            }
        }
    }

    fclose(f);
    return true;
}

// saveSettings
bool saveSettings() {
    ensureDir();
    FILE *f = fopen(std::string(SETTINGS_PATH).c_str(), "w");
    if (!f) {
        return false;
    }

    fprintf(f, "# Canary3DS settings\n");
    fprintf(f, "volume=%d\n", g_settings.volume);
    fprintf(f, "repeat=%d\n", g_settings.repeat == RepeatMode::ALL ? 1 : 0);
    fprintf(f, "show_cover_art=%d\n", g_settings.showCoverArt ? 1 : 0);
    fprintf(f, "sleep_allowed=%d\n", g_settings.sleepAllowed ? 1 : 0);
    fprintf(f, "start_path=%s\n", g_settings.startPath.c_str());

    fclose(f);
    return true;
}

// applyVolume
void applyVolume() {
    float vol = (float) g_settings.volume / 10.0f;
    float mix[12] = {};
    mix[0] = vol;  // left  → left
    mix[1] = vol;  // left  → right
    mix[2] = vol;  // right → left
    mix[3] = vol;  // right → right
    ndspChnSetMix(0, mix);
}

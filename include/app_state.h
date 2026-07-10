#pragma once

#include <citro2d.h>
#include <citro3d.h>

#include <cstdio>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "gfx.h"
#include "playlist.h"
#include "settings.h"
#include "toggle_settings.h"

enum class TopScreenState { FILEBROWSER, INFO, PLAYLIST_BROWSER, PLAYLIST_VIEW, SETTINGS };

struct FileBrowserState {
    size_t scroll = 0;

    // Folder-picker mode
    bool folderPickerMode = false;
    std::string pickerSavedCwd;
    size_t pickerSavedSel = 0;
    size_t pickerSavedScroll = 0;
    std::deque<std::pair<size_t, size_t>> pickerSavedHistory;

    // Set to true when startPath changes in settings.
    // main.cpp re-runs initFileHistory and clears this flag.
    bool reinitPending = false;
};

struct PlaylistState {
    std::vector<Playlist> playlists;
    size_t sel = 0;
    size_t selSong = 0;
    size_t browserScroll = 0;
    size_t viewScroll = 0;
    bool dirty = true;
    bool inHeader = false;
    int headerBtnSel = 0;
    bool reorderMode = false;
    bool reorderPicked = false;
    bool reorderDirty = false;
    C2D_Image coverImage{};
    C3D_Tex coverTex{};
    Tex3DS_SubTexture coverSubtex{};
    bool hasCover = false;
    std::string coverLoadedFrom;
};

struct InfoState {
    int scrollTop = 0;
    std::vector<std::string> autoplayItems;
    C2D_Image image{};
    C3D_Tex tex{};
    Tex3DS_SubTexture subtex{};
    bool hasCover = false;
    bool displayCover = true;
    bool seekDragging = false;
    float seekDragProgress = 0.0f;
    // Queue reorder mode
    bool reorderMode = false;
    bool reorderPicked = false;
    int reorderFromIdx = -1;  // original queue index of the picked song, -1 when not picked
};

struct SettingsState {
    size_t sel = 0;
    size_t scrollOffset = 0;

    // Row indices
    enum : size_t {
        ROW_VOLUME,
        ROW_BRIGHTNESS,
        ROW_SEEK,
        ROW_START_PATH,
        ROW_LOCK_START,
        ROW_REPEAT,
        ROW_COVER_ART,
        ROW_SLEEP,
        ROW_AUTO_SWITCH_PLAYER,
        ROW_LOCK_SHOULDER_BUTTONS,
        ROW_PAUSE_ON_HEADPHONE_DISCONNECT,
        ROW_SCROBBLING,
        ROW_ACCENT,
        ROW_SECONDARY,
        ROW_ADV_HEADER,
        ROW_QUEUE_SIZE,
        ROW_HISTORY_SIZE,
        ROW_MAX_DEPTH,
        ROW_DEBUG,
        ROW_RESET,
        ROW_COUNT,
    };

    static constexpr size_t VISIBLE_ROWS = 11;

    static bool isHeaderRow(size_t row) {
        return row == ROW_ADV_HEADER;
    }

    // Sentinel bytes appended to a row's label to indicate to draw toggle icon
    static constexpr char TOGGLE_ON = '\x01';
    static constexpr char TOGGLE_OFF = '\x02';

    static std::vector<std::string> buildRows() {
        auto toggle = [](bool b) -> char { return b ? TOGGLE_ON : TOGGLE_OFF; };

        char vol[48], bri[48], seek[48], acc[48], sec[48], qsz[48], hsz[48], dep[48];

        std::vector<std::string> rows(ROW_COUNT);

        // Toggle rows are table driven
        for (size_t i = 0; i < TOGGLE_SETTINGS_COUNT; ++i) {
            const ToggleSetting &t = TOGGLE_SETTINGS[i];
            char buf[48];
            snprintf(buf, sizeof(buf), "%s: %c", t.label, toggle(g_settings.*t.value));
            rows[t.row] = buf;
        }

        snprintf(vol, sizeof(vol), "Volume:  %d%%", g_settings.volumePercent);
        snprintf(bri, sizeof(bri), "Brightness:  %d / 5", g_settings.brightness);

        bool seekIsPreset = false;
        for (int p : SEEK_PRESETS) {
            if (g_settings.seekSeconds == p) {
                seekIsPreset = true;
                break;
            }
        }
        if (seekIsPreset) {
            snprintf(seek, sizeof(seek), "Seek:  %ds", g_settings.seekSeconds);
        } else {
            snprintf(seek, sizeof(seek), "Seek:  %ds (custom)", g_settings.seekSeconds);
        }

        std::string pathRow = "Music Folder:   " + g_settings.startPath;

        if (g_settings.accentColor == "custom") {
            snprintf(
                acc, sizeof(acc), "Accent Color:  #%06X", g_settings.accentColorHex & 0xFFFFFFu);
        } else {
            snprintf(acc, sizeof(acc), "Accent Color:  %s", g_settings.accentColor.c_str());
        }
        if (g_settings.accentColor2 == "custom") {
            snprintf(sec,
                     sizeof(sec),
                     "Secondary Accent Color:  #%06X",
                     g_settings.secondaryColorHex & 0xFFFFFFu);
        } else {
            snprintf(
                sec, sizeof(sec), "Secondary Accent Color:  %s", g_settings.accentColor2.c_str());
        }

        const std::string advHeader = "--- Advanced";

        snprintf(qsz, sizeof(qsz), "Max Queue Size:  %d", g_settings.queueSize);
        snprintf(hsz, sizeof(hsz), "Max History Size:  %d", g_settings.historySize);
        snprintf(dep, sizeof(dep), "Max Depth:  %d", g_settings.maxDepth);

        rows[ROW_VOLUME] = vol;
        rows[ROW_BRIGHTNESS] = bri;
        rows[ROW_SEEK] = seek;
        rows[ROW_START_PATH] = pathRow;
        rows[ROW_ACCENT] = acc;
        rows[ROW_SECONDARY] = sec;
        rows[ROW_ADV_HEADER] = advHeader;
        rows[ROW_QUEUE_SIZE] = qsz;
        rows[ROW_HISTORY_SIZE] = hsz;
        rows[ROW_MAX_DEPTH] = dep;
        rows[ROW_RESET] = "Reset to Defaults";
        return rows;
    }
};

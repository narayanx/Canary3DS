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
    static constexpr size_t ROW_VOLUME = 0;
    static constexpr size_t ROW_BRIGHTNESS = 1;
    static constexpr size_t ROW_SEEK = 2;
    static constexpr size_t ROW_START_PATH = 3;
    static constexpr size_t ROW_LOCK_START = 4;
    static constexpr size_t ROW_REPEAT = 5;
    static constexpr size_t ROW_COVER_ART = 6;
    static constexpr size_t ROW_SLEEP = 7;
    static constexpr size_t ROW_ACCENT = 8;
    static constexpr size_t ROW_SECONDARY = 9;
    static constexpr size_t ROW_ADV_HEADER = 10;
    static constexpr size_t ROW_QUEUE_SIZE = 11;
    static constexpr size_t ROW_HISTORY_SIZE = 12;
    static constexpr size_t ROW_MAX_DEPTH = 13;
    static constexpr size_t ROW_DEBUG = 14;
    static constexpr size_t ROW_RESET = 15;
    static constexpr size_t ROW_COUNT = 16;

    static constexpr size_t VISIBLE_ROWS = 11;

    static bool isHeaderRow(size_t row) {
        return row == ROW_ADV_HEADER;
    }

    // Sentinel bytes appended to a row's label to indicate to draw toggle icon
    static constexpr char TOGGLE_ON = '\x01';
    static constexpr char TOGGLE_OFF = '\x02';

    static std::vector<std::string> buildRows() {
        auto toggle = [](bool b) -> char { return b ? TOGGLE_ON : TOGGLE_OFF; };

        char vol[48], bri[48], seek[48], lock[48], rep[48], cov[48], slp[48], acc[48], sec[48],
            qsz[48], hsz[48], dep[48], dbg[48];

        snprintf(vol, sizeof(vol), "Volume:  %d / 10", g_settings.volume);
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

        snprintf(lock,
                 sizeof(lock),
                 "Prevent Exiting Music Folder: %c",
                 toggle(g_settings.lockToStartPath));
        snprintf(rep, sizeof(rep), "Loop Folder: %c", toggle(g_settings.loopFolder));
        snprintf(cov, sizeof(cov), "Cover Art: %c", toggle(g_settings.showCoverArt));
        snprintf(slp,
                 sizeof(slp),
                 "Play with Lid Closed: %c",
                 toggle(g_settings.allowClosedLidPlayback));

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
        snprintf(
            dbg, sizeof(dbg), "Enable Dev Debug Screen: %c", toggle(g_settings.showDebugScreen));

        return {vol,
                bri,
                seek,
                pathRow,
                lock,
                rep,
                cov,
                slp,
                acc,
                sec,
                advHeader,
                qsz,
                hsz,
                dep,
                dbg,
                "Reset to Defaults"};
    }
};

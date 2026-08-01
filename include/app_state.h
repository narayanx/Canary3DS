#pragma once

#include <citro2d.h>
#include <citro3d.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "gfx.h"
#include "image.h"
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

// State for the Speed/Pitch pad overlay shown on the bottom screen.
// Dragging tracks whether the current touch started inside the plot area, so
// the point keeps following the finger even if it strays slightly outside the plot bounds.
struct SpeedPitchPadState {
    bool active = false;
    bool dragging = false;
};

struct SelectionRangeState {
    bool active = false;
    bool selecting = true;  // action decided by the anchor row's state when Y was first pressed
    int anchorIndex = 0;
    int lastCursorIndex = 0;
    // Selection state of each row index the drag has touched (used to revert rows that leave range)
    std::map<int, bool> originalState;
};

struct SettingsState {
    size_t sel = 0;
    size_t scrollOffset = 0;

    // Row indices, grouped by the foldable category they belong to
    enum : size_t {
        ROW_CAT_GENERAL,
        ROW_VOLUME,
        ROW_BRIGHTNESS,
        ROW_SEEK,
        ROW_SPEED,
        ROW_PITCH,
        ROW_LINK_SPEED_PITCH,
        ROW_START_PATH,
        ROW_SLEEP,
        ROW_AUTO_SWITCH_PLAYER,
        ROW_SWAP_SELECT_Y,
        ROW_LOCK_SHOULDER_BUTTONS,
        ROW_PAUSE_ON_HEADPHONE_DISCONNECT,
        ROW_SCROBBLING,
        ROW_ACCENT,
        ROW_SECONDARY,

        ROW_CAT_FILEBROWSER,
        ROW_LOCK_START,
        ROW_SORT_BY,
        ROW_REVERSE_SORT,
        ROW_REPEAT,
        ROW_AUTOPLAY_NEXT_FOLDER,

        ROW_CAT_PLAYER,
        ROW_COVER_ART,

        ROW_CAT_ADVANCED,
        ROW_QUEUE_SIZE,
        ROW_HISTORY_SIZE,
        ROW_MAX_DEPTH,
        ROW_CACHE_COVER_ART,
        ROW_CLEAR_COVER_CACHE,
        ROW_DEBUG,
        ROW_RESET,
        ROW_COUNT,
    };

    static constexpr size_t VISIBLE_ROWS = 11;

    enum class Category : int { GENERAL, FILEBROWSER, PLAYER, ADVANCED, COUNT };

    // Whether each category's rows are hidden. General starts open, the rest start folded.
    bool categoryFolded[(int) Category::COUNT] = {false, true, true, true};

    static bool isCategoryHeaderRow(size_t row) {
        return row == ROW_CAT_GENERAL || row == ROW_CAT_FILEBROWSER || row == ROW_CAT_PLAYER ||
               row == ROW_CAT_ADVANCED;
    }

    static Category headerCategory(size_t row) {
        switch (row) {
            case ROW_CAT_GENERAL:
                return Category::GENERAL;
            case ROW_CAT_FILEBROWSER:
                return Category::FILEBROWSER;
            case ROW_CAT_PLAYER:
                return Category::PLAYER;
            default:
                return Category::ADVANCED;
        }
    }

    // Category a (non header) row belongs to, based on which header block it falls after
    static Category categoryForRow(size_t row) {
        if (row > ROW_CAT_GENERAL && row < ROW_CAT_FILEBROWSER) {
            return Category::GENERAL;
        }
        if (row > ROW_CAT_FILEBROWSER && row < ROW_CAT_PLAYER) {
            return Category::FILEBROWSER;
        }
        if (row > ROW_CAT_PLAYER && row < ROW_CAT_ADVANCED) {
            return Category::PLAYER;
        }
        return Category::ADVANCED;
    }

    void toggleCategoryFold(size_t headerRow) {
        categoryFolded[(int) headerCategory(headerRow)] =
            !categoryFolded[(int) headerCategory(headerRow)];
    }

    std::vector<size_t> visibleRows() const {
        std::vector<size_t> v;
        v.reserve(ROW_COUNT);
        for (size_t r = 0; r < ROW_COUNT; ++r) {
            if (isCategoryHeaderRow(r) || r == ROW_RESET ||
                !categoryFolded[(int) categoryForRow(r)]) {
                v.push_back(r);
            }
        }
        return v;
    }

    void clampScroll() {
        std::vector<size_t> visible = visibleRows();
        auto it = std::find(visible.begin(), visible.end(), sel);
        size_t pos = (it != visible.end()) ? (size_t) (it - visible.begin()) : 0;
        if (pos < scrollOffset) {
            scrollOffset = pos;
        } else if (pos >= scrollOffset + VISIBLE_ROWS) {
            scrollOffset = pos - VISIBLE_ROWS + 1;
        }
    }

    // Sentinel bytes appended to a row's label to indicate to draw toggle icon
    static constexpr char TOGGLE_ON = '\x01';
    static constexpr char TOGGLE_OFF = '\x02';

    // Sentinel bytes appended to a category header's label to indicate fold state
    static constexpr char CAT_OPEN = '\x03';
    static constexpr char CAT_FOLDED = '\x04';

    static std::string formatCacheSize(uint64_t bytes) {
        char buf[32];
        if (bytes >= 1024ull * 1024ull) {
            snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
        } else if (bytes >= 1024ull) {
            snprintf(buf, sizeof(buf), "%.0f KB", bytes / 1024.0);
        } else {
            snprintf(buf, sizeof(buf), "%llu B", (unsigned long long) bytes);
        }
        return buf;
    }

    std::vector<std::string> buildRows() const {
        auto toggle = [](bool b) -> char { return b ? TOGGLE_ON : TOGGLE_OFF; };
        auto catLabel = [&](const char *name, Category c) -> std::string {
            return std::string(name) + (categoryFolded[(int) c] ? CAT_FOLDED : CAT_OPEN);
        };

        char vol[48], bri[48], seek[48], spd[48], pit[48], srt[48], acc[48], sec[48], qsz[48],
            hsz[48], dep[48];

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

        snprintf(spd, sizeof(spd), "Speed:  %.2fx", g_settings.speedPercent / 100.0f);
        if (g_settings.linkedSpeedPitch) {
            snprintf(pit, sizeof(pit), "Pitch:  Linked to Speed");
        } else if (g_settings.pitchSemitones > 0) {
            snprintf(pit, sizeof(pit), "Pitch:  +%.1f st", g_settings.pitchSemitones);
        } else {
            snprintf(pit, sizeof(pit), "Pitch:  %.1f st", g_settings.pitchSemitones);
        }

        std::string pathRow = "Music Folder:   " + g_settings.startPath;

        snprintf(srt, sizeof(srt), "Sort By:  %s", g_settings.sortBy.c_str());

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

        snprintf(qsz, sizeof(qsz), "Max Queue Size:  %d", g_settings.queueSize);
        snprintf(hsz, sizeof(hsz), "Max History Size:  %d", g_settings.historySize);
        snprintf(dep, sizeof(dep), "Max Depth:  %d", g_settings.maxDepth);

        CoverCacheStats cacheStats = getCoverCacheStats();
        char clearCache[80];
        snprintf(clearCache,
                 sizeof(clearCache),
                 "Clear Cover Cache  (%zu, %s)",
                 cacheStats.fileCount,
                 formatCacheSize(cacheStats.totalBytes).c_str());

        rows[ROW_VOLUME] = vol;
        rows[ROW_BRIGHTNESS] = bri;
        rows[ROW_SEEK] = seek;
        rows[ROW_SPEED] = spd;
        rows[ROW_PITCH] = pit;
        rows[ROW_START_PATH] = pathRow;
        rows[ROW_SORT_BY] = srt;
        rows[ROW_ACCENT] = acc;
        rows[ROW_SECONDARY] = sec;
        rows[ROW_CAT_GENERAL] = catLabel("General", Category::GENERAL);
        rows[ROW_CAT_FILEBROWSER] = catLabel("Filebrowser", Category::FILEBROWSER);
        rows[ROW_CAT_PLAYER] = catLabel("Player", Category::PLAYER);
        rows[ROW_CAT_ADVANCED] = catLabel("Advanced", Category::ADVANCED);
        rows[ROW_QUEUE_SIZE] = qsz;
        rows[ROW_HISTORY_SIZE] = hsz;
        rows[ROW_MAX_DEPTH] = dep;
        rows[ROW_CLEAR_COVER_CACHE] = clearCache;
        rows[ROW_RESET] = "Reset to Defaults";
        return rows;
    }
};

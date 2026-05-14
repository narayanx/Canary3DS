#pragma once

#include <citro2d.h>
#include <citro3d.h>
#include <cstdio>
#include <string>
#include <vector>

#include "playlist.h"
#include "settings.h"

enum class TopScreenState { FILEBROWSER, INFO, PLAYLIST_BROWSER, PLAYLIST_VIEW, SETTINGS };

struct FileBrowserState {
    size_t scroll = 0;
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
};

struct SettingsState {
    size_t sel = 0;

    // Build display strings for each row from current g_settings values
    static std::vector<std::string> buildRows() {
        auto yn = [](bool b) -> const char * { return b ? "Yes" : "No"; };
        auto repeat = [](RepeatMode r) -> const char * {
            return r == RepeatMode::ALL ? "All" : "Off";
        };

        char vol[32];
        snprintf(vol, sizeof(vol), "Volume:       %d / 10", g_settings.volume);

        char rep[32];
        snprintf(rep, sizeof(rep), "Repeat:       %s", repeat(g_settings.repeat));

        char cov[32];
        snprintf(cov, sizeof(cov), "Cover Art:    %s", yn(g_settings.showCoverArt));

        char slp[48];
        snprintf(
            slp, sizeof(slp), "Sleep (lid):  %s", g_settings.sleepAllowed ? "Allowed" : "Blocked");

        std::string path = "Start Path:   " + g_settings.startPath;

        return {vol, rep, cov, slp, path};
    }

    static constexpr size_t ROW_VOLUME = 0;
    static constexpr size_t ROW_REPEAT = 1;
    static constexpr size_t ROW_COVER_ART = 2;
    static constexpr size_t ROW_SLEEP = 3;
    static constexpr size_t ROW_START_PATH = 4;
    static constexpr size_t ROW_COUNT = 5;
};

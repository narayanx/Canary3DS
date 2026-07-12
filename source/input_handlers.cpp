#include "input_handlers.h"

#include <3ds.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

#include "audio_decoder.h"
#include "audio_engine.h"
#include "filebrowser.h"
#include "gfx.h"
#include "playlist.h"
#include "settings.h"
#include "toggle_settings.h"

static constexpr u64 MULTI_TAP_WINDOW_MS = 350;

// Switch to the screen for bottom-screen nav tab
static void performNavSwitch(int i,
                             TopScreenState &screenState,
                             FileBrowserState &fb,
                             PlaylistState &pl,
                             InfoState &info,
                             SettingsState &st) {
    if (fb.folderPickerMode && i != 0) {
        exitFolderPickerMode(screenState, fb, false);
    }
    if (info.reorderMode) {
        info.reorderMode = false;
        info.reorderPicked = false;
        info.reorderFromIdx = -1;
    }
    if (i == 0) {
        screenState = TopScreenState::FILEBROWSER;
    } else if (i == 1) {
        screenState = TopScreenState::INFO;
        fileController.selectedQueueItem = 0;
        info.scrollTop = 0;
    } else if (i == 2) {
        pl.dirty = true;
        screenState = TopScreenState::PLAYLIST_BROWSER;
    } else if (i == 3) {
        screenState = TopScreenState::SETTINGS;
        st.sel = 0;
        st.scrollOffset = 0;
    }
}

// Play or shuffle-play all songs in the currently viewed playlist
// Shows a confirmation popup if the queue is non-empty
static void
triggerPlaylistPlay(PlaylistState &pl, CtxMenu &s_ctx, TopScreenState &screenState, bool shuffle) {
    if (pl.playlists.empty() || pl.sel >= pl.playlists.size()) {
        return;
    }
    const Playlist &lst = pl.playlists[pl.sel];
    if (lst.songs.empty()) {
        return;
    }

    std::vector<std::string> songs = lst.songs;
    if (shuffle) {
        std::mt19937 g = makeShuffleRng();
        std::shuffle(songs.begin(), songs.end(), g);
    }

    auto doPlay = [&pl, &s_ctx, &screenState, songs]() {
        clearQueue();
        stopPlaybackIfPlaying();
        if (playSong(songs[0])) {
            for (size_t i = 1; i < songs.size(); ++i) {
                enqueueSong(songs[i]);
            }
            if (g_settings.autoSwitchToPlayer) {
                screenState = TopScreenState::INFO;
            }
        }
        s_ctx.close();
    };

    if (!fileController.playQueue.empty()) {
        s_ctx.close();
        const char *lbl = shuffle ? "Shuffle  (clears queue)" : "Play  (clears queue)";
        s_ctx.add(lbl, doPlay);
        s_ctx.add("Cancel", [&s_ctx]() { s_ctx.close(); });
        s_ctx.open(60.0f, 60.0f);
    } else {
        doPlay();
    }
}

// Open the playlist-picker submenu for song path
static void openAddToPlaylistSub(PlaylistState &pl,
                                 CtxMenu &s_ctx,
                                 CtxMenu &s_sub,
                                 const std::string &songPath) {
    pl.playlists = loadPlaylists();
    if (pl.playlists.empty()) {
        logToDebugScreen("No playlists. Create one first.");
        s_ctx.close();
        return;
    }
    s_sub.close();
    for (size_t i = 0; i < pl.playlists.size(); ++i) {
        s_sub.add(pl.playlists[i].name, [&pl, &s_sub, &s_ctx, songPath, i]() {
            if (addSongToPlaylist(pl.playlists[i].path, songPath)) {
                pl.playlists[i].songs.push_back(songPath);
                logToDebugScreen("Added to \"" + pl.playlists[i].name + "\"");
            } else {
                logToDebugScreen("Failed to add to playlist");
            }
            s_sub.active = false;
            s_ctx.close();
        });
    }
    s_sub.open(s_ctx.x + 10.0f, s_ctx.y + 20.0f);
}

// Open the playlist picker submenu for a list of songs (eg: queue, folder)
static void openAddSongsToPlaylistSub(PlaylistState &pl,
                                      CtxMenu &s_ctx,
                                      CtxMenu &s_sub,
                                      const std::vector<std::string> &songs) {
    if (songs.empty()) {
        logToDebugScreen("No songs to add");
        s_ctx.close();
        return;
    }
    pl.playlists = loadPlaylists();
    if (pl.playlists.empty()) {
        logToDebugScreen("No playlists. Create one first.");
        s_ctx.close();
        return;
    }
    s_sub.close();
    for (size_t i = 0; i < pl.playlists.size(); ++i) {
        s_sub.add(pl.playlists[i].name, [&pl, &s_sub, &s_ctx, songs, i]() {
            int added = 0;
            for (const auto &song : songs) {
                if (addSongToPlaylist(pl.playlists[i].path, song)) {
                    pl.playlists[i].songs.push_back(song);
                    ++added;
                }
            }
            logToDebugScreen("Added " + std::to_string(added) + " song(s) to \"" +
                             pl.playlists[i].name + "\"");
            s_sub.active = false;
            s_ctx.close();
        });
    }
    s_sub.open(s_ctx.x + 10.0f, s_ctx.y + 20.0f);
}

static std::vector<std::string> getFolderSongs(const std::string &folderPath) {
    std::vector<std::string> songs;
    DIR *d = opendir(folderPath.c_str());
    if (!d) {
        return songs;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_type == DT_REG) {
            std::string p = folderPath + ent->d_name;
            if (isSupportedAudioFile(p)) {
                songs.push_back(p);
            }
        }
    }
    closedir(d);
    std::sort(songs.begin(), songs.end());
    return songs;
}

static void openSeekKeyboard() {
    SwkbdState swkbd;
    char buf[16] = {};
    snprintf(buf, sizeof(buf), "%d", g_settings.seekSeconds);
    swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 2, 4);
    swkbdSetHintText(&swkbd, "Seek seconds (1-999)");
    swkbdSetInitialText(&swkbd, buf);
    if (swkbdInputText(&swkbd, buf, sizeof(buf)) == SWKBD_BUTTON_CONFIRM && buf[0]) {
        try {
            int v = std::stoi(buf);
            if (v >= 1 && v <= 999) {
                g_settings.seekSeconds = v;
            }
        } catch (...) {
        }
    }
}

static void openVolumeKeyboard() {
    SwkbdState swkbd;
    char buf[16] = {};
    snprintf(buf, sizeof(buf), "%d", g_settings.volumePercent);
    swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 2, 3);
    swkbdSetHintText(&swkbd, "Volume percent (0-200)");
    swkbdSetInitialText(&swkbd, buf);
    if (swkbdInputText(&swkbd, buf, sizeof(buf)) == SWKBD_BUTTON_CONFIRM && buf[0]) {
        try {
            int v = std::stoi(buf);
            if (v >= 0 && v <= VOLUME_MAX_PERCENT) {
                g_settings.volumePercent = v;
                applyVolume();
            }
        } catch (...) {
        }
    }
}

void enterFolderPickerMode(TopScreenState &screenState, FileBrowserState &fb) {
    fb.pickerSavedCwd = fileController.cwd;
    fb.pickerSavedSel = fileController.selectedFile;
    fb.pickerSavedScroll = fb.scroll;
    fb.pickerSavedHistory = fileController.fileHistory;

    fileController.cwd = g_settings.startPath;
    fileController.fileHistory.clear();
    fileController.selectedFile = 0;
    fb.scroll = 0;
    fileController.files = getFiles(fileController.cwd.c_str());
    fileController.filesShown = fileController.files.size();

    fb.folderPickerMode = true;
    screenState = TopScreenState::FILEBROWSER;
}

void exitFolderPickerMode(TopScreenState &screenState, FileBrowserState &fb, bool confirmPath) {
    if (confirmPath) {
        g_settings.startPath = fileController.cwd;
        saveSettings();
        fb.reinitPending = true;
        logToDebugScreen("Start path: " + g_settings.startPath);
    }

    fileController.cwd = fb.pickerSavedCwd;
    fileController.fileHistory = fb.pickerSavedHistory;
    fileController.selectedFile = fb.pickerSavedSel;
    fb.scroll = fb.pickerSavedScroll;
    fileController.files = getFiles(fileController.cwd.c_str());
    fileController.filesShown = (fileController.files.size() > FILE_LAZY_THRESHOLD)
                                    ? std::min(fileController.files.size(), FILE_PAGE_SIZE)
                                    : fileController.files.size();
    if (!fileController.files.empty() && fileController.selectedFile >= fileController.filesShown) {
        fileController.filesShown =
            std::min(fileController.files.size(), fileController.selectedFile + 1);
    }

    fb.folderPickerMode = false;
    screenState = TopScreenState::SETTINGS;
}

void handleNavTouch(touchPosition touchPos,
                    bool newTouch,
                    TopScreenState &screenState,
                    FileBrowserState &fb,
                    PlaylistState &pl,
                    InfoState &info,
                    SettingsState &st,
                    CtxMenu &s_ctx,
                    CtxMenu &s_sub) {
    if (!newTouch) {
        return;
    }
    float px = (float) touchPos.px, py = (float) touchPos.py;
    if (py >= NAV_BTN_Y && py <= NAV_BTN_Y + NAV_BTN_H) {
        for (int i = 0; i < NAV_BTN_COUNT; ++i) {
            if (px >= NAV_BTN_X[i] && px <= NAV_BTN_X[i] + NAV_BTN_W) {
                if (screenState == TopScreenState::PLAYLIST_VIEW && pl.reorderMode &&
                    pl.reorderDirty) {
                    s_sub.active = false;
                    s_ctx.close();
                    s_ctx.add("Save changes", [&pl, &s_ctx, &screenState, &fb, &info, &st, i]() {
                        if (!pl.playlists.empty() && pl.sel < pl.playlists.size() &&
                            writePlaylistSongOrder(pl.playlists[pl.sel].path,
                                                   pl.playlists[pl.sel].songs)) {
                            logToDebugScreen("Playlist order saved");
                        } else {
                            logToDebugScreen("Failed to save playlist order");
                        }
                        pl.reorderMode = false;
                        pl.reorderPicked = false;
                        pl.reorderDirty = false;
                        s_ctx.close();
                        performNavSwitch(i, screenState, fb, pl, info, st);
                    });
                    s_ctx.add("Discard changes", [&pl, &s_ctx, &screenState, &fb, &info, &st, i]() {
                        pl.reorderMode = false;
                        pl.reorderPicked = false;
                        pl.reorderDirty = false;
                        pl.dirty = true;
                        s_ctx.close();
                        performNavSwitch(i, screenState, fb, pl, info, st);
                    });
                    s_ctx.add("Cancel", [&s_ctx]() { s_ctx.close(); });
                    s_ctx.open(60.0f, 60.0f);
                    break;
                }
                s_sub.active = false;
                s_ctx.close();
                performNavSwitch(i, screenState, fb, pl, info, st);
                break;
            }
        }
    }
    if (py >= LOOP_BTN_Y && py <= LOOP_BTN_Y + LOOP_BTN_H) {
        if (px >= LOOP_BTN_X && px <= LOOP_BTN_X + LOOP_BTN_W) {
            audioController.loopOne = !audioController.loopOne;
            logToDebugScreen(audioController.loopOne ? "Loop: on" : "Loop: off");
        } else if (px >= SHUFFLE_BTN_X && px <= SHUFFLE_BTN_X + LOOP_BTN_W) {
            toggleShuffle();
        }
    }
    if (py >= PLAY_BTN_Y && py <= PLAY_BTN_Y + PLAY_BTN_H) {
        if (px >= PREV_BTN_X && px <= PREV_BTN_X + PREV_BTN_W) {
            if (audioController.songReady && !fileController.playHistory.empty()) {
                std::string prevSong = fileController.playHistory.front();
                fileController.playHistory.pop_front();
                fileController.playQueue.push_front(audioController.songPath);
                fileController.playbackOrder.insert(fileController.playbackOrder.begin(),
                                                    audioController.songPath);
                audioController.skipNextHistoryEntry = true;
                audioController.pendingStartPaused = ndspChnIsPaused(0);
                audioController.applyPendingStartPaused = true;
                stopPlaybackIfPlaying();
                playSong(prevSong);
            }
        } else if (px >= PLAY_PAUSE_X && px <= PLAY_PAUSE_X + PLAY_PAUSE_W) {
            if (audioController.songReady) {
                ndspChnSetPaused(0, !ndspChnIsPaused(0));
            }
        } else if (px >= NEXT_BTN_X && px <= NEXT_BTN_X + NEXT_BTN_W) {
            goToNextSong();
        }
    }
}

void handleSeekTouch(touchPosition touchPos,
                     bool newTouch,
                     bool screenTouched,
                     bool touchReleased,
                     InfoState &info,
                     float seekBarX,
                     float seekBarY,
                     float seekBarW,
                     float seekBarH) {
    if (!audioController.songReady) {
        return;
    }
    float px = (float) touchPos.px, py = (float) touchPos.py;
    bool inBar =
        px >= seekBarX && px <= seekBarX + seekBarW && py >= seekBarY && py <= seekBarY + seekBarH;
    if (newTouch && inBar) {
        audioController.seekRestorePaused = ndspChnIsPaused(0);
        info.seekDragging = true;
        ndspChnSetPaused(0, true);
    }
    if (info.seekDragging && screenTouched) {
        float prog = (px - seekBarX) / seekBarW;
        info.seekDragProgress = std::max(0.0f, std::min(1.0f, prog));
    }
    if (info.seekDragging && touchReleased) {
        double dur = audioController.songDurationSeconds;
        if (dur > 0) {
            audioController.seekTargetSeconds = (double) info.seekDragProgress * dur;
            audioController.seekPending = true;
            LightEvent_Signal(&audioController.fillBufferEvent);
        }
        info.seekDragging = false;
    }
}

bool handleContextMenu(u32 kDown, CtxMenu &s_ctx, CtxMenu &s_sub) {
    bool ctxHandled = s_ctx.active || s_sub.active;
    if (!ctxHandled) {
        return false;
    }

    CtxMenu &active = s_sub.active ? s_sub : s_ctx;
    const size_t n = active.labels.size();
    auto actions = active.actions;  // cache to avoid use after free
    if (kDown & KEY_A && n > 0) {
        actions[active.idx]();
    } else if (kDown & KEY_X && n >= 2) {
        actions[1]();
    } else if (kDown & KEY_Y) {
        if (n >= 3) {
            actions[2]();
        } else {
            // No third item: close menu and let the Y view-switch run below
            s_sub.active = false;
            s_ctx.close();
            ctxHandled = false;
        }
    } else if (kDown & KEY_B) {
        if (s_sub.active) {
            s_sub.active = false;
        } else {
            s_ctx.close();
        }
    }
    if (kDown & KEY_UP && active.idx > 0) {
        --active.idx;
        if (active.idx < active.scrollOffset) {
            active.scrollOffset = active.idx;
        }
    }
    if (kDown & KEY_DOWN && active.idx + 1 < n) {
        ++active.idx;
        if (active.idx >= active.scrollOffset + (size_t) MAX_CTX_VISIBLE) {
            ++active.scrollOffset;
        }
    }
    return ctxHandled;
}

void handleAButton(u32 &kDown,
                   TopScreenState &screenState,
                   FileBrowserState &fb,
                   PlaylistState &pl,
                   InfoState &info,
                   SettingsState &st,
                   CtxMenu &s_ctx,
                   CtxMenu &s_sub) {
    if (screenState == TopScreenState::SETTINGS) {
        if (st.sel == SettingsState::ROW_START_PATH) {
            s_ctx.close();
            s_ctx.add("Browse folders", [&screenState, &fb, &s_ctx]() {
                s_ctx.close();
                enterFolderPickerMode(screenState, fb);
            });
            s_ctx.add("Type path (keyboard)", [&s_ctx, &fb]() {
                SwkbdState swkbd;
                char buf[256] = {};
                strncpy(buf, g_settings.startPath.c_str(), sizeof(buf) - 1);
                swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, (int) (sizeof(buf) - 1));
                swkbdSetHintText(&swkbd, "Start path (e.g. sdmc:/Music/)");
                swkbdSetInitialText(&swkbd, buf);
                if (swkbdInputText(&swkbd, buf, sizeof(buf)) == SWKBD_BUTTON_CONFIRM && buf[0]) {
                    std::string p(buf);
                    if (p.back() != '/') {
                        p += '/';
                    }
                    g_settings.startPath = p;
                    saveSettings();
                    fb.reinitPending = true;
                    logToDebugScreen("Start path: " + p);
                }
                s_ctx.close();
            });
            s_ctx.open(60.0f, 60.0f);
        } else if (st.sel == SettingsState::ROW_VOLUME) {
            openVolumeKeyboard();
            saveSettings();
        } else if (st.sel == SettingsState::ROW_SEEK) {
            openSeekKeyboard();
            saveSettings();
        } else if (st.sel == SettingsState::ROW_ACCENT || st.sel == SettingsState::ROW_SECONDARY) {
            bool isAccent = (st.sel == SettingsState::ROW_ACCENT);
            u32 cur = isAccent ? g_accentColor : g_secondaryColor;
            char buf[8] = {};
            snprintf(buf,
                     sizeof(buf),
                     "%02X%02X%02X",
                     (unsigned) (cur & 0xFF),
                     (unsigned) ((cur >> 8) & 0xFF),
                     (unsigned) ((cur >> 16) & 0xFF));
            SwkbdState swkbd;
            swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 7);
            swkbdSetHintText(&swkbd, "Hex color RRGGBB");
            swkbdSetInitialText(&swkbd, buf);
            char result[8] = {};
            if (swkbdInputText(&swkbd, result, sizeof(result)) == SWKBD_BUTTON_CONFIRM &&
                result[0]) {
                const char *hex = (result[0] == '#') ? result + 1 : result;
                if (strlen(hex) == 6) {
                    try {
                        unsigned int rgb = (unsigned int) std::stoul(hex, nullptr, 16);
                        if (isAccent) {
                            g_settings.accentColor = "custom";
                            g_settings.accentColorHex = rgb;
                            applyAccentColor();
                        } else {
                            g_settings.accentColor2 = "custom";
                            g_settings.secondaryColorHex = rgb;
                            applySecondaryColor();
                        }
                        saveSettings();
                    } catch (...) {
                    }
                }
            }
        } else if (st.sel == SettingsState::ROW_RESET) {
            s_ctx.close();
            s_ctx.add("Cancel", [&s_ctx]() { s_ctx.close(); });
            s_ctx.add("Yes, reset", [&s_ctx, &st]() {
                g_settings = Settings{};
                applyVolume();
                applySpeedPitch();
                applyBrightness();
                applyAccentColor();
                applySecondaryColor();
                aptSetSleepAllowed(!g_settings.allowClosedLidPlayback);
                saveSettings();
                st.sel = 0;
                st.scrollOffset = 0;
                s_ctx.close();
            });
            s_ctx.open(60.0f, 60.0f);
        } else {
            kDown |= KEY_DRIGHT;
        }
    } else if (screenState == TopScreenState::FILEBROWSER) {
        if (fileController.files.empty()) {
            return;
        }
        auto ft = fileController.files[fileController.selectedFile].d_type;
        if (ft == DT_DIR) {
            fileController.cwd += fileController.files[fileController.selectedFile].d_name;
            fileController.cwd += '/';
            fileController.fileHistory.push_back({fileController.selectedFile, fb.scroll});
            fileController.selectedFile = 0;
            fb.scroll = 0;
            if (fileController.fileHistory.size() > (size_t) g_settings.maxDepth) {
                fileController.fileHistory.pop_front();
            }
            fileController.files = getFiles(fileController.cwd.c_str());
            fileController.filesShown = (fileController.files.size() > FILE_LAZY_THRESHOLD)
                                            ? std::min(fileController.files.size(), FILE_PAGE_SIZE)
                                            : fileController.files.size();
        } else if (ft == DT_REG) {
            if (fb.folderPickerMode) {
                return;
            }
            char *nm = fileController.files[fileController.selectedFile].d_name;
            const std::string path = fileController.cwd + nm;
            if (!isSupportedAudioFile(path)) {
                logToDebugScreen("Unsupported file: " + std::string(nm));
                return;
            }
            stopPlaybackIfPlaying();
            if (playSong(path)) {
                logToDebugScreen("Playing: " + (std::string) nm);
                if (g_settings.autoSwitchToPlayer) {
                    screenState = TopScreenState::INFO;
                }
                fileController.playingFile = fileController.selectedFile;
                fileController.playingCwd = fileController.cwd;
                fileController.playingFiles = fileController.files;
                if (fileController.shuffleEnabled) {
                    reshuffleAutoplay(false);
                }
            }
        }
    } else if (screenState == TopScreenState::PLAYLIST_BROWSER) {
        if (pl.sel == pl.playlists.size()) {
            SwkbdState swkbd;
            char nameBuf[64] = {0};
            swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 32);
            swkbdSetHintText(&swkbd, "Playlist name");
            SwkbdButton btn = swkbdInputText(&swkbd, nameBuf, sizeof(nameBuf));
            if (btn == SWKBD_BUTTON_CONFIRM && nameBuf[0]) {
                if (createPlaylist(nameBuf)) {
                    pl.dirty = true;
                    logToDebugScreen("Created: " + std::string(nameBuf));
                } else {
                    logToDebugScreen("Failed to create playlist");
                }
            }
        } else if (!pl.playlists.empty()) {
            pl.selSong = 0;
            pl.viewScroll = 0;
            pl.inHeader = false;
            pl.reorderMode = false;
            pl.reorderPicked = false;
            pl.reorderDirty = false;
            pl.coverLoadedFrom = "";
            screenState = TopScreenState::PLAYLIST_VIEW;
        }
    } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
        if (pl.reorderMode) {
            if (pl.inHeader) {
                if (!pl.playlists.empty() && pl.sel < pl.playlists.size()) {
                    if (writePlaylistSongOrder(pl.playlists[pl.sel].path,
                                               pl.playlists[pl.sel].songs)) {
                        pl.reorderDirty = false;
                        logToDebugScreen("Playlist order saved");
                    } else {
                        logToDebugScreen("Failed to save playlist order");
                    }
                }
            } else if (!pl.playlists.empty() && pl.sel < pl.playlists.size() &&
                       !pl.playlists[pl.sel].songs.empty()) {
                pl.reorderPicked = !pl.reorderPicked;
            }
        } else if (pl.inHeader) {
            triggerPlaylistPlay(pl, s_ctx, screenState, pl.headerBtnSel == 1);
        } else if (!pl.playlists.empty() && pl.sel < pl.playlists.size() &&
                   !pl.playlists[pl.sel].songs.empty()) {
            const Playlist &lst = pl.playlists[pl.sel];
            stopPlaybackIfPlaying();
            if (playSong(lst.songs[pl.selSong])) {
                for (size_t i = pl.selSong + 1; i < lst.songs.size(); ++i) {
                    enqueueSong(lst.songs[i]);
                }
                if (g_settings.autoSwitchToPlayer) {
                    screenState = TopScreenState::INFO;
                }
            }
        }
    } else if (screenState == TopScreenState::INFO) {
        if (info.reorderMode) {
            const int qSz = (int) fileController.playbackOrder.size();
            if (!info.reorderPicked) {
                if (qSz > 0) {
                    info.reorderPicked = true;
                    info.reorderFromIdx = fileController.selectedQueueItem - 1;
                }
            } else {
                int from = info.reorderFromIdx;
                int to = fileController.selectedQueueItem - 1;
                if (from >= 0 && from < qSz && to >= 0 && to < qSz && from != to) {
                    reorderQueueItem((size_t) from, (size_t) to);
                }
                info.reorderPicked = false;
                info.reorderFromIdx = -1;
            }
            return;
        }
        const int sel = fileController.selectedQueueItem;
        const int qSz = (int) fileController.playbackOrder.size();
        const int aSz = (int) info.autoplayItems.size();

        if (sel < 0) {
            int hi = -(sel + 1);
            if (hi < (int) fileController.playHistory.size()) {
                std::string path = fileController.playHistory[(size_t) hi];
                stopPlaybackIfPlaying();
                playSong(path);
            }
        } else if (sel >= 1 && sel <= qSz) {
            int qi = sel - 1;
            std::string path = fileController.playbackOrder[(size_t) qi];
            skipQueueItems((size_t) qi + 1);
            stopPlaybackIfPlaying();
            if (!playSong(path)) {
                advanceQueueOrAutoplay();
            }
        } else if (sel > qSz && sel - qSz - 1 < aSz) {
            int ai = sel - qSz - 1;
            const std::string path = info.autoplayItems[(size_t) ai];
            clearQueue();
            if (fileController.shuffleEnabled) {
                fileController.shuffledAutoplay.erase(fileController.shuffledAutoplay.begin(),
                                                      fileController.shuffledAutoplay.begin() + ai +
                                                          1);
            }
            for (size_t i = fileController.playingFile + 1; i < fileController.playingFiles.size();
                 ++i) {
                if (fileController.playingCwd + fileController.playingFiles[i].d_name == path) {
                    fileController.playingFile = i;
                    break;
                }
            }
            stopPlaybackIfPlaying();
            if (!playSong(path)) {
                advanceQueueOrAutoplay();
            }
        }
    }
}

void handleXButton(u32 kDown,
                   TopScreenState screenState,
                   FileBrowserState &fb,
                   PlaylistState &pl,
                   InfoState &info,
                   CtxMenu &s_ctx,
                   CtxMenu &s_sub) {
    if (screenState == TopScreenState::FILEBROWSER) {
        if (!fileController.files.empty()) {
            auto &f = fileController.files[fileController.selectedFile];
            std::string songPath = fileController.cwd + f.d_name;
            if (f.d_type == DT_REG && isSupportedAudioFile(songPath)) {
                float row = (float) (fileController.selectedFile - fb.scroll) + 1.0f;
                s_ctx.close();
                s_ctx.add("Play next", [&s_ctx, songPath]() {
                    queuePlayNext(songPath);
                    s_ctx.close();
                });
                s_ctx.add("Add to queue", [&s_ctx, songPath]() {
                    enqueueSong(songPath);
                    s_ctx.close();
                });
                s_ctx.add("Add to playlist >", [&pl, &s_ctx, &s_sub, songPath]() {
                    openAddToPlaylistSub(pl, s_ctx, s_sub, songPath);
                });
                s_ctx.open(50.0f, 16.0f * row);
            } else if (f.d_type == DT_DIR && strcmp(f.d_name, ".") != 0 &&
                       strcmp(f.d_name, "..") != 0) {
                std::string folderPath = fileController.cwd + f.d_name + "/";
                float row = (float) (fileController.selectedFile - fb.scroll) + 1.0f;
                s_ctx.close();
                s_ctx.add("Play folder next", [&s_ctx, folderPath]() {
                    auto songs = getFolderSongs(folderPath);
                    for (int i = (int) songs.size() - 1; i >= 0; --i) {
                        queuePlayNext(songs[i]);
                    }
                    s_ctx.close();
                });
                s_ctx.add("Add folder to queue", [&s_ctx, folderPath]() {
                    auto songs = getFolderSongs(folderPath);
                    for (const auto &s : songs) {
                        enqueueSong(s);
                    }
                    s_ctx.close();
                });
                s_ctx.add("Play shuffled next", [&s_ctx, folderPath]() {
                    auto songs = getFolderSongs(folderPath);
                    std::mt19937 g = makeShuffleRng();
                    std::shuffle(songs.begin(), songs.end(), g);
                    for (int i = (int) songs.size() - 1; i >= 0; --i) {
                        queuePlayNext(songs[i]);
                    }
                    s_ctx.close();
                });
                s_ctx.add("Add shuffled to queue", [&s_ctx, folderPath]() {
                    auto songs = getFolderSongs(folderPath);
                    std::mt19937 g = makeShuffleRng();
                    std::shuffle(songs.begin(), songs.end(), g);
                    for (const auto &s : songs) {
                        enqueueSong(s);
                    }
                    s_ctx.close();
                });
                s_ctx.add("Add folder to playlist >", [&pl, &s_ctx, &s_sub, folderPath]() {
                    auto songs = getFolderSongs(folderPath);
                    if (songs.empty()) {
                        logToDebugScreen("No supported audio files in folder");
                        s_ctx.close();
                        return;
                    }
                    pl.playlists = loadPlaylists();
                    if (pl.playlists.empty()) {
                        logToDebugScreen("No playlists. Create one first.");
                        s_ctx.close();
                        return;
                    }
                    s_sub.close();
                    for (size_t i = 0; i < pl.playlists.size(); ++i) {
                        s_sub.add(pl.playlists[i].name, [&pl, &s_sub, &s_ctx, songs, i]() {
                            int added = 0;
                            for (const auto &song : songs) {
                                if (addSongToPlaylist(pl.playlists[i].path, song)) {
                                    pl.playlists[i].songs.push_back(song);
                                    ++added;
                                }
                            }
                            logToDebugScreen("Added " + std::to_string(added) + " song(s) to \"" +
                                             pl.playlists[i].name + "\"");
                            s_sub.active = false;
                            s_ctx.close();
                        });
                    }
                    s_sub.open(s_ctx.x + 10.0f, s_ctx.y + 20.0f);
                });
                s_ctx.open(50.0f, 16.0f * row);
            }
        }

    } else if (screenState == TopScreenState::INFO) {
        const int sel = fileController.selectedQueueItem;
        const int qSz = (int) fileController.playbackOrder.size();
        const int hSz = (int) fileController.playHistory.size();
        const int aSz = (int) info.autoplayItems.size();

        std::string path;
        if (sel < 0) {
            int hi = -(sel + 1);
            if (hi < hSz) {
                path = fileController.playHistory[(size_t) hi];
            }
        } else if (sel == 0) {
            path = audioController.songPath;
        } else if (sel <= qSz) {
            path = fileController.playbackOrder[(size_t) (sel - 1)];
        } else {
            int ai = sel - qSz - 1;
            if (ai < aSz) {
                path = info.autoplayItems[(size_t) ai];
            }
        }

        if (!path.empty()) {
            float row = (float) (sel - info.scrollTop) + 1.0f;
            s_ctx.close();
            s_ctx.add("Play next", [&s_ctx, path]() {
                queuePlayNext(path);
                s_ctx.close();
            });
            s_ctx.add("Add to queue", [&s_ctx, path]() {
                enqueueSong(path);
                s_ctx.close();
            });
            if (sel < 0) {
                int hi = -(sel + 1);
                s_ctx.add("Remove from history", [&s_ctx, &info, hi]() {
                    if (hi < (int) fileController.playHistory.size()) {
                        fileController.playHistory.erase(fileController.playHistory.begin() + hi);
                        if (-(fileController.selectedQueueItem + 1) >=
                            (int) fileController.playHistory.size()) {
                            fileController.selectedQueueItem =
                                -((int) fileController.playHistory.size());
                        }
                        int minScroll = -((int) fileController.playHistory.size());
                        if (info.scrollTop < minScroll) {
                            info.scrollTop = minScroll;
                        }
                        if (info.scrollTop > fileController.selectedQueueItem) {
                            info.scrollTop = fileController.selectedQueueItem;
                        }
                    }
                    s_ctx.close();
                });
            } else if (sel >= 1 && sel <= qSz) {
                s_ctx.add("Remove from queue", [&s_ctx, &info, sel]() {
                    int qi = sel - 1;
                    if (qi < (int) fileController.playbackOrder.size()) {
                        removeQueueItem((size_t) qi);
                        int newMax = (int) fileController.playbackOrder.size() +
                                     (int) info.autoplayItems.size();
                        if (fileController.selectedQueueItem > newMax) {
                            fileController.selectedQueueItem = newMax > 0 ? newMax : 0;
                        }
                        if (info.scrollTop > fileController.selectedQueueItem) {
                            info.scrollTop = fileController.selectedQueueItem;
                        }
                    }
                    s_ctx.close();
                });
                if (info.reorderMode) {
                    s_ctx.add("Exit rearranging mode", [&s_ctx, &info]() {
                        info.reorderMode = false;
                        info.reorderPicked = false;
                        info.reorderFromIdx = -1;
                        s_ctx.close();
                    });
                } else {
                    s_ctx.add("Reorder songs", [&s_ctx, &info]() {
                        info.reorderMode = true;
                        info.reorderPicked = false;
                        info.reorderFromIdx = -1;
                        s_ctx.close();
                    });
                }
            }
            s_ctx.add("Add to playlist >", [&pl, &s_ctx, &s_sub, path]() {
                openAddToPlaylistSub(pl, s_ctx, s_sub, path);
            });

            if (sel >= 0 && !fileController.playbackOrder.empty()) {
                s_ctx.add("Add entire queue to playlist >", [&pl, &s_ctx, &s_sub]() {
                    std::vector<std::string> songs(fileController.playbackOrder.begin(),
                                                   fileController.playbackOrder.end());
                    openAddSongsToPlaylistSub(pl, s_ctx, s_sub, songs);
                });
            }

            if (sel < 0 && !fileController.playHistory.empty()) {
                s_ctx.add("Clear history", [&s_ctx, &s_sub, &info]() {
                    s_sub.close();
                    s_sub.add("No (cancel)", [&s_sub, &s_ctx]() {
                        s_sub.active = false;
                        s_ctx.close();
                    });
                    s_sub.add("Yes, clear history", [&s_sub, &s_ctx, &info]() {
                        fileController.playHistory.clear();
                        fileController.selectedQueueItem = 0;
                        info.scrollTop = 0;
                        logToDebugScreen("History cleared");
                        s_sub.active = false;
                        s_ctx.close();
                    });
                    s_sub.open(s_ctx.x + 10.0f, s_ctx.y + 20.0f);
                });
            } else if (sel >= 0 && !fileController.playbackOrder.empty()) {
                s_ctx.add("Clear queue", [&s_ctx, &s_sub]() {
                    s_sub.close();
                    s_sub.add("No (cancel)", [&s_sub, &s_ctx]() {
                        s_sub.active = false;
                        s_ctx.close();
                    });
                    s_sub.add("Yes, clear queue", [&s_sub, &s_ctx]() {
                        clearQueue();
                        logToDebugScreen("Queue cleared");
                        s_sub.active = false;
                        s_ctx.close();
                    });
                    s_sub.open(s_ctx.x + 10.0f, s_ctx.y + 20.0f);
                });
            }
            s_ctx.open(212.0f, 10.0f + 24.0f + 16.0f * row);
        }

    } else if (screenState == TopScreenState::PLAYLIST_BROWSER && !pl.playlists.empty() &&
               pl.sel < pl.playlists.size()) {
        size_t snapSel = pl.sel;
        float menuY = 16.0f * (float) (pl.sel - pl.browserScroll) + 16.0f;
        s_ctx.close();

        s_ctx.add("Rename", [&pl, &s_ctx, snapSel]() {
            if (snapSel >= pl.playlists.size()) {
                s_ctx.close();
                return;
            }
            SwkbdState swkbd;
            char buf[64] = {};
            strncpy(buf, pl.playlists[snapSel].name.c_str(), sizeof(buf) - 1);
            swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 32);
            swkbdSetHintText(&swkbd, "New playlist name");
            swkbdSetInitialText(&swkbd, buf);
            if (swkbdInputText(&swkbd, buf, sizeof(buf)) == SWKBD_BUTTON_CONFIRM && buf[0]) {
                if (renamePlaylist(pl.playlists[snapSel].path, buf)) {
                    logToDebugScreen("Renamed to: " + std::string(buf));
                    pl.dirty = true;
                } else {
                    logToDebugScreen("Rename failed");
                }
            }
            s_ctx.close();
        });

        s_ctx.add("Duplicate", [&pl, &s_ctx, snapSel]() {
            if (snapSel >= pl.playlists.size()) {
                s_ctx.close();
                return;
            }
            SwkbdState swkbd;
            char buf[64] = {};
            std::string suggested = pl.playlists[snapSel].name + " copy";
            strncpy(buf, suggested.c_str(), sizeof(buf) - 1);
            swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 32);
            swkbdSetHintText(&swkbd, "Duplicate playlist name");
            swkbdSetInitialText(&swkbd, buf);
            if (swkbdInputText(&swkbd, buf, sizeof(buf)) == SWKBD_BUTTON_CONFIRM && buf[0]) {
                if (duplicatePlaylist(pl.playlists[snapSel].path, buf)) {
                    logToDebugScreen("Duplicated as: " + std::string(buf));
                    pl.dirty = true;
                } else {
                    logToDebugScreen("Duplicate failed");
                }
            }
            s_ctx.close();
        });

        s_ctx.add("Merge with...", [&pl, &s_ctx, &s_sub, snapSel]() {
            if (snapSel >= pl.playlists.size()) {
                s_ctx.close();
                return;
            }
            s_sub.close();
            for (size_t i = 0; i < pl.playlists.size(); ++i) {
                if (i == snapSel) {
                    continue;
                }
                s_sub.add(pl.playlists[i].name, [&pl, &s_sub, &s_ctx, snapSel, i]() {
                    if (snapSel < pl.playlists.size() && i < pl.playlists.size()) {
                        std::string srcName = pl.playlists[snapSel].name;
                        std::string srcPath = pl.playlists[snapSel].path;
                        std::string dstName = pl.playlists[i].name;
                        if (mergePlaylist(pl.playlists[i].path, srcPath)) {
                            deletePlaylist(srcPath);
                            logToDebugScreen("Merged \"" + srcName + "\" into \"" + dstName +
                                             "\" and deleted \"" + srcName + "\"");
                            pl.dirty = true;
                        } else {
                            logToDebugScreen("Merge failed");
                        }
                    }
                    s_sub.active = false;
                    s_ctx.close();
                });
            }
            if (s_sub.labels.empty()) {
                logToDebugScreen("No other playlists to merge with");
            } else {
                s_sub.open(s_ctx.x + 10.0f, s_ctx.y + 20.0f);
            }
        });

        s_ctx.add("Remove duplicates", [&pl, &s_ctx, snapSel]() {
            if (snapSel < pl.playlists.size()) {
                size_t before = pl.playlists[snapSel].songs.size();
                if (removeDuplicateSongs(pl.playlists[snapSel].path)) {
                    pl.playlists[snapSel].songs = readPlaylistSongs(pl.playlists[snapSel].path);
                    size_t removed = before - pl.playlists[snapSel].songs.size();
                    logToDebugScreen("Removed " + std::to_string(removed) + " duplicate(s)");
                } else {
                    logToDebugScreen("Failed to remove duplicates");
                }
            }
            s_ctx.close();
        });

        s_ctx.add("Delete", [&pl, &s_ctx, &s_sub, snapSel]() {
            if (snapSel >= pl.playlists.size()) {
                s_ctx.close();
                return;
            }
            std::string name = pl.playlists[snapSel].name;
            s_sub.close();
            // "No" is first so the default (idx=0) is the safe choice
            s_sub.add("No (cancel)", [&s_sub, &s_ctx]() {
                s_sub.active = false;
                s_ctx.close();
            });
            s_sub.add("Yes, delete \"" + name + "\"", [&pl, &s_sub, &s_ctx, snapSel, name]() {
                if (snapSel < pl.playlists.size() && deletePlaylist(pl.playlists[snapSel].path)) {
                    logToDebugScreen("Deleted: " + name);
                    pl.dirty = true;
                } else {
                    logToDebugScreen("Failed to delete playlist");
                }
                s_sub.active = false;
                s_ctx.close();
            });
            s_sub.open(s_ctx.x + 10.0f, s_ctx.y + 64.0f);
        });

        s_ctx.open(50.0f, menuY);

    } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
        if (pl.reorderMode) {
            if (!pl.playlists.empty() && pl.sel < pl.playlists.size()) {
                if (writePlaylistSongOrder(pl.playlists[pl.sel].path, pl.playlists[pl.sel].songs)) {
                    pl.reorderDirty = false;
                    logToDebugScreen("Playlist order saved");
                } else {
                    logToDebugScreen("Failed to save playlist order");
                }
            }
        } else if (!pl.playlists.empty() && pl.sel < pl.playlists.size() &&
                   !pl.playlists[pl.sel].songs.empty() && !pl.inHeader) {
            std::string songPath = pl.playlists[pl.sel].songs[pl.selSong];
            size_t snapIdx = pl.selSong;
            const size_t pv_header_steps = std::min(pl.viewScroll, (size_t) 12);
            const size_t pv_song_offset = pl.viewScroll - pv_header_steps;
            float menu_y = 192.0f - (float) pv_header_steps * 16.0f +
                           16.0f * (float) (pl.selSong - pv_song_offset);
            s_ctx.close();
            s_ctx.add("Play next", [&s_ctx, songPath]() {
                queuePlayNext(songPath);
                s_ctx.close();
            });
            s_ctx.add("Add to queue", [&s_ctx, songPath]() {
                enqueueSong(songPath);
                s_ctx.close();
            });
            s_ctx.add("Remove from playlist", [&pl, &s_ctx, snapIdx]() {
                if (!pl.playlists.empty() && pl.sel < pl.playlists.size()) {
                    if (removeSongFromPlaylist(pl.playlists[pl.sel].path, snapIdx)) {
                        pl.playlists[pl.sel].songs.erase(pl.playlists[pl.sel].songs.begin() +
                                                         snapIdx);
                        if (pl.selSong > 0 && pl.selSong >= pl.playlists[pl.sel].songs.size()) {
                            --pl.selSong;
                        }
                        const auto &songs = pl.playlists[pl.sel].songs;
                        if (songs.empty() || pl.viewScroll + 2 >= songs.size()) {
                            pl.viewScroll = songs.size() > 2 ? songs.size() - 3 : 0;
                        }
                        logToDebugScreen("Removed song from playlist");
                    } else {
                        logToDebugScreen("Failed to remove song");
                    }
                }
                s_ctx.close();
            });
            s_ctx.add("Set as playlist cover", [&pl, &s_ctx, songPath]() {
                if (!pl.playlists.empty() && pl.sel < pl.playlists.size()) {
                    if (cachePlaylistCoverArt(pl.playlists[pl.sel].name, songPath)) {
                        pl.coverLoadedFrom = "";
                        logToDebugScreen("Cover art updated");
                    } else {
                        logToDebugScreen("Failed to set cover art");
                    }
                }
                s_ctx.close();
            });
            s_ctx.add("Reorder songs", [&pl, &s_ctx]() {
                pl.reorderMode = true;
                pl.reorderPicked = false;
                pl.reorderDirty = false;
                s_ctx.close();
            });
            s_ctx.open(50.0f, menu_y);
        }
    }
}

void handleBButton(u32 kDown,
                   TopScreenState &screenState,
                   FileBrowserState &fb,
                   PlaylistState &pl,
                   InfoState &info) {
    if (screenState == TopScreenState::SETTINGS) {
        screenState = TopScreenState::FILEBROWSER;
    } else if (screenState == TopScreenState::INFO) {
        if (info.reorderMode) {
            info.reorderMode = false;
            info.reorderPicked = false;
            info.reorderFromIdx = -1;
        } else {
            screenState = TopScreenState::FILEBROWSER;
            ndspChnSetPaused(0, true);
            logToDebugScreen("Pausing and going to filebrowser");
        }
    } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
        if (pl.reorderMode) {
            pl.reorderMode = false;
            pl.reorderPicked = false;
            pl.reorderDirty = false;
        } else {
            screenState = TopScreenState::PLAYLIST_BROWSER;
        }
    } else {  // FILEBROWSER
        // Folder-picker mode: B goes up or cancels
        if (fb.folderPickerMode) {
            size_t ls = fileController.cwd.rfind('/', fileController.cwd.size() - 2);
            if (ls == std::string::npos || fileController.cwd == "sdmc:/") {
                exitFolderPickerMode(screenState, fb, false);
            } else {
                fileController.cwd = fileController.cwd.substr(0, ls + 1);
                if (!fileController.fileHistory.empty()) {
                    auto [rf, rs] = fileController.fileHistory.back();
                    fileController.fileHistory.pop_back();
                    fileController.selectedFile = rf;
                    fb.scroll = rs;
                } else {
                    fileController.selectedFile = 0;
                    fb.scroll = 0;
                }
                fileController.files = getFiles(fileController.cwd.c_str());
                fileController.filesShown =
                    (fileController.files.size() > FILE_LAZY_THRESHOLD)
                        ? std::min(fileController.files.size(), FILE_PAGE_SIZE)
                        : fileController.files.size();
            }
            return;
        }

        // Normal mode: respect lockToStartPath
        if (g_settings.lockToStartPath && fileController.cwd == g_settings.startPath) {
            return;
        }

        size_t ls = fileController.cwd.rfind('/', fileController.cwd.size() - 2);
        if (ls != fileController.cwd.npos) {
            fileController.cwd = fileController.cwd.substr(0, ls + 1);
            if (!fileController.fileHistory.empty()) {
                auto [rf, rs] = fileController.fileHistory.back();
                fileController.fileHistory.pop_back();
                fileController.selectedFile = rf;
                fb.scroll = rs;
            } else {
                fileController.selectedFile = 0;
                fb.scroll = 0;
            }
            fileController.files = getFiles(fileController.cwd.c_str());
            fileController.filesShown = (fileController.files.size() > FILE_LAZY_THRESHOLD)
                                            ? std::min(fileController.files.size(), FILE_PAGE_SIZE)
                                            : fileController.files.size();
            // Ensure restored selectedFile index is within the shown range
            if (!fileController.files.empty() &&
                fileController.selectedFile >= fileController.filesShown) {
                fileController.filesShown =
                    std::min(fileController.files.size(), fileController.selectedFile + 1);
            }
        }
    }
}

void handleYButton(u32 kDown, TopScreenState &screenState, InfoState &info) {
    if (screenState == TopScreenState::FILEBROWSER) {
        screenState = TopScreenState::INFO;
        fileController.selectedQueueItem = 0;
        info.scrollTop = 0;
    } else if (screenState == TopScreenState::INFO) {
        info.reorderMode = false;
        info.reorderPicked = false;
        info.reorderFromIdx = -1;
        screenState = TopScreenState::FILEBROWSER;
    }
}

void handleSettingsInput(u32 kDown,
                         TopScreenState screenState,
                         SettingsState &st,
                         InfoState &info,
                         PlaylistState &pl,
                         bool seekLeftRepeat,
                         bool seekRightRepeat) {
    // Left/right to switch between Play/Shuffle when cursor is on header
    if (screenState == TopScreenState::PLAYLIST_VIEW && pl.inHeader) {
        if ((kDown & KEY_DLEFT) || (kDown & KEY_LEFT)) {
            pl.headerBtnSel = 0;
        }
        if ((kDown & KEY_DRIGHT) || (kDown & KEY_RIGHT)) {
            pl.headerBtnSel = 1;
        }
    }

    if (screenState != TopScreenState::SETTINGS && screenState != TopScreenState::PLAYLIST_VIEW &&
        screenState != TopScreenState::PLAYLIST_BROWSER && audioController.songReady) {
        double SEEK_SECONDS = (double) g_settings.seekSeconds;
        bool seekLeft = (kDown & KEY_DLEFT) || seekLeftRepeat;
        bool seekRight = (kDown & KEY_DRIGHT) || seekRightRepeat;
        if (seekLeft || seekRight) {
            double toSeek = seekRight ? SEEK_SECONDS : -SEEK_SECONDS;
            double target = audioController.songPositionSeconds + toSeek;
            double dur = audioController.songDurationSeconds;
            target = std::max(0.0, dur > 0 ? std::min(target, dur) : target);
            if (dur > 0) {
                info.seekDragProgress = (float) (target / dur);
            }
            audioController.seekRestorePaused = ndspChnIsPaused(0);
            audioController.seekTargetSeconds = target;
            audioController.seekPending = true;
            LightEvent_Signal(&audioController.fillBufferEvent);
        }
    }

    if (screenState != TopScreenState::SETTINGS) {
        return;
    }

    bool changed = false;
    bool right = (kDown & KEY_DRIGHT) || (kDown & KEY_RIGHT);
    bool left = (kDown & KEY_DLEFT) || (kDown & KEY_LEFT);

    if (right || left) {
        if (const ToggleSetting *tgl = findToggle(st.sel)) {
            g_settings.*(tgl->value) = !(g_settings.*(tgl->value));
            if (tgl->onToggle) {
                tgl->onToggle(info);
            }
            changed = true;
        } else {
            switch (st.sel) {
                case SettingsState::ROW_VOLUME:
                    if (right && g_settings.volumePercent < VOLUME_MAX_PERCENT) {
                        g_settings.volumePercent =
                            std::min(g_settings.volumePercent + VOLUME_STEP, VOLUME_MAX_PERCENT);
                        changed = true;
                    }
                    if (left && g_settings.volumePercent > 0) {
                        g_settings.volumePercent =
                            std::max(g_settings.volumePercent - VOLUME_STEP, 0);
                        changed = true;
                    }
                    if (changed) {
                        applyVolume();
                    }
                    break;

                case SettingsState::ROW_BRIGHTNESS:
                    if (right && g_settings.brightness < 5) {
                        ++g_settings.brightness;
                        changed = true;
                    }
                    if (left && g_settings.brightness > 1) {
                        --g_settings.brightness;
                        changed = true;
                    }
                    if (changed) {
                        applyBrightness();
                    }
                    break;

                case SettingsState::ROW_SEEK:
                    {
                        int n = 4;
                        int curIdx = -1;
                        for (int i = 0; i < n; i++) {
                            if (g_settings.seekSeconds == SEEK_PRESETS[i]) {
                                curIdx = i;
                                break;
                            }
                        }
                        int oldVal = g_settings.seekSeconds;
                        if (right) {
                            g_settings.seekSeconds =
                                SEEK_PRESETS[curIdx < 0 ? 0 : (curIdx + 1) % n];
                        }
                        if (left) {
                            g_settings.seekSeconds =
                                SEEK_PRESETS[curIdx < 0 ? n - 1 : (curIdx + n - 1) % n];
                        }
                        changed = (g_settings.seekSeconds != oldVal);
                        break;
                    }

                case SettingsState::ROW_SPEED:
                    if (right && g_settings.speedPercent < SPEED_MAX_PERCENT) {
                        g_settings.speedPercent =
                            std::min(g_settings.speedPercent + SPEED_STEP, SPEED_MAX_PERCENT);
                        changed = true;
                    }
                    if (left && g_settings.speedPercent > SPEED_MIN_PERCENT) {
                        g_settings.speedPercent =
                            std::max(g_settings.speedPercent - SPEED_STEP, SPEED_MIN_PERCENT);
                        changed = true;
                    }
                    if (changed) {
                        applySpeedPitch();
                    }
                    break;

                case SettingsState::ROW_PITCH:
                    if (right && g_settings.pitchSemitones < PITCH_MAX_SEMITONES) {
                        ++g_settings.pitchSemitones;
                        changed = true;
                    }
                    if (left && g_settings.pitchSemitones > PITCH_MIN_SEMITONES) {
                        --g_settings.pitchSemitones;
                        changed = true;
                    }
                    if (changed) {
                        applySpeedPitch();
                    }
                    break;

                case SettingsState::ROW_START_PATH:
                    break;

                case SettingsState::ROW_ACCENT:
                    {
                        int n = ACCENT_COLOR_COUNT;
                        int curIdx = -1;
                        for (int i = 0; i < n; i++) {
                            if (g_settings.accentColor == ACCENT_COLOR_NAMES[i]) {
                                curIdx = i;
                                break;
                            }
                        }
                        if (right) {
                            g_settings.accentColor =
                                ACCENT_COLOR_NAMES[curIdx < 0 ? 0 : (curIdx + 1) % n];
                        }
                        if (left) {
                            g_settings.accentColor =
                                ACCENT_COLOR_NAMES[curIdx < 0 ? n - 1 : (curIdx + n - 1) % n];
                        }
                        applyAccentColor();
                        changed = true;
                        break;
                    }

                case SettingsState::ROW_SECONDARY:
                    {
                        int n = SECONDARY_COLOR_COUNT;
                        int curIdx = -1;
                        for (int i = 0; i < n; i++) {
                            if (g_settings.accentColor2 == SECONDARY_COLOR_NAMES[i]) {
                                curIdx = i;
                                break;
                            }
                        }
                        if (right) {
                            g_settings.accentColor2 =
                                SECONDARY_COLOR_NAMES[curIdx < 0 ? 0 : (curIdx + 1) % n];
                        }
                        if (left) {
                            g_settings.accentColor2 =
                                SECONDARY_COLOR_NAMES[curIdx < 0 ? n - 1 : (curIdx + n - 1) % n];
                        }
                        applySecondaryColor();
                        changed = true;
                        break;
                    }

                case SettingsState::ROW_QUEUE_SIZE:
                    if (right) {
                        if (g_settings.queueSize < 20) {
                            g_settings.queueSize += 1;
                        } else if (g_settings.queueSize < 100) {
                            g_settings.queueSize += 10;
                        } else {
                            g_settings.queueSize += 50;
                        }
                        if (g_settings.queueSize > 9999) {
                            g_settings.queueSize = 9999;
                        }
                        changed = true;
                    }
                    if (left) {
                        if (g_settings.queueSize > 100) {
                            g_settings.queueSize -= 50;
                        } else if (g_settings.queueSize > 20) {
                            g_settings.queueSize -= 10;
                        } else if (g_settings.queueSize > 10) {
                            g_settings.queueSize -= 1;
                        }
                        changed = true;
                    }
                    break;

                case SettingsState::ROW_HISTORY_SIZE:
                    if (right && g_settings.historySize < 200) {
                        g_settings.historySize = std::min(200, g_settings.historySize + 5);
                        changed = true;
                    }
                    if (left && g_settings.historySize > 5) {
                        g_settings.historySize = std::max(5, g_settings.historySize - 5);
                        changed = true;
                    }
                    break;

                case SettingsState::ROW_MAX_DEPTH:
                    if (right && g_settings.maxDepth < 50) {
                        ++g_settings.maxDepth;
                        changed = true;
                    }
                    if (left && g_settings.maxDepth > 1) {
                        --g_settings.maxDepth;
                        changed = true;
                    }
                    break;

                default:
                    break;
            }
        }
        if (changed) {
            saveSettings();
        }
    }

    // UP / DOWN navigation with section-header skipping
    if (kDown & KEY_UP) {
        if (st.sel > 0) {
            size_t next = st.sel - 1;
            while (next > 0 && SettingsState::isHeaderRow(next)) {
                --next;
            }
            if (!SettingsState::isHeaderRow(next)) {
                st.sel = next;
            }
            if (st.sel < st.scrollOffset) {
                st.scrollOffset = st.sel;
            }
        }
    }
    if (kDown & KEY_DOWN) {
        if (st.sel + 1 < SettingsState::ROW_COUNT) {
            size_t next = st.sel + 1;
            while (next < SettingsState::ROW_COUNT - 1 && SettingsState::isHeaderRow(next)) {
                ++next;
            }
            if (!SettingsState::isHeaderRow(next)) {
                st.sel = next;
            }
            if (st.sel >= st.scrollOffset + SettingsState::VISIBLE_ROWS) {
                st.scrollOffset = st.sel - SettingsState::VISIBLE_ROWS + 1;
            }
        }
    }
}

void handleShoulderTaps(
    u32 kDown, u64 now, u64 &lTapTime, int &lTapCount, u64 &rTapTime, int &rTapCount) {
    if (g_settings.lockShoulderButtons) {
        return;
    }
    if (kDown & KEY_L) {
        lTapCount = (lTapCount > 0 && (now - lTapTime) <= MULTI_TAP_WINDOW_MS) ? lTapCount + 1 : 1;
        lTapTime = now;
    }
    if (kDown & KEY_R) {
        rTapCount = (rTapCount > 0 && (now - rTapTime) <= MULTI_TAP_WINDOW_MS) ? rTapCount + 1 : 1;
        rTapTime = now;
    }
    if (lTapCount > 0 && (now - lTapTime) > MULTI_TAP_WINDOW_MS) {
        if (lTapCount >= 3 && !fileController.playHistory.empty()) {
            std::string prevSong = fileController.playHistory.front();
            fileController.playHistory.pop_front();
            if (audioController.songReady) {
                fileController.playQueue.push_front(audioController.songPath);
                fileController.playbackOrder.insert(fileController.playbackOrder.begin(),
                                                    audioController.songPath);
                audioController.skipNextHistoryEntry = true;
                audioController.pendingStartPaused = ndspChnIsPaused(0);
                audioController.applyPendingStartPaused = true;
                stopPlaybackIfPlaying();
            }
            playSong(prevSong);
        }
        lTapCount = 0;
    }
    if (rTapCount > 0 && (now - rTapTime) > MULTI_TAP_WINDOW_MS) {
        if (rTapCount == 2 && audioController.songReady) {
            ndspChnSetPaused(0, !ndspChnIsPaused(0));
        } else if (rTapCount >= 3) {
            goToNextSong();
        }
        rTapCount = 0;
    }
}

void handleUpNav(u32 kDown,
                 bool upRepeat,
                 u64 &upRepeatMs,
                 TopScreenState screenState,
                 FileBrowserState &fb,
                 PlaylistState &pl,
                 InfoState &info,
                 int minInfoIdx,
                 int maxInfoIdx) {
    if (upRepeat) {
        upRepeatMs = osGetTime();
    }
    if (screenState == TopScreenState::FILEBROWSER) {
        if (fileController.selectedFile > 0) {
            --fileController.selectedFile;
            if (fileController.selectedFile < fb.scroll) {
                --fb.scroll;
            }
        } else {
            fileController.filesShown = fileController.files.size();
            fileController.selectedFile = fileController.filesShown - 1;
            fb.scroll = (fileController.filesShown > (size_t) MAX_FILES)
                            ? fileController.filesShown - MAX_FILES
                            : 0;
        }
    } else if (screenState == TopScreenState::INFO) {
        if (fileController.selectedQueueItem > minInfoIdx) {
            --fileController.selectedQueueItem;
            if (fileController.selectedQueueItem < info.scrollTop) {
                --info.scrollTop;
            }
        } else if (!upRepeat) {
            fileController.selectedQueueItem = maxInfoIdx;
            info.scrollTop = std::max(minInfoIdx, maxInfoIdx - INFO_MAX_VIS + 1);
        }
    } else if (screenState == TopScreenState::PLAYLIST_BROWSER) {
        if (pl.sel > 0) {
            --pl.sel;
            if (pl.sel < pl.browserScroll) {
                --pl.browserScroll;
            }
        } else if (!upRepeat) {
            // count make playlist entry
            pl.sel = pl.playlists.size();
            pl.browserScroll = (pl.playlists.size() + 1 > (size_t) MAX_FILES)
                                   ? pl.playlists.size() + 1 - MAX_FILES
                                   : 0;
        }
    } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
        if (pl.reorderMode) {
            if (!pl.inHeader) {
                if (pl.selSong > 0) {
                    if (pl.reorderPicked) {
                        std::swap(pl.playlists[pl.sel].songs[pl.selSong],
                                  pl.playlists[pl.sel].songs[pl.selSong - 1]);
                        pl.reorderDirty = true;
                    }
                    --pl.selSong;
                    const size_t pv_song_offset = (pl.viewScroll > 12) ? pl.viewScroll - 12 : 0;
                    if (pl.selSong < pv_song_offset) {
                        --pl.viewScroll;
                    }
                } else if (!upRepeat && !pl.reorderPicked) {
                    pl.inHeader = true;
                    pl.viewScroll = 0;
                }
            }
            // inHeader (Save button): nothing above it
        } else if (!pl.inHeader) {
            if (pl.selSong > 0) {
                --pl.selSong;
                const size_t pv_song_offset = (pl.viewScroll > 12) ? pl.viewScroll - 12 : 0;
                if (pl.selSong < pv_song_offset) {
                    --pl.viewScroll;
                }
            } else if (!upRepeat) {
                pl.inHeader = true;
                pl.viewScroll = 0;
            }
        } else if (!upRepeat && !pl.playlists.empty() && pl.sel < pl.playlists.size() &&
                   !pl.playlists[pl.sel].songs.empty()) {
            const auto &s = pl.playlists[pl.sel].songs;
            pl.inHeader = false;
            pl.selSong = s.size() - 1;
            pl.viewScroll = (s.size() >= 3) ? s.size() - 3 : 0;
        }
    }
}

void handleDownNav(u32 kDown,
                   bool downRepeat,
                   u64 &downRepeatMs,
                   TopScreenState screenState,
                   FileBrowserState &fb,
                   PlaylistState &pl,
                   InfoState &info,
                   int maxInfoIdx) {
    if (downRepeat) {
        downRepeatMs = osGetTime();
    }
    if (screenState == TopScreenState::FILEBROWSER) {
        // At the last shown entry: extend the page before advancing if more exist
        if (fileController.selectedFile == fileController.filesShown - 1 &&
            fileController.filesShown < fileController.files.size()) {
            fileController.filesShown =
                std::min(fileController.files.size(), fileController.filesShown + FILE_PAGE_SIZE);
        }
        if (fileController.selectedFile < fileController.filesShown - 1) {
            ++fileController.selectedFile;
            if (fileController.selectedFile >= fb.scroll + MAX_FILES) {
                ++fb.scroll;
            }
        } else {
            fileController.selectedFile = 0;
            fb.scroll = 0;
        }
    } else if (screenState == TopScreenState::INFO) {
        if (info.reorderMode) {
            const int qSz = (int) fileController.playQueue.size();
            if (fileController.selectedQueueItem < qSz) {
                ++fileController.selectedQueueItem;
                const int infoMaxVis = (info.scrollTop <= 0) ? INFO_MAX_VIS_CARD : INFO_MAX_VIS;
                if (fileController.selectedQueueItem >= info.scrollTop + infoMaxVis) {
                    ++info.scrollTop;
                }
            }
        } else if (fileController.selectedQueueItem < maxInfoIdx) {
            ++fileController.selectedQueueItem;
            const bool useLargeWindow =
                fileController.selectedQueueItem < -8 && info.scrollTop < -17;

            const int infoMaxVis =
                (info.scrollTop <= 0 && !useLargeWindow) ? INFO_MAX_VIS_CARD : INFO_MAX_VIS;
            if (fileController.selectedQueueItem >= info.scrollTop + infoMaxVis) {
                ++info.scrollTop;
            }
            // ensure that card fits on screen when scrolling down in history and get close
            if ((fileController.selectedQueueItem >= -2 && fileController.selectedQueueItem < 0) &&
                (info.scrollTop <= -12)) {
                ++info.scrollTop;
            }
        } else if (!downRepeat) {
            fileController.selectedQueueItem = 0;
            info.scrollTop = 0;
        }
    } else if (screenState == TopScreenState::PLAYLIST_BROWSER) {
        // count make playlist entry
        if (pl.sel < pl.playlists.size()) {
            ++pl.sel;
            if (pl.sel >= pl.browserScroll + MAX_FILES) {
                ++pl.browserScroll;
            }
        } else if (!downRepeat) {
            pl.sel = 0;
            pl.browserScroll = 0;
        }
    } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
        if (pl.reorderMode) {
            if (pl.inHeader) {
                if (!pl.playlists.empty() && pl.sel < pl.playlists.size() &&
                    !pl.playlists[pl.sel].songs.empty()) {
                    pl.inHeader = false;
                    pl.selSong = 0;
                    pl.viewScroll = 0;
                }
            } else if (!pl.playlists.empty() && pl.sel < pl.playlists.size() &&
                       pl.selSong < pl.playlists[pl.sel].songs.size() - 1) {
                if (pl.reorderPicked) {
                    std::swap(pl.playlists[pl.sel].songs[pl.selSong],
                              pl.playlists[pl.sel].songs[pl.selSong + 1]);
                    pl.reorderDirty = true;
                }
                ++pl.selSong;
                if (pl.selSong > pl.viewScroll + 2) {
                    ++pl.viewScroll;
                }
            }
            // last song: no wrap to header while reordering
        } else if (pl.inHeader) {
            // don't scroll in empty playlist
            if (!pl.playlists.empty() && pl.sel < pl.playlists.size() &&
                !pl.playlists[pl.sel].songs.empty()) {
                pl.inHeader = false;
                pl.selSong = 0;
                pl.viewScroll = 0;
            }
        } else if (!pl.playlists.empty() && !pl.playlists[pl.sel].songs.empty() &&
                   pl.selSong < pl.playlists[pl.sel].songs.size() - 1) {
            ++pl.selSong;
            if (pl.selSong > pl.viewScroll + 2) {
                ++pl.viewScroll;
            }
        } else if (!downRepeat && !pl.playlists.empty()) {
            // wrap to header
            pl.inHeader = true;
            pl.viewScroll = 0;
        }
    }
}

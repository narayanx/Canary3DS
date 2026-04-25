#include <3ds.h>
#include <citro2d.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <string>
#include <vector>

#include "audio_decoder.h"
#include "audio_engine.h"
#include "ctx_menu.h"
#include "filebrowser.h"
#include "gfx.h"
#include "image.h"
#include "playlist.h"

inline constexpr float SEEK_BAR_X = 8.0f;
inline constexpr float SEEK_BAR_Y = 220.0f;
inline constexpr float SEEK_BAR_W = 304.0f;
inline constexpr float SEEK_BAR_H = 10.0f;

inline constexpr int INFO_MAX_VIS = 9;
inline constexpr size_t AUTOPLAY_PEEK = 5;

// make sure to have trailing '/' character
inline constexpr std::string_view START_PATH = "sdmc:/Music/";
// how long to hold before auto-repeat begins (ms)
inline constexpr double REPEAT_INITIAL_DELAY_MS = 400.0;
// interval between each repeated scroll step once repeating (ms)
inline constexpr double REPEAT_INTERVAL_MS = 30.0;
// stop saving depth after this many directories (conserve memory, TODO allow changing in settings)
inline constexpr size_t MAX_DEPTH = 20;

enum class TopScreenState { FILEBROWSER, INFO, PLAYLIST_BROWSER, PLAYLIST_VIEW };

struct FileBrowserState {
    size_t scroll = 0;
};

struct PlaylistState {
    std::vector<Playlist> playlists;
    size_t sel = 0;      // selected playlist index
    size_t selSong = 0;  // selected song inside the viewed playlist
    size_t browserScroll = 0;
    size_t viewScroll = 0;
    bool dirty = true;
};

struct InfoState {
    int scrollTop = 0;
    std::vector<std::string> autoplayItems;
    // Cover art
    C2D_Image image{};
    C3D_Tex tex{};
    Tex3DS_SubTexture subtex{};
    bool hasCover = false;
    bool displayCover = true;
    // Seek-bar drag
    bool seekDragging = false;
    float seekDragProgress = 0.0f;
};

int main(int argc, char *argv[]) {
    romfsInit();
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    sceneInit();

    // lets us keep playing when 3ds is closed
    aptSetSleepAllowed(false);
    ndspInit();

    LightEvent_Init(&audioController.startEvent, RESET_ONESHOT);
    LightEvent_Init(&audioController.fillBufferEvent, RESET_ONESHOT);

    // TODO add a msg telling ppl how to dump with luma3ds (likely bc dspfirm isn't dumped)
    if (!audioInit()) {
        logToDebugScreen("Failed to init audio");
        waitForInput();
        gfxExit();
        ndspExit();
        romfsExit();
        return EXIT_FAILURE;
    }

    int32_t mainPrio = 0x30;
    svcGetThreadPriority(&mainPrio, CUR_THREAD_HANDLE);
    const Thread audioTid = threadCreate(
        audioThread, nullptr, AUDIO_THREAD_STACK_SZ, mainPrio - 1, AUDIO_THREAD_AFFINITY, false);

    ndspSetCallback(audioCallback, nullptr);

    fileController.cwd = START_PATH;
    DIR *tmp = opendir(fileController.cwd.c_str());
    if (!tmp) {
        fileController.cwd = "sdmc:/";
        fileController.fileHistory.clear();
    } else {
        closedir(tmp);
        tmp = opendir("sdmc:/");
        int idx = 0;
        while (tmp) {
            dirent *ent = readdir(tmp);
            if (!ent) {
                break;
            }
            if (ent->d_type == DT_DIR && strncmp(ent->d_name, "Music", 6) == 0) {
                fileController.fileHistory.push_back(
                    {(size_t) idx,
                     (size_t) idx >= (size_t) MAX_FILES ? (size_t) idx - MAX_FILES + 1 : 0});
                closedir(tmp);
                break;
            }
            ++idx;
        }
    }

    u64 upPressMs = 0, upRepeatMs = 0, downPressMs = 0, downRepeatMs = 0;

    static constexpr u64 MULTI_TAP_WINDOW_MS = 350;
    u64 lTapTime = 0, rTapTime = 0;
    int lTapCount = 0, rTapCount = 0;

    TopScreenState screenState = TopScreenState::FILEBROWSER;
    FileBrowserState fb;
    PlaylistState pl;
    InfoState info;
    CtxMenu s_ctx, s_sub;

    bool updateFiles = true, needsRender = true;
    bool wasTouched = false;
    bool showLog = false;
    bool prevHeadphonesConnected = true;

    // Open the playlist-picker submenu for song path.
    auto openAddToPlaylistSub = [&](const std::string &songPath) {
        pl.playlists = loadPlaylists();
        if (pl.playlists.empty()) {
            logToDebugScreen("No playlists. Create one first.");
            s_ctx.close();
            return;
        }
        s_sub.close();
        for (size_t i = 0; i < pl.playlists.size(); ++i) {
            s_sub.add(pl.playlists[i].name, [&, songPath, i]() {
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
    };

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        touchPosition touchPos;
        hidTouchRead(&touchPos);
        bool screenTouched = (touchPos.px != 0 || touchPos.py != 0);
        bool newTouch = screenTouched && !wasTouched;
        bool touchReleased = !screenTouched && wasTouched;
        wasTouched = screenTouched;

        if (kDown & KEY_START) {
            break;
        }

        bool hpNow = osIsHeadsetConnected();
        if (!hpNow && prevHeadphonesConnected && audioController.songReady) {
            ndspChnSetPaused(0, true);
            logToDebugScreen("Headphones disconnected, pausing");
        }
        prevHeadphonesConnected = hpNow;

        needsRender = kDown || kHeld || screenTouched || audioController.newSongStarted ||
                      audioController.songReady || needsRender;

        if (updateFiles) {
            fileController.files = getFiles(fileController.cwd.c_str());
            updateFiles = false;
        }

        // Autoplay preview (cheap to rebuild each frame)
        info.autoplayItems.clear();
        if (audioController.songReady) {
            size_t nf = fileController.playingFile + 1;
            while (nf < fileController.files.size() && info.autoplayItems.size() < AUTOPLAY_PEEK) {
                if (fileController.files[nf].d_type == DT_REG) {
                    std::string p = fileController.cwd + fileController.files[nf].d_name;
                    if (isSupportedAudioFile(p)) {
                        info.autoplayItems.push_back(p);
                    }
                }
                ++nf;
            }
        }

        // Nav button touch
        if (newTouch) {
            float px = (float) touchPos.px, py = (float) touchPos.py;
            if (py >= NAV_BTN_Y && py <= NAV_BTN_Y + NAV_BTN_H) {
                for (int i = 0; i < 3; ++i) {
                    if (px >= NAV_BTN_X[i] && px <= NAV_BTN_X[i] + NAV_BTN_W) {
                        if (i == 0) {
                            screenState = TopScreenState::FILEBROWSER;
                        } else if (i == 1) {
                            screenState = TopScreenState::INFO;
                            fileController.selectedQueueItem = 0;
                            info.scrollTop = 0;
                        } else if (i == 2) {
                            pl.dirty = true;
                            screenState = TopScreenState::PLAYLIST_BROWSER;
                        }
                        break;
                    }
                }
            }
        }

        // Touch-seek
        if (audioController.songReady) {
            float px = (float) touchPos.px, py = (float) touchPos.py;
            bool inBar = px >= SEEK_BAR_X && px <= SEEK_BAR_X + SEEK_BAR_W && py >= SEEK_BAR_Y &&
                         py <= SEEK_BAR_Y + SEEK_BAR_H;
            if (newTouch && inBar) {
                audioController.seekRestorePaused = ndspChnIsPaused(0);
                info.seekDragging = true;
                ndspChnSetPaused(0, true);
            }
            if (info.seekDragging && screenTouched) {
                float prog = (px - SEEK_BAR_X) / SEEK_BAR_W;
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

        // Context menu input
        bool ctxHandled = s_ctx.active || s_sub.active;
        if (ctxHandled) {
            CtxMenu &active = s_sub.active ? s_sub : s_ctx;
            const size_t n = active.labels.size();
            if (kDown & KEY_A && n > 0) {
                active.actions[active.idx]();
            } else if (kDown & KEY_X && n >= 2) {
                active.actions[1]();
            } else if (kDown & KEY_Y) {
                if (n >= 3) {
                    active.actions[2]();
                } else {
                    // No third item: close menu and let the Y view-switch run below.
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
        }

        // A button
        if (!ctxHandled && (kDown & KEY_A)) {
            if (screenState == TopScreenState::FILEBROWSER) {
                auto ft = fileController.files[fileController.selectedFile].d_type;
                if (ft == DT_DIR) {
                    fileController.cwd += fileController.files[fileController.selectedFile].d_name;
                    fileController.cwd += '/';
                    fileController.fileHistory.push_back({fileController.selectedFile, fb.scroll});
                    fileController.selectedFile = 0;
                    fb.scroll = 0;
                    if (fileController.fileHistory.size() > MAX_DEPTH) {
                        fileController.fileHistory.pop_front();
                    }
                    fileController.files = getFiles(fileController.cwd.c_str());
                } else if (ft == DT_REG) {
                    stopPlaybackIfPlaying();
                    char *nm = fileController.files[fileController.selectedFile].d_name;
                    if (playSong(fileController.cwd + nm)) {
                        logToDebugScreen("Playing: " + (std::string) nm);
                        screenState = TopScreenState::INFO;
                    }
                    fileController.playingFile = fileController.selectedFile;
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
                    screenState = TopScreenState::PLAYLIST_VIEW;
                }
            } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
                if (!pl.playlists.empty() && pl.sel < pl.playlists.size() &&
                    !pl.playlists[pl.sel].songs.empty()) {
                    const Playlist &lst = pl.playlists[pl.sel];
                    stopPlaybackIfPlaying();
                    if (playSong(lst.songs[pl.selSong])) {
                        for (size_t i = pl.selSong + 1; i < lst.songs.size(); ++i) {
                            enqueueSong(lst.songs[i]);
                        }
                        screenState = TopScreenState::INFO;
                    }
                }
            }
        }

        // X button
        if (!ctxHandled && (kDown & KEY_X)) {
            if (screenState == TopScreenState::FILEBROWSER) {
                if (!fileController.files.empty()) {
                    auto &f = fileController.files[fileController.selectedFile];
                    std::string songPath = fileController.cwd + f.d_name;
                    if (f.d_type == DT_REG && isSupportedAudioFile(songPath)) {
                        std::string songPath = fileController.cwd + f.d_name;
                        float row = (float) (fileController.selectedFile - fb.scroll) + 1.0f;
                        s_ctx.close();
                        s_ctx.add("Play next", [&, songPath]() {
                            fileController.playQueue.push_front(songPath);
                            logToDebugScreen("Play next: " + songPath);
                            s_ctx.close();
                        });
                        s_ctx.add("Add to queue", [&, songPath]() {
                            enqueueSong(songPath);
                            s_ctx.close();
                        });
                        s_ctx.add("Add to playlist >",
                                  [&, songPath]() { openAddToPlaylistSub(songPath); });
                        s_ctx.open(50.0f, 16.0f * row);
                    }
                }

            } else if (screenState == TopScreenState::INFO) {
                const int sel = fileController.selectedQueueItem;
                const int qSz = (int) fileController.playQueue.size();
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
                    path = fileController.playQueue[(size_t) (sel - 1)];
                } else {
                    int ai = sel - qSz - 1;
                    if (ai < aSz) {
                        path = info.autoplayItems[(size_t) ai];
                    }
                }

                if (!path.empty()) {
                    float row = (float) (sel - info.scrollTop) + 1.0f;
                    s_ctx.close();
                    s_ctx.add("Play next", [&, path]() {
                        fileController.playQueue.push_front(path);
                        logToDebugScreen("Play next: " + path);
                        s_ctx.close();
                    });
                    s_ctx.add("Add to queue", [&, path]() {
                        enqueueSong(path);
                        s_ctx.close();
                    });
                    if (sel < 0) {
                        int hi = -(sel + 1);
                        s_ctx.add("Remove from history", [&, hi]() {
                            if (hi < (int) fileController.playHistory.size()) {
                                fileController.playHistory.erase(
                                    fileController.playHistory.begin() + hi);
                                if (-(fileController.selectedQueueItem + 1) >=
                                    (int) fileController.playHistory.size()) {
                                    fileController.selectedQueueItem =
                                        -((int) fileController.playHistory.size());
                                }
                                if (info.scrollTop > fileController.selectedQueueItem) {
                                    info.scrollTop = fileController.selectedQueueItem;
                                }
                            }
                            s_ctx.close();
                        });
                    } else if (sel >= 1 && sel <= qSz) {
                        s_ctx.add("Remove from queue", [&, sel]() {
                            int qi = sel - 1;
                            if (qi < (int) fileController.playQueue.size()) {
                                fileController.playQueue.erase(fileController.playQueue.begin() +
                                                               qi);
                                int newMax = (int) fileController.playQueue.size() +
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
                    }
                    s_ctx.add("Add to playlist >", [&, path]() { openAddToPlaylistSub(path); });
                    s_ctx.open(212.0f, 10.0f + 24.0f + 16.0f * row);
                }

            } else if (screenState == TopScreenState::PLAYLIST_BROWSER && !pl.playlists.empty() &&
                       pl.sel < pl.playlists.size()) {
                if (deletePlaylist(pl.playlists[pl.sel].path)) {
                    logToDebugScreen("Deleted: " + pl.playlists[pl.sel].name);
                    pl.dirty = true;
                } else {
                    logToDebugScreen("Failed to delete playlist");
                }

            } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
                if (!pl.playlists.empty() && pl.sel < pl.playlists.size() &&
                    !pl.playlists[pl.sel].songs.empty()) {
                    std::string songPath = pl.playlists[pl.sel].songs[pl.selSong];
                    size_t snapIdx = pl.selSong;
                    float row = (float) (pl.selSong - pl.viewScroll) + 1.0f;
                    s_ctx.close();
                    s_ctx.add("Play next", [&, songPath]() {
                        fileController.playQueue.push_front(songPath);
                        logToDebugScreen("Play next: " + songPath);
                        s_ctx.close();
                    });
                    s_ctx.add("Add to queue", [&, songPath]() {
                        enqueueSong(songPath);
                        s_ctx.close();
                    });
                    s_ctx.add("Remove from playlist", [&, snapIdx]() {
                        if (!pl.playlists.empty() && pl.sel < pl.playlists.size()) {
                            if (removeSongFromPlaylist(pl.playlists[pl.sel].path, snapIdx)) {
                                pl.playlists[pl.sel].songs.erase(
                                    pl.playlists[pl.sel].songs.begin() + snapIdx);
                                if (pl.selSong > 0 &&
                                    pl.selSong >= pl.playlists[pl.sel].songs.size()) {
                                    --pl.selSong;
                                }
                                const auto &songs = pl.playlists[pl.sel].songs;
                                if (pl.viewScroll > 0 && pl.viewScroll + MAX_FILES > songs.size()) {
                                    pl.viewScroll = songs.size() > (size_t) MAX_FILES
                                                        ? songs.size() - MAX_FILES
                                                        : 0;
                                }
                                logToDebugScreen("Removed song from playlist");
                            } else {
                                logToDebugScreen("Failed to remove song");
                            }
                        }
                        s_ctx.close();
                    });
                    s_ctx.open(50.0f, 16.0f * row);
                }
            }
        }

        // B button
        if (!ctxHandled && (kDown & KEY_B)) {
            if (screenState == TopScreenState::INFO) {
                screenState = TopScreenState::FILEBROWSER;
                ndspChnSetPaused(0, true);
                logToDebugScreen("Pausing and going to filebrowser");
            } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
                screenState = TopScreenState::PLAYLIST_BROWSER;
            } else {
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
                }
            }
        }

        // Y button
        if (!ctxHandled && (kDown & KEY_Y)) {
            if (screenState == TopScreenState::FILEBROWSER) {
                screenState = TopScreenState::INFO;
                fileController.selectedQueueItem = 0;
                info.scrollTop = 0;
            } else if (screenState == TopScreenState::INFO) {
                pl.dirty = true;
                screenState = TopScreenState::PLAYLIST_BROWSER;
            } else {
                screenState = TopScreenState::FILEBROWSER;
            }
        }

        // SELECT: toggle log overlay;
        if (!ctxHandled && (kDown & KEY_SELECT)) {
            showLog = !showLog;
        }

        // Shoulder multi-tap
        u64 now = osGetTime();
        if (kDown & KEY_L) {
            lTapCount =
                (lTapCount > 0 && (now - lTapTime) <= MULTI_TAP_WINDOW_MS) ? lTapCount + 1 : 1;
            lTapTime = now;
        }
        if (kDown & KEY_R) {
            rTapCount =
                (rTapCount > 0 && (now - rTapTime) <= MULTI_TAP_WINDOW_MS) ? rTapCount + 1 : 1;
            rTapTime = now;
        }
        if (lTapCount > 0 && (now - lTapTime) > MULTI_TAP_WINDOW_MS) {
            if (lTapCount >= 3 && !fileController.playHistory.empty()) {
                std::string prevSong = fileController.playHistory.front();
                fileController.playHistory.pop_front();
                if (audioController.songReady) {
                    fileController.playQueue.push_front(audioController.songPath);
                    audioController.skipNextHistoryEntry = true;
                    stopPlaybackIfPlaying();
                }
                playSong(prevSong);
            }
            lTapCount = 0;
        }
        if (rTapCount > 0 && (now - rTapTime) > MULTI_TAP_WINDOW_MS) {
            if (rTapCount == 2 && audioController.songReady) {
                ndspChnSetPaused(0, !ndspChnIsPaused(0));
            } else if (rTapCount >= 3 && audioController.songReady &&
                       fileController.playingFile < fileController.files.size() - 1) {
                goToNextSong();
            }
            rTapCount = 0;
        }

        // D-pad auto-repeat timing
        if (kDown & KEY_UP) {
            upPressMs = now;
            upRepeatMs = now;
        }
        if (kDown & KEY_DOWN) {
            downPressMs = now;
            downRepeatMs = now;
        }

        // INFO virtual list bounds
        const int histSzInfo = (int) fileController.playHistory.size();
        const int qSzInfo = (int) fileController.playQueue.size();
        const int aSzInfo = (int) info.autoplayItems.size();
        const int minInfoIdx = -histSzInfo;
        const int maxInfoIdx = qSzInfo + aSzInfo;

        bool firstItem =
            (screenState == TopScreenState::FILEBROWSER && fileController.selectedFile == 0) ||
            (screenState == TopScreenState::INFO &&
             fileController.selectedQueueItem <= minInfoIdx) ||
            (screenState == TopScreenState::PLAYLIST_BROWSER && pl.sel == 0) ||
            (screenState == TopScreenState::PLAYLIST_VIEW && pl.selSong == 0);
        bool upRepeat = (kHeld & KEY_UP) && !firstItem &&
                        (double) (now - upPressMs) > REPEAT_INITIAL_DELAY_MS &&
                        (double) (now - upRepeatMs) > REPEAT_INTERVAL_MS;

        // UP
        if (!ctxHandled && ((kDown & KEY_UP) || upRepeat)) {
            if (upRepeat) {
                upRepeatMs = now;
            }
            if (screenState == TopScreenState::FILEBROWSER) {
                if (fileController.selectedFile > 0) {
                    --fileController.selectedFile;
                    if (fileController.selectedFile < fb.scroll) {
                        --fb.scroll;
                    }
                } else {
                    fileController.selectedFile = fileController.files.size() - 1;
                    fb.scroll = (fileController.files.size() > (size_t) MAX_FILES)
                                    ? fileController.files.size() - MAX_FILES
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
                if (pl.selSong > 0) {
                    --pl.selSong;
                    if (pl.selSong < pl.viewScroll) {
                        --pl.viewScroll;
                    }
                } else if (!upRepeat && !pl.playlists.empty()) {
                    const auto &s = pl.playlists[pl.sel].songs;
                    pl.selSong = s.size() - 1;
                    pl.viewScroll = (s.size() > (size_t) MAX_FILES) ? s.size() - MAX_FILES : 0;
                }
            }
        }

        bool lastFile = screenState == TopScreenState::FILEBROWSER &&
                        fileController.selectedFile == fileController.files.size() - 1;
        bool lastInfo =
            screenState == TopScreenState::INFO && fileController.selectedQueueItem >= maxInfoIdx;
        bool lastPlaylist =
            screenState == TopScreenState::PLAYLIST_BROWSER &&
            pl.sel == pl.playlists.size();  // consider "make playlist" entry at bottom
        bool lastPlaylistSong = screenState == TopScreenState::PLAYLIST_VIEW &&
                                !pl.playlists.empty() &&
                                pl.selSong == pl.playlists[pl.sel].songs.size() - 1;
        bool lastItem = (lastFile) || (lastInfo) || (lastPlaylist) || (lastPlaylistSong);
        bool downRepeat = (kHeld & KEY_DOWN) && !lastItem &&
                          (double) (now - downPressMs) > REPEAT_INITIAL_DELAY_MS &&
                          (double) (now - downRepeatMs) > REPEAT_INTERVAL_MS;

        // DOWN
        if (!ctxHandled && ((kDown & KEY_DOWN) || downRepeat)) {
            if (downRepeat) {
                downRepeatMs = now;
            }
            if (screenState == TopScreenState::FILEBROWSER) {
                if (fileController.selectedFile < fileController.files.size() - 1) {
                    ++fileController.selectedFile;
                    if (fileController.selectedFile >= fb.scroll + MAX_FILES) {
                        ++fb.scroll;
                    }
                } else {
                    fileController.selectedFile = 0;
                    fb.scroll = 0;
                }
            } else if (screenState == TopScreenState::INFO) {
                if (fileController.selectedQueueItem < maxInfoIdx) {
                    ++fileController.selectedQueueItem;
                    if (fileController.selectedQueueItem >= info.scrollTop + INFO_MAX_VIS) {
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
                if (!pl.playlists.empty() && pl.selSong < pl.playlists[pl.sel].songs.size() - 1) {
                    ++pl.selSong;
                    if (pl.selSong >= pl.viewScroll + MAX_FILES) {
                        ++pl.viewScroll;
                    }
                } else if (!downRepeat && !pl.playlists.empty()) {
                    pl.selSong = 0;
                    pl.viewScroll = 0;
                }
            }
        }

        // New song started
        if (audioController.newSongStarted) {
            audioController.newSongStarted = false;
            updateFiles = true;
            // hasCover is passed as freeExisting to release the previous texture
            info.hasCover =
                loadCoverArtForCurrentSong(info.image, info.tex, info.subtex, info.hasCover);
            fileController.selectedQueueItem = 0;
            info.scrollTop = 0;
        }

        // Reload playlists when dirty
        if (pl.dirty && (screenState == TopScreenState::PLAYLIST_BROWSER ||
                         screenState == TopScreenState::PLAYLIST_VIEW)) {
            pl.playlists = loadPlaylists();
            pl.dirty = false;
            if (!pl.playlists.empty() && pl.sel > pl.playlists.size()) {
                pl.sel = pl.playlists.size();
                pl.browserScroll = (pl.sel + 1 > (size_t) MAX_FILES) ? pl.sel + 1 - MAX_FILES : 0;
            }
        }

        // Render
        if (needsRender) {
            consoleClear();
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

            C2D_TargetClear(top, CLEAR_COLOR);
            C2D_SceneBegin(top);

            if (screenState == TopScreenState::FILEBROWSER) {
                printC2DText(fileController.cwd, 0);
                printFiles(
                    fileController.files, fileController.selectedFile, fb.scroll, MAX_FILES, 1);

            } else if (screenState == TopScreenState::INFO) {
                if (info.displayCover && info.hasCover) {
                    drawCoverScaled(info.image, info.subtex, 10.0f, 10.0f);
                }

                printNowPlayingList(fileController.playHistory,
                                    fileController.playQueue,
                                    info.autoplayItems,
                                    fileController.selectedQueueItem,
                                    info.scrollTop,
                                    audioController.songPath,
                                    audioController.songArtist,
                                    audioController.songTrackNumber);

                {
                    double dur = audioController.songDurationSeconds;
                    bool showDrag = info.seekDragging || audioController.seekPending;
                    double pos = (showDrag && dur > 0) ? (double) info.seekDragProgress * dur
                                                       : audioController.songPositionSeconds;
                    drawProgressBar(
                        10.0f, 206.0f, 190.0f, 7.0f, (dur > 0) ? (float) (pos / dur) : 0.0f);
                    drawTimeText(pos, dur, 10.0f, 217.0f, 0.44f, 0.44f);
                }

            } else if (screenState == TopScreenState::PLAYLIST_BROWSER) {
                printC2DText("Playlists  A=Open  X=Delete", 0);
                std::vector<std::string> names;
                for (const auto &p : pl.playlists) {
                    names.push_back(p.name);
                }
                names.push_back("(+ Create playlist)");
                printStringList(names, pl.sel, pl.browserScroll, 1);

            } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
                if (!pl.playlists.empty() && pl.sel < pl.playlists.size()) {
                    const Playlist &lst = pl.playlists[pl.sel];
                    printC2DText("< " + lst.name + "  A=Play  X=Menu", 0);
                    std::vector<std::string> songNames;
                    for (const auto &s : lst.songs) {
                        size_t sl = s.find_last_of('/');
                        songNames.push_back(sl != std::string::npos ? s.substr(sl + 1) : s);
                    }
                    printStringList(songNames, pl.selSong, pl.viewScroll, 1);
                }
            }

            // Context menu overlay
            if (s_sub.active) {
                printContextMenu(s_sub.labels, s_sub.idx, s_sub.scrollOffset, s_sub.x, s_sub.y);
            } else if (s_ctx.active) {
                printContextMenu(s_ctx.labels, s_ctx.idx, s_ctx.scrollOffset, s_ctx.x, s_ctx.y);
            }

            // Log overlay (drawn last so it sits on top of everything)
            if (showLog) {
                renderLogOverlay();
            }

            // Bottom screen
            C2D_TargetClear(bottom, BOTTOM_CLEAR_COLOR);
            C2D_SceneBegin(bottom);
            int activeTab = 0;
            if (screenState == TopScreenState::INFO) {
                activeTab = 1;
            } else if (screenState == TopScreenState::PLAYLIST_BROWSER ||
                       screenState == TopScreenState::PLAYLIST_VIEW) {
                activeTab = 2;
            }
            renderBottomScreen(
                audioController.songReady,
                audioController.songPositionSeconds,
                audioController.songDurationSeconds,
                audioController.songPath,
                audioController.songArtist,
                SEEK_BAR_X,
                SEEK_BAR_Y,
                SEEK_BAR_W,
                SEEK_BAR_H,
                (info.seekDragging || audioController.seekPending) ? info.seekDragProgress : -1.0f,
                activeTab);
            C3D_FrameEnd(0);
        }
        needsRender = false;
    }

    // Cleanup
    if (audioController.songReady) {
        audioController.stopPlayback = true;
    }
    audioController.interrupted = true;
    runThreads = false;
    LightEvent_Signal(&audioController.startEvent);
    LightEvent_Signal(&audioController.fillBufferEvent);

    threadJoin(audioTid, UINT64_MAX);
    threadFree(audioTid);

    audioExit();
    ndspExit();
    sceneExit();
    C2D_Fini();
    C3D_Fini();
    romfsExit();
    gfxExit();
    return 0;
}

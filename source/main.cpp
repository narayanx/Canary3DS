#include <3ds.h>
#include <citro2d.h>

#include <algorithm>
#include <cstdlib>
#include <dirent.h>
#include <string>

#include "app_lifecycle.h"
#include "app_state.h"
#include "audio_decoder.h"
#include "audio_engine.h"
#include "ctx_menu.h"
#include "filebrowser.h"
#include "gfx.h"
#include "image.h"
#include "input_handlers.h"
#include "playlist.h"
#include "render_frame.h"
#include "settings.h"

inline constexpr float SEEK_BAR_X = 8.0f;
inline constexpr float SEEK_BAR_Y = 220.0f;
inline constexpr float SEEK_BAR_W = 304.0f;
inline constexpr float SEEK_BAR_H = 10.0f;

inline constexpr size_t AUTOPLAY_PEEK = 20;

int main(int argc, char *argv[]) {
    if (!appInit()) {
        return EXIT_FAILURE;
    }

    TopScreenState screenState = TopScreenState::FILEBROWSER;
    FileBrowserState fb;
    PlaylistState pl;
    InfoState info;
    SettingsState st;
    CtxMenu s_ctx, s_sub;

    initFileHistory(g_settings.startPath);

    u64 upPressMs = 0, upRepeatMs = 0, downPressMs = 0, downRepeatMs = 0;
    u64 leftPressMs = 0, leftRepeatMs = 0, rightPressMs = 0, rightRepeatMs = 0;
    u64 lTapTime = 0, rTapTime = 0;
    int lTapCount = 0, rTapCount = 0;

    bool updateFiles = true, needsRender = true;
    bool wasTouched = false;
    bool showLog = false;
    bool prevHeadphonesConnected = true;

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
            fileController.filesShown = (fileController.files.size() > FILE_LAZY_THRESHOLD)
                                            ? std::min(fileController.files.size(), FILE_PAGE_SIZE)
                                            : fileController.files.size();
            // selectedFile may be beyond the initial page (e.g. after pagination then song start)
            if (!fileController.files.empty() &&
                fileController.selectedFile >= fileController.filesShown) {
                fileController.filesShown =
                    std::min(fileController.files.size(), fileController.selectedFile + 1);
            }
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

        handleNavTouch(touchPos, newTouch, screenState, pl, info, st, s_ctx, s_sub);
        handleSeekTouch(touchPos,
                        newTouch,
                        screenTouched,
                        touchReleased,
                        info,
                        SEEK_BAR_X,
                        SEEK_BAR_Y,
                        SEEK_BAR_W,
                        SEEK_BAR_H);

        u64 now = osGetTime();
        if (kDown & KEY_DLEFT) {
            leftPressMs = now;
            leftRepeatMs = now;
        }
        if (kDown & KEY_DRIGHT) {
            rightPressMs = now;
            rightRepeatMs = now;
        }

        bool ctxHandled = handleContextMenu(kDown, s_ctx, s_sub);
        if (!ctxHandled) {
            if (kDown & KEY_A) {
                handleAButton(kDown, screenState, fb, pl, info, st, s_ctx, s_sub);
            }
            if (kDown & KEY_X) {
                handleXButton(kDown, screenState, fb, pl, info, s_ctx, s_sub);
            }
            if (kDown & KEY_B) {
                handleBButton(kDown, screenState, fb, pl);
            }
            if (kDown & KEY_Y) {
                handleYButton(kDown, screenState, info);
            }
            if (kDown & KEY_SELECT) {
                showLog = !showLog;
            }
            bool seekLeftRepeat = (kHeld & KEY_DLEFT) && leftPressMs != 0 &&
                                  (double) (now - leftPressMs) > REPEAT_INITIAL_DELAY_MS &&
                                  (double) (now - leftRepeatMs) > SEEK_REPEAT_INTERVAL_MS;
            bool seekRightRepeat = (kHeld & KEY_DRIGHT) && rightPressMs != 0 &&
                                   (double) (now - rightPressMs) > REPEAT_INITIAL_DELAY_MS &&
                                   (double) (now - rightRepeatMs) > SEEK_REPEAT_INTERVAL_MS;
            handleSettingsInput(kDown, screenState, st, info, pl, seekLeftRepeat, seekRightRepeat);
            if (seekLeftRepeat) {
                leftRepeatMs = now;
            }
            if (seekRightRepeat) {
                rightRepeatMs = now;
            }
        }

        handleShoulderTaps(kDown, now, lTapTime, lTapCount, rTapTime, rTapCount);

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
            (screenState == TopScreenState::PLAYLIST_VIEW && pl.inHeader);
        bool upRepeat = (kHeld & KEY_UP) && !firstItem &&
                        (double) (now - upPressMs) > REPEAT_INITIAL_DELAY_MS &&
                        (double) (now - upRepeatMs) > REPEAT_INTERVAL_MS;

        if (!ctxHandled && screenState != TopScreenState::SETTINGS &&
            ((kDown & KEY_UP) || upRepeat)) {
            handleUpNav(
                kDown, upRepeat, upRepeatMs, screenState, fb, pl, info, minInfoIdx, maxInfoIdx);
        }

        bool lastFile = screenState == TopScreenState::FILEBROWSER &&
                        fileController.selectedFile == fileController.filesShown - 1 &&
                        fileController.filesShown >= fileController.files.size();
        bool lastInfo =
            screenState == TopScreenState::INFO && fileController.selectedQueueItem >= maxInfoIdx;
        bool lastPlaylist =
            screenState == TopScreenState::PLAYLIST_BROWSER &&
            pl.sel == pl.playlists.size();  // consider "make playlist" entry at bottom
        bool lastPlaylistSong = screenState == TopScreenState::PLAYLIST_VIEW && !pl.inHeader &&
                                !pl.playlists.empty() &&
                                pl.selSong == pl.playlists[pl.sel].songs.size() - 1;
        bool lastItem = (lastFile) || (lastInfo) || (lastPlaylist) || (lastPlaylistSong);
        bool downRepeat = (kHeld & KEY_DOWN) && !lastItem &&
                          (double) (now - downPressMs) > REPEAT_INITIAL_DELAY_MS &&
                          (double) (now - downRepeatMs) > REPEAT_INTERVAL_MS;

        if (!ctxHandled && screenState != TopScreenState::SETTINGS &&
            ((kDown & KEY_DOWN) || downRepeat)) {
            handleDownNav(kDown, downRepeat, downRepeatMs, screenState, fb, pl, info, maxInfoIdx);
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
            pl.coverLoadedFrom = "";
            if (!pl.playlists.empty() && pl.sel > pl.playlists.size()) {
                pl.sel = pl.playlists.size();
                pl.browserScroll = (pl.sel + 1 > (size_t) MAX_FILES) ? pl.sel + 1 - MAX_FILES : 0;
            }
        }

        // Load cover art for the currently viewed playlist
        if (screenState == TopScreenState::PLAYLIST_VIEW && !pl.playlists.empty() &&
            pl.sel < pl.playlists.size()) {
            const Playlist &curPl = pl.playlists[pl.sel];
            std::string coverPath = playlistCoverPath(curPl.name);
            if (coverPath != pl.coverLoadedFrom) {
                pl.coverLoadedFrom = coverPath;
                struct stat st;
                if (stat(coverPath.c_str(), &st) == 0) {
                    if (pl.hasCover) {
                        C3D_TexDelete(&pl.coverTex);
                    }
                    pl.hasCover =
                        loadC2DImage(coverPath.c_str(), pl.coverImage, pl.coverTex, pl.coverSubtex);
                } else {
                    if (pl.hasCover) {
                        C3D_TexDelete(&pl.coverTex);
                        pl.hasCover = false;
                    }
                }
            }
        }

        if (needsRender) {
            renderFrame(screenState,
                        fb,
                        pl,
                        info,
                        st,
                        s_ctx,
                        s_sub,
                        showLog,
                        SEEK_BAR_X,
                        SEEK_BAR_Y,
                        SEEK_BAR_W,
                        SEEK_BAR_H);
        }
        needsRender = false;
    }

    appExit();
    return 0;
}

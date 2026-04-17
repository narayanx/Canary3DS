#include <3ds.h>
#include <citro2d.h>
#include <dirent.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "audio_engine.h"
#include "filebrowser.h"
#include "gfx.h"
#include "image.h"
#include "playlist.h"

// Shared between the renderer and the touch hit-test
inline constexpr float SEEK_BAR_X = 8.0f;
inline constexpr float SEEK_BAR_Y = 196.0f;
inline constexpr float SEEK_BAR_W = 304.0f;
inline constexpr float SEEK_BAR_H = 18.0f;

enum class TopScreenState {
    FILEBROWSER,
    INFO,
    PLAYLIST_BROWSER,  // list of playlists
    PLAYLIST_VIEW,     // songs inside a selected playlist (specifically only accessible in from playlist browser)
};

enum class ContextMenuState {
    NONE,
    MAIN,            // root menu
    PLAYLIST_SELECT, // sub-menu: pick which playlist
};

int main(int argc, char* argv[]) {
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
        logToBottomScreen("Failed to init audio");
        waitForInput();
        gfxExit(); ndspExit(); romfsExit();
        return EXIT_FAILURE;
    }

    int32_t mainPrio = 0x30;
    svcGetThreadPriority(&mainPrio, CUR_THREAD_HANDLE);
    const Thread audioTid =
        threadCreate(audioThread, nullptr, AUDIO_THREAD_STACK_SZ,
                     mainPrio - 1, AUDIO_THREAD_AFFINITY, false);

    ndspSetCallback(audioCallback, nullptr);

    fileController.cwd = START_PATH;
    DIR* tmp = opendir(fileController.cwd.c_str());
    if (!tmp) {
        fileController.cwd = "sdmc:/";
        fileController.fileHistory.clear();
    } else {
        closedir(tmp);
        tmp = opendir("sdmc:/");
        int idx = 0;
        while (tmp) {
            dirent* ent = readdir(tmp);
            if (!ent) break;
            if (ent->d_type == DT_DIR && strncmp(ent->d_name, "Music", 6) == 0) {
                fileController.fileHistory.push_back({
                    (size_t)idx,
                    (size_t)idx >= (size_t)MAX_FILES ? (size_t)idx - MAX_FILES + 1 : 0
                });
                closedir(tmp); break;
            }
            ++idx;
        }
    }

    u64 upPressMs = 0, upRepeatMs = 0, downPressMs = 0, downRepeatMs = 0;

    // Multi-tap state for L and R shoulder buttons
    static constexpr u64 MULTI_TAP_WINDOW_MS = 350;
    u64  lTapTime = 0, rTapTime = 0;
    int  lTapCount = 0, rTapCount = 0;

    C2D_Image image; C3D_Tex tex; Tex3DS_SubTexture subtex;
    bool tryLoadImage = false, loadedImage = false, displayCoverArt = true;

    TopScreenState screenState = TopScreenState::FILEBROWSER;

    std::vector<Playlist> playlists;
    size_t selPlaylist = 0, selPlaylistSong = 0;
    bool   playlistsDirty = true;
    size_t fileBrowserScroll = 0, playlistBrowserScroll = 0, playlistViewScroll = 0;

    bool updateFiles = true, needsRender = true;

    static const std::vector<std::string> CTX_OPTS = {
        "Play next", "Add to queue", "Add to playlist >"
    };
    ContextMenuState ctxState = ContextMenuState::NONE;
    size_t ctxMenuIdx = 0, ctxPlaylistIdx = 0;
    std::string ctxSongPath;

    // Touch-seek state.
    // seekDragging:     true while the user's finger is held on the seek bar
    // seekDragProgress: normalised position (0-1) updated every frame while
    //                   dragging. (The actual decoder seek is issued on finger release.)
    bool  wasTouched       = false;
    bool  seekDragging     = false;
    float seekDragProgress = 0.0f;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        touchPosition touchPos;
        hidTouchRead(&touchPos);
        bool screenTouched = (touchPos.px != 0 || touchPos.py != 0);
        bool newTouch      = screenTouched && !wasTouched;
        bool touchReleased = !screenTouched && wasTouched;
        wasTouched = screenTouched;

        if (kDown & KEY_START) break;

        needsRender = kDown || kHeld || screenTouched ||
                      audioController.newSongStarted ||
                      audioController.songReady ||
                      needsRender;

        if (updateFiles) {
            fileController.files = getFiles(fileController.cwd.c_str());
            updateFiles = false;
        }

        // Touch-seek on the bottom screen.
        if (audioController.songReady) {
            float px = (float)touchPos.px;
            float py = (float)touchPos.py;
            bool  inBar = px >= SEEK_BAR_X && px <= SEEK_BAR_X + SEEK_BAR_W &&
                          py >= SEEK_BAR_Y && py <= SEEK_BAR_Y + SEEK_BAR_H;

            if (newTouch && inBar) {
                seekDragging = true;
                ndspChnSetPaused(0, true);
            }

            if (seekDragging && screenTouched) {
                float prog = (px - SEEK_BAR_X) / SEEK_BAR_W;
                seekDragProgress = std::max(0.0f, std::min(1.0f, prog));
            }

            if (seekDragging && touchReleased) {
                double dur = audioController.songDurationSeconds;
                if (dur > 0) {
                    audioController.seekTargetSeconds = (double)seekDragProgress * dur;
                    audioController.seekPending       = true;
                    LightEvent_Signal(&audioController.fillBufferEvent);
                }
                seekDragging = false;
            }
        }

        // Context-menu intercept
        bool ctxHandled = (ctxState != ContextMenuState::NONE);
        if (ctxHandled) {
            if (kDown & KEY_B) {
                ctxState = (ctxState == ContextMenuState::PLAYLIST_SELECT)
                           ? ContextMenuState::MAIN
                           : ContextMenuState::NONE;
                if (ctxState == ContextMenuState::MAIN) ctxMenuIdx = 2;
            }
            if (kDown & KEY_A) {
                if (ctxState == ContextMenuState::MAIN) {
                    if (ctxMenuIdx == 0) {
                        fileController.playQueue.push_front(ctxSongPath);
                        logToBottomScreen("Play next: " + ctxSongPath);
                        ctxState = ContextMenuState::NONE;
                    } else if (ctxMenuIdx == 1) {
                        enqueueSong(ctxSongPath);
                        ctxState = ContextMenuState::NONE;
                    } else {
                        if (playlists.empty()) {
                            logToBottomScreen("No playlists. Create one first.");
                            ctxState = ContextMenuState::NONE;
                        } else {
                            ctxPlaylistIdx = 0;
                            ctxState = ContextMenuState::PLAYLIST_SELECT;
                        }
                    }
                } else {
                    if (!playlists.empty() && ctxPlaylistIdx < playlists.size()) {
                        if (addSongToPlaylist(playlists[ctxPlaylistIdx].path, ctxSongPath)) {
                            playlists[ctxPlaylistIdx].songs.push_back(ctxSongPath);
                            logToBottomScreen("Added to \"" + playlists[ctxPlaylistIdx].name + "\"");
                        } else logToBottomScreen("Failed to add to playlist");
                    }
                    ctxState = ContextMenuState::NONE;
                }
            }
            if (kDown & KEY_UP) {
                if (ctxState == ContextMenuState::MAIN && ctxMenuIdx > 0) --ctxMenuIdx;
                else if (ctxState == ContextMenuState::PLAYLIST_SELECT && ctxPlaylistIdx > 0) --ctxPlaylistIdx;
            }
            if (kDown & KEY_DOWN) {
                if (ctxState == ContextMenuState::MAIN && ctxMenuIdx < CTX_OPTS.size()-1) ++ctxMenuIdx;
                else if (ctxState == ContextMenuState::PLAYLIST_SELECT &&
                         !playlists.empty() && ctxPlaylistIdx < playlists.size()-1) ++ctxPlaylistIdx;
            }
        }

        // A button
        if (!ctxHandled && (kDown & KEY_A)) {
            if (screenState == TopScreenState::FILEBROWSER) {
                auto ft = fileController.files[fileController.selectedFile].d_type;
                if (ft == DT_DIR) {
                    fileController.cwd += fileController.files[fileController.selectedFile].d_name;
                    fileController.cwd += '/';
                    fileController.fileHistory.push_back({fileController.selectedFile, fileBrowserScroll});
                    fileController.selectedFile = 0;
                    fileBrowserScroll = 0;
                    if (fileController.fileHistory.size() > MAX_DEPTH)
                        fileController.fileHistory.pop_front();
                    fileController.files = getFiles(fileController.cwd.c_str());
                } else if (ft == DT_REG) {
                    stopPlaybackIfPlaying();
                    char* nm = fileController.files[fileController.selectedFile].d_name;
                    if (playSong(fileController.cwd + nm)) {
                        logToBottomScreen("Playing: " + (std::string)nm);
                        screenState = TopScreenState::INFO;
                    }
                    fileController.playingFile = fileController.selectedFile;
                }
            } else if (screenState == TopScreenState::PLAYLIST_BROWSER) {
                if (!playlists.empty()) {
                    selPlaylistSong = 0; playlistViewScroll = 0;
                    screenState = TopScreenState::PLAYLIST_VIEW;
                }
            } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
                if (!playlists.empty() && selPlaylist < playlists.size() &&
                    !playlists[selPlaylist].songs.empty()) {
                    const Playlist& pl = playlists[selPlaylist];
                    stopPlaybackIfPlaying();
                    if (playSong(pl.songs[selPlaylistSong])) {
                        for (size_t i = selPlaylistSong + 1; i < pl.songs.size(); ++i)
                            enqueueSong(pl.songs[i]);
                        screenState = TopScreenState::INFO;
                    }
                }
            }
        }

        // X button
        if (!ctxHandled && (kDown & KEY_X)) {
            if (screenState == TopScreenState::FILEBROWSER) {
                if (!fileController.files.empty()) {
                    auto ft = fileController.files[fileController.selectedFile].d_type;
                    if (ft == DT_REG) {
                        ctxSongPath = fileController.cwd +
                                      fileController.files[fileController.selectedFile].d_name;
                        ctxMenuIdx = 0;
                        playlists  = loadPlaylists();
                        ctxState   = ContextMenuState::MAIN;
                    }
                }
            } else if (screenState == TopScreenState::INFO &&
                       !fileController.playQueue.empty()) {
                fileController.playQueue.erase(
                    fileController.playQueue.begin() + fileController.selectedQueueItem);
                if (fileController.selectedQueueItem > 0 &&
                    (size_t)fileController.selectedQueueItem >= fileController.playQueue.size())
                    --fileController.selectedQueueItem;
            } else if (screenState == TopScreenState::PLAYLIST_BROWSER && !playlists.empty()) {
                if (deletePlaylist(playlists[selPlaylist].path)) {
                    logToBottomScreen("Deleted: " + playlists[selPlaylist].name);
                    playlistsDirty = true;
                } else logToBottomScreen("Failed to delete playlist");
            } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
                if (!playlists.empty() && selPlaylist < playlists.size() &&
                    !playlists[selPlaylist].songs.empty()) {
                    if (removeSongFromPlaylist(playlists[selPlaylist].path, selPlaylistSong)) {
                        playlists[selPlaylist].songs.erase(
                            playlists[selPlaylist].songs.begin() + selPlaylistSong);
                        if (selPlaylistSong > 0 &&
                            selPlaylistSong >= playlists[selPlaylist].songs.size())
                            --selPlaylistSong;
                        const auto& songs = playlists[selPlaylist].songs;
                        if (playlistViewScroll > 0 &&
                            playlistViewScroll + MAX_FILES > songs.size())
                            playlistViewScroll = (songs.size() > (size_t)MAX_FILES)
                                                 ? songs.size() - MAX_FILES : 0;
                        logToBottomScreen("Removed song from playlist");
                    } else logToBottomScreen("Failed to remove song");
                }
            }
        }

        // B button
        if (!ctxHandled && (kDown & KEY_B)) {
            if (screenState == TopScreenState::INFO) {
                stopPlaybackIfPlaying();
                screenState  = TopScreenState::FILEBROWSER;
                tryLoadImage = false;
                logToBottomScreen("Stopping playback...");
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
                        fileBrowserScroll = rs;
                    } else {
                        fileController.selectedFile = 0;
                        fileBrowserScroll = 0;
                    }
                    fileController.files = getFiles(fileController.cwd.c_str());
                }
            }
        }

        // Y button
        if (!ctxHandled && (kDown & KEY_Y)) {
            if      (screenState == TopScreenState::FILEBROWSER)  screenState = TopScreenState::INFO;
            else if (screenState == TopScreenState::INFO)        { playlistsDirty = true; screenState = TopScreenState::PLAYLIST_BROWSER; }
            else                                                   screenState = TopScreenState::FILEBROWSER;
        }

        // SELECT – create playlist
        if (!ctxHandled && (kDown & KEY_SELECT) &&
            screenState == TopScreenState::PLAYLIST_BROWSER) {
            SwkbdState swkbd; char nameBuf[64] = {0};
            swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 63);
            swkbdSetHintText(&swkbd, "Playlist name");
            swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT,  "Cancel", false);
            swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "Create", true);
            if (swkbdInputText(&swkbd, nameBuf, sizeof(nameBuf)) == SWKBD_BUTTON_RIGHT
                && nameBuf[0]) {
                if (createPlaylist(nameBuf)) {
                    logToBottomScreen((std::string)"Created: " + nameBuf);
                    playlistsDirty = true;
                } else logToBottomScreen("Failed to create playlist");
            }
        }

        // Shoulder button multi-tap detection
        u64 now = osGetTime();

        if (kDown & KEY_L) {
            if (lTapCount > 0 && (now - lTapTime) <= MULTI_TAP_WINDOW_MS)
                ++lTapCount;
            else
                lTapCount = 1;
            lTapTime = now;
        }
        if (kDown & KEY_R) {
            if (rTapCount > 0 && (now - rTapTime) <= MULTI_TAP_WINDOW_MS)
                ++rTapCount;
            else
                rTapCount = 1;
            rTapTime = now;
        }

        // Commit L taps once the window expires
        if (lTapCount > 0 && (now - lTapTime) > MULTI_TAP_WINDOW_MS) {
            if (lTapCount >= 3 && audioController.songReady) {
                // Triple-tap L → previous song
                if (fileController.playingFile != 0) {
                    size_t prev = fileController.playingFile - 1;
                    stopPlaybackIfPlaying();
                    if (playSong(fileController.cwd + fileController.files[prev].d_name))
                        fileController.playingFile = prev;
                }
            }
            lTapCount = 0;
        }

        // Commit R taps once the window expires
        if (rTapCount > 0 && (now - rTapTime) > MULTI_TAP_WINDOW_MS) {
            if (rTapCount == 2 && audioController.songReady) {
                // Double-tap R: pause / resume
                bool paused = ndspChnIsPaused(0);
                ndspChnSetPaused(0, !paused);
            } else if (rTapCount >= 3 && audioController.songReady) {
                // Triple-tap R: next song
                if (fileController.playingFile < fileController.files.size() - 1)
                    goToNextSong();
            }
            rTapCount = 0;
        }

        // D-pad auto-repeat
        if (kDown & KEY_UP)   { upPressMs   = now; upRepeatMs   = now; }
        if (kDown & KEY_DOWN) { downPressMs = now; downRepeatMs = now; }

        bool firstItem =
            (screenState == TopScreenState::FILEBROWSER      && fileController.selectedFile == 0) ||
            (screenState == TopScreenState::INFO             && fileController.selectedQueueItem == 0) ||
            (screenState == TopScreenState::PLAYLIST_BROWSER && selPlaylist == 0) ||
            (screenState == TopScreenState::PLAYLIST_VIEW    && selPlaylistSong == 0);
        bool upRepeat = (kHeld & KEY_UP) && !firstItem &&
            (double)(now - upPressMs)  > REPEAT_INITIAL_DELAY_MS &&
            (double)(now - upRepeatMs) > REPEAT_INTERVAL_MS;

        if (!ctxHandled && ((kDown & KEY_UP) || upRepeat)) {
            if (upRepeat) upRepeatMs = now;
            if (screenState == TopScreenState::FILEBROWSER) {
                if (fileController.selectedFile > 0) {
                    --fileController.selectedFile;
                    if (fileController.selectedFile < fileBrowserScroll) --fileBrowserScroll;
                } else {
                    fileController.selectedFile = fileController.files.size() - 1;
                    fileBrowserScroll = (fileController.files.size() > (size_t)MAX_FILES)
                                        ? fileController.files.size() - MAX_FILES : 0;
                }
            } else if (screenState == TopScreenState::INFO && !fileController.playQueue.empty()) {
                if (fileController.selectedQueueItem > 0) --fileController.selectedQueueItem;
            } else if (screenState == TopScreenState::PLAYLIST_BROWSER) {
                if (selPlaylist > 0) {
                    --selPlaylist;
                    if (selPlaylist < playlistBrowserScroll) --playlistBrowserScroll;
                } else if (!upRepeat) {
                    selPlaylist = playlists.size() - 1;
                    playlistBrowserScroll = (playlists.size() > (size_t)MAX_FILES)
                                           ? playlists.size() - MAX_FILES : 0;
                }
            } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
                if (selPlaylistSong > 0) {
                    --selPlaylistSong;
                    if (selPlaylistSong < playlistViewScroll) --playlistViewScroll;
                } else if (!upRepeat && !playlists.empty()) {
                    const auto& s = playlists[selPlaylist].songs;
                    selPlaylistSong = s.size() - 1;
                    playlistViewScroll = (s.size() > (size_t)MAX_FILES) ? s.size() - MAX_FILES : 0;
                }
            }
        }

        bool lastItem =
            (screenState == TopScreenState::FILEBROWSER      && fileController.selectedFile == fileController.files.size()-1) ||
            (screenState == TopScreenState::INFO             && (size_t)fileController.selectedQueueItem == fileController.playQueue.size()-1) ||
            (screenState == TopScreenState::PLAYLIST_BROWSER && selPlaylist == playlists.size()-1) ||
            (screenState == TopScreenState::PLAYLIST_VIEW    && !playlists.empty() &&
                selPlaylistSong == playlists[selPlaylist].songs.size()-1);
        bool downRepeat = (kHeld & KEY_DOWN) && !lastItem &&
            (double)(now - downPressMs)  > REPEAT_INITIAL_DELAY_MS &&
            (double)(now - downRepeatMs) > REPEAT_INTERVAL_MS;

        if (!ctxHandled && ((kDown & KEY_DOWN) || downRepeat)) {
            if (downRepeat) downRepeatMs = now;
            if (screenState == TopScreenState::FILEBROWSER) {
                if (fileController.selectedFile < fileController.files.size()-1) {
                    ++fileController.selectedFile;
                    if (fileController.selectedFile >= fileBrowserScroll + MAX_FILES) ++fileBrowserScroll;
                } else { fileController.selectedFile = 0; fileBrowserScroll = 0; }
            } else if (screenState == TopScreenState::INFO && !fileController.playQueue.empty()) {
                if ((size_t)fileController.selectedQueueItem < fileController.playQueue.size()-1)
                    ++fileController.selectedQueueItem;
            } else if (screenState == TopScreenState::PLAYLIST_BROWSER) {
                if (selPlaylist < playlists.size()-1) {
                    ++selPlaylist;
                    if (selPlaylist >= playlistBrowserScroll + MAX_FILES) ++playlistBrowserScroll;
                } else if (!downRepeat) { selPlaylist = 0; playlistBrowserScroll = 0; }
            } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
                if (!playlists.empty() && selPlaylistSong < playlists[selPlaylist].songs.size()-1) {
                    ++selPlaylistSong;
                    if (selPlaylistSong >= playlistViewScroll + MAX_FILES) ++playlistViewScroll;
                } else if (!downRepeat && !playlists.empty()) { selPlaylistSong = 0; playlistViewScroll = 0; }
            }
        }

        // New song started – load cover art
        if (audioController.newSongStarted) {
            audioController.newSongStarted = false;
            updateFiles = true;
            bool ok = loadCoverArtForCurrentSong(image, tex, subtex, loadedImage);
            tryLoadImage = loadedImage && ok;
        }

        // Reload playlists when needed
        if (playlistsDirty &&
            (screenState == TopScreenState::PLAYLIST_BROWSER ||
             screenState == TopScreenState::PLAYLIST_VIEW)) {
            playlists      = loadPlaylists();
            playlistsDirty = false;
            if (!playlists.empty() && selPlaylist >= playlists.size()) {
                selPlaylist = playlists.size() - 1;
                playlistBrowserScroll = (selPlaylist >= MAX_FILES) ? selPlaylist - MAX_FILES + 1 : 0;
            }
        }

        if (needsRender) {
            consoleClear();
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

            C2D_TargetClear(top, CLEAR_COLOR);
            C2D_SceneBegin(top);

            if (screenState == TopScreenState::FILEBROWSER) {
                printC2DText(fileController.cwd, 0);
                printFiles(fileController.files, fileController.selectedFile,
                           fileBrowserScroll, MAX_FILES, 1);

            } else if (screenState == TopScreenState::INFO) {
                if (displayCoverArt && tryLoadImage)
                    drawCoverScaled(image, subtex, 10.0f, 10.0f);

                printQueue(fileController.playQueue,
                           fileController.selectedQueueItem, 1);

                {
                    double dur = audioController.songDurationSeconds;
                    bool   showDrag = seekDragging || audioController.seekPending;
                    double pos = (showDrag && dur > 0)
                                 ? (double)seekDragProgress * dur
                                 : audioController.songPositionSeconds;

                    drawProgressBar(10.0f, 206.0f, 190.0f, 7.0f,
                                    (dur > 0) ? (float)(pos / dur) : 0.0f);
                    drawTimeText(pos, dur, 10.0f, 217.0f, 0.44f, 0.44f);
                }

            } else if (screenState == TopScreenState::PLAYLIST_BROWSER) {
                printC2DText("Playlists  A=Open  X=Delete  SELECT=New", 0);
                std::vector<std::string> names;
                for (const auto& p : playlists) names.push_back(p.name);
                printStringList(names, selPlaylist, playlistBrowserScroll, 1);

            } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
                if (!playlists.empty() && selPlaylist < playlists.size()) {
                    const Playlist& pl = playlists[selPlaylist];
                    printC2DText("< " + pl.name + "  A=Play  X=Remove", 0);
                    std::vector<std::string> songNames;
                    for (const auto& s : pl.songs) {
                        size_t sl = s.find_last_of('/');
                        songNames.push_back(sl != std::string::npos ? s.substr(sl+1) : s);
                    }
                    printStringList(songNames, selPlaylistSong, playlistViewScroll, 1);
                }
            }

            // Context-menu overlay
            if (ctxState == ContextMenuState::MAIN) {
                float row = (float)(fileController.selectedFile - fileBrowserScroll) + 1.0f;
                printContextMenu(CTX_OPTS, ctxMenuIdx, 50.0f, 16.0f * row);
            } else if (ctxState == ContextMenuState::PLAYLIST_SELECT) {
                std::vector<std::string> pn;
                for (const auto& p : playlists) pn.push_back(p.name);
                float row = (float)(fileController.selectedFile - fileBrowserScroll) + 1.0f;
                printContextMenu(pn, ctxPlaylistIdx, 245.0f, 16.0f * row);
            }

            // Bottom screen
            C2D_TargetClear(bottom, BOTTOM_CLEAR_COLOR);
            C2D_SceneBegin(bottom);

            renderBottomScreen(
                audioController.songReady,
                audioController.songPositionSeconds,
                audioController.songDurationSeconds,
                audioController.songPath,
                audioController.songArtist,
                SEEK_BAR_X, SEEK_BAR_Y, SEEK_BAR_W, SEEK_BAR_H,
                (seekDragging || audioController.seekPending)
                    ? seekDragProgress : -1.0f);

            C3D_FrameEnd(0);
        }
        needsRender = false;
    }

    // Cleanup
    if (audioController.songReady) {audioController.stopPlayback = true;}
    audioController.interrupted = true;  // don't try to autoplay next song
    runThreads = false;
    LightEvent_Signal(&audioController.startEvent);
    LightEvent_Signal(&audioController.fillBufferEvent);

    threadJoin(audioTid,    UINT64_MAX); threadFree(audioTid);

    audioExit();
    ndspExit();
    sceneExit();
    C2D_Fini();
    C3D_Fini();
    romfsExit();
    gfxExit();
    return 0;
}

#include <3ds.h>
#include <citro2d.h>
#include <dirent.h>
#include <opusfile.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "filebrowser.h"
#include "gfx.h"
#include "image.h"
#include "opus.h"
#include "playlist.h"


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

    // Enable N3DS 804MHz operation, where available
    // osSetSpeedupEnable(true);
    // lets us keep playing when 3ds is closed
    aptSetSleepAllowed(false);

    // TODO add a msg telling ppl how to dump with luma3ds (likely bc dspfirm isn't dumped)
    ndspInit();

    LightEvent_Init(&opusController.startEvent, RESET_ONESHOT);
    LightEvent_Init(&opusController.doneEvent, RESET_ONESHOT);

    // we only want to initialize/deinit at program start/end not everytime a song is played
    if (!audioInit()) {
        logToBottomScreen("Failed to initialise audio\n");
        waitForInput();

        gfxExit();
        ndspExit();
        romfsExit();
        return EXIT_FAILURE;
    }

    // Spawn audio thread
    // main thread priority
    int32_t mainThreadPriority = 0x30;
    svcGetThreadPriority(&mainThreadPriority, CUR_THREAD_HANDLE);
    // lower number => higher actual priority
    // thread priorities must be between 0x18 and 0x3F
    int32_t audioThreadPriority = mainThreadPriority - 1;
    int32_t playNextThreadPriority = audioThreadPriority - 2;

    // takes no args as it gets opus filepath from global state struct
    const Thread threadId = threadCreate(audioThread, nullptr, THREAD_STACK_SZ, audioThreadPriority,
                                         THREAD_AFFINITY, false);
    const Thread playNextThreadId =
        threadCreate(playNextThread, nullptr, 4096, playNextThreadPriority, THREAD_AFFINITY, false);

    ndspSetCallback(opusCallback, NULL);

    fileController.cwd = START_PATH;

    // if Music folder doesn't exist, default to sd root
    DIR* tmp = opendir(fileController.cwd.c_str());
    if (tmp == nullptr) {
        fileController.cwd = "sdmc:/";
        fileController.fileHistory.clear();
    } else {
        closedir(tmp);
        // assumes start path is in sd card root TODO make more robust
        tmp = opendir("sdmc:/");
        int i = 0;
        while (tmp != nullptr) {
            dirent* ent = readdir(tmp);
            if (ent == nullptr) {
                break;
            }
            // TODO assumes the path is sdmc:/Music/ and that the Music folder in the root, change
            if (ent->d_type == DT_DIR && strncmp(ent->d_name, "Music", sizeof("Music")) == 0) {
                fileController.fileHistory.push_back({(size_t)i, (size_t)0});
                closedir(tmp);
                break;
            }
            i++;
        }
    }

    // Two-stage scroll repeat
    u64 upKeyPressTime_ms     = 0;
    u64 upLastRepeatTime_ms   = 0;
    u64 downKeyPressTime_ms   = 0;
    u64 downLastRepeatTime_ms = 0;

    // for displaying embedded cover art
    C2D_Image image;
    C3D_Tex tex;
    Tex3DS_SubTexture subtex;

    bool tryLoadImage = false;
    bool updateFiles = true;
    bool needsRender = true;

    // if we previously loaded image, need to free memory before loading next
    bool loadedImage = false;
    OpusTagData opusMetadata;
    bool displayCoverArt = true;

    // for having multiple top screen views (defaults to filebrowser on startup)
    TopScreenState screenState = TopScreenState::FILEBROWSER;

    // playlist state
    std::vector<Playlist> playlists;
    size_t selectedPlaylistIdx = 0;
    size_t selectedPlaylistSongIdx = 0;
    bool playlistsDirty = true;  // reload playlists on next visit to PLAYLIST_BROWSER
    size_t fileBrowserScrollOffset = 0;
    size_t playlistBrowserScrollOffset = 0;
    size_t playlistViewScrollOffset = 0;

    static const std::vector<std::string> CTX_MAIN_OPTIONS = {
        "Play next",
        "Add to queue",
        "Add to playlist >"
    };
    ContextMenuState ctxState   = ContextMenuState::NONE;
    size_t ctxMenuIdx           = 0;
    size_t ctxPlaylistIdx       = 0;
    std::string ctxSongPath;

    while (aptMainLoop()) {
        hidScanInput();

        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();
        touchPosition touchPos;
        hidTouchRead(&touchPos);

        if (kDown & KEY_START) {
            break;
        }

        // defaults to (0, 0)
        bool screenTouched = touchPos.px != 0 || touchPos.py != 0;
        // don't update on key held to avoid lag when scrolling
        if (kDown || screenTouched) {
            updateFiles = true;
        }
        // lazily render, have true on first frame
        needsRender = kDown || kHeld || screenTouched || opusController.newSongStarted || needsRender;

        if (updateFiles) {
            fileController.files = getFiles(fileController.cwd.c_str());
            updateFiles = false;
        }

        // Context menu intercepts key presses while active
        bool ctxHandled = (ctxState != ContextMenuState::NONE);
        if (ctxHandled) {
            if (kDown & KEY_B) {
                if (ctxState == ContextMenuState::PLAYLIST_SELECT) {
                    ctxState  = ContextMenuState::MAIN;
                    ctxMenuIdx = 2;  // keep "Add to playlist >" highlighted
                } else {
                    ctxState = ContextMenuState::NONE;
                }
            }
            if (kDown & KEY_A) {
                if (ctxState == ContextMenuState::MAIN) {
                    if (ctxMenuIdx == 0) {                        // Play next
                        fileController.playQueue.push_front(ctxSongPath);
                        logToBottomScreen("Play next: " + ctxSongPath);
                        ctxState = ContextMenuState::NONE;
                    } else if (ctxMenuIdx == 1) {                 // Add to queue
                        enqueueSong(ctxSongPath);
                        ctxState = ContextMenuState::NONE;
                    } else if (ctxMenuIdx == 2) {                 // Add to playlist >
                        if (playlists.empty()) {
                            logToBottomScreen("No playlists. Create one first (Y -> SELECT).");
                            ctxState = ContextMenuState::NONE;
                        } else {
                            ctxPlaylistIdx = 0;
                            ctxState = ContextMenuState::PLAYLIST_SELECT;
                        }
                    }
                } else if (ctxState == ContextMenuState::PLAYLIST_SELECT) {
                    if (!playlists.empty() && ctxPlaylistIdx < playlists.size()) {
                        if (addSongToPlaylist(playlists[ctxPlaylistIdx].path, ctxSongPath)) {
                            playlists[ctxPlaylistIdx].songs.push_back(ctxSongPath);
                            logToBottomScreen("Added to \"" + playlists[ctxPlaylistIdx].name + "\"");
                        } else {
                            logToBottomScreen("Failed to add to playlist");
                        }
                    }
                    ctxState = ContextMenuState::NONE;
                }
            }
            if (kDown & KEY_UP) {
                if (ctxState == ContextMenuState::MAIN && ctxMenuIdx > 0)
                    ctxMenuIdx--;
                else if (ctxState == ContextMenuState::PLAYLIST_SELECT && ctxPlaylistIdx > 0)
                    ctxPlaylistIdx--;
            }
            if (kDown & KEY_DOWN) {
                if (ctxState == ContextMenuState::MAIN &&
                    ctxMenuIdx < CTX_MAIN_OPTIONS.size() - 1)
                    ctxMenuIdx++;
                else if (ctxState == ContextMenuState::PLAYLIST_SELECT &&
                         !playlists.empty() &&
                         ctxPlaylistIdx < playlists.size() - 1)
                    ctxPlaylistIdx++;
            }
        }

        // A button
        if (!ctxHandled && (kDown & KEY_A)) {
            if (screenState == TopScreenState::FILEBROWSER) {
                // A: enter directory or play file
                auto fileType = fileController.files[fileController.selectedFile].d_type;
                if (fileType == DT_DIR) {
                    fileController.cwd += fileController.files[fileController.selectedFile].d_name;
                    fileController.cwd += '/';
                    // Save current cursor and scroll position before descending
                    fileController.fileHistory.push_back({fileController.selectedFile, fileBrowserScrollOffset});
                    fileController.selectedFile = 0;  // reset to first file in new directory
                    fileBrowserScrollOffset = 0;
                    if (fileController.fileHistory.size() > MAX_DEPTH) {
                        fileController.fileHistory.pop_front();
                    }
                    fileController.files = getFiles(fileController.cwd.c_str());
                } else if (fileType == DT_REG) {
                    stopPlaybackIfPlaying();
                    char* songFilename = fileController.files[fileController.selectedFile].d_name;

                    if (playSong(fileController.cwd + songFilename)) {
                        logToBottomScreen(("Playing file: " + (std::string)songFilename).c_str());
                        // switch to player screen when we play song with A
                        screenState = TopScreenState::INFO;
                    }
                    fileController.playingFile = fileController.selectedFile;
                }
            } else if (screenState == TopScreenState::PLAYLIST_BROWSER) {
                // A: open selected playlist
                if (!playlists.empty()) {
                    selectedPlaylistSongIdx = 0;
                    playlistViewScrollOffset = 0;
                    screenState = TopScreenState::PLAYLIST_VIEW;
                }
            } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
                // A: play selected song, then enqueue everything after it in the playlist
                if (!playlists.empty() &&
                    selectedPlaylistIdx < playlists.size() &&
                    !playlists[selectedPlaylistIdx].songs.empty()) {
                    const Playlist& pl = playlists[selectedPlaylistIdx];
                    const std::string& songPath = pl.songs[selectedPlaylistSongIdx];
                    stopPlaybackIfPlaying();
                    if (playSong(songPath)) {
                        size_t slash = songPath.find_last_of('/');
                        std::string songName =
                            (slash != std::string::npos) ? songPath.substr(slash + 1) : songPath;
                        logToBottomScreen("Playing: " + songName);

                        // Enqueue all songs after the selected one
                        for (size_t i = selectedPlaylistSongIdx + 1; i < pl.songs.size(); i++) {
                            enqueueSong(pl.songs[i]);
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
                    auto fileType = fileController.files[fileController.selectedFile].d_type;
                    if (fileType == DT_REG) {
                        ctxSongPath = fileController.cwd + fileController.files[fileController.selectedFile].d_name;
                        ctxMenuIdx = 0;
                        playlists = loadPlaylists();  // freshen list for the sub-menu
                        ctxState = ContextMenuState::MAIN;
                    }
                }
            } else if (screenState == TopScreenState::INFO && !fileController.playQueue.empty()) {
               fileController.playQueue.erase(
                   fileController.playQueue.begin() + fileController.selectedQueueItem);
               if (fileController.selectedQueueItem > 0 &&
                   (size_t)fileController.selectedQueueItem >= fileController.playQueue.size()) {
                       fileController.selectedQueueItem--;
                   }
            } else if (screenState == TopScreenState::PLAYLIST_BROWSER) {
                // X: delete selected playlist
                if (!playlists.empty()) {
                    if (deletePlaylist(playlists[selectedPlaylistIdx].path)) {
                        logToBottomScreen("Deleted playlist: " + playlists[selectedPlaylistIdx].name);
                        playlistsDirty = true;
                    } else {
                        logToBottomScreen("Failed to delete playlist");
                    }
                }
            } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
                // X: remove selected song from playlist
                if (!playlists.empty() &&
                    selectedPlaylistIdx < playlists.size() &&
                    !playlists[selectedPlaylistIdx].songs.empty()) {
                    if (removeSongFromPlaylist(playlists[selectedPlaylistIdx].path,
                                               selectedPlaylistSongIdx)) {
                        playlists[selectedPlaylistIdx].songs.erase(
                            playlists[selectedPlaylistIdx].songs.begin() + selectedPlaylistSongIdx);
                        if (selectedPlaylistSongIdx > 0 &&
                            selectedPlaylistSongIdx >= playlists[selectedPlaylistIdx].songs.size()) {
                            selectedPlaylistSongIdx--;
                        }
                        // clamp scroll offset if list shrank
                        const auto& songs = playlists[selectedPlaylistIdx].songs;
                        if (playlistViewScrollOffset > 0 &&
                            playlistViewScrollOffset + MAX_FILES > songs.size()) {
                            playlistViewScrollOffset =
                                songs.size() > (size_t)MAX_FILES ? songs.size() - MAX_FILES : 0;
                        }
                        logToBottomScreen("Removed song from playlist");
                    } else {
                        logToBottomScreen("Failed to remove song");
                    }
                }
            }
        }

        // B button
        if (!ctxHandled && kDown & KEY_B) {
            if (screenState == TopScreenState::INFO) {
                // pause and go back
                stopPlaybackIfPlaying();
                screenState = TopScreenState::FILEBROWSER;
                // don't display song's cover art anymore
                tryLoadImage = false;
                logToBottomScreen("Stopping playback...\n");
            } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
                screenState = TopScreenState::PLAYLIST_BROWSER;
            } else {
                // FILEBROWSER: go up a directory
                // TODO: maybe extract going up dir into a function START
                // ignore last character (trailing '/')
                size_t lastSlashIdx = fileController.cwd.rfind('/', fileController.cwd.size() - 2);
                // since we ignore the trailing slash, when at root no slash will be found
                if (lastSlashIdx != fileController.cwd.npos) {
                    // include slash
                    fileController.cwd = fileController.cwd.substr(0, lastSlashIdx + 1);
                    if (!fileController.fileHistory.empty()) {
                        // Restore both cursor and scroll offset exactly as they were
                        auto [restoredFile, restoredScroll] = fileController.fileHistory.back();
                        fileController.fileHistory.pop_back();
                        fileController.selectedFile = restoredFile;
                        fileBrowserScrollOffset = restoredScroll;
                    } else {
                        // default to first file in parent directory
                        fileController.selectedFile = 0;
                        fileBrowserScrollOffset = 0;
                    }
                    fileController.files = getFiles(fileController.cwd.c_str());
                }
                // maybe extract going up dir into a function END
            }
        }

        // Y button: cycle FILEBROWSER -> INFO -> PLAYLIST_BROWSER -> FILEBROWSER
        if (!ctxHandled && kDown & KEY_Y) {
            if (screenState == TopScreenState::FILEBROWSER) {
                screenState = TopScreenState::INFO;
            } else if (screenState == TopScreenState::INFO) {
                playlistsDirty = true;
                screenState = TopScreenState::PLAYLIST_BROWSER;
            } else if (screenState == TopScreenState::PLAYLIST_BROWSER ||
                       screenState == TopScreenState::PLAYLIST_VIEW) {
                screenState = TopScreenState::FILEBROWSER;
            }
        }

        if (!ctxHandled && kDown & KEY_SELECT) {
            if (screenState == TopScreenState::PLAYLIST_BROWSER) {
                // get playlist name
                SwkbdState swkbd;
                char nameBuf[64] = {0};
                swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 63);
                swkbdSetHintText(&swkbd, "Playlist name");
                swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
                swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "Create", true);
                SwkbdButton btn = swkbdInputText(&swkbd, nameBuf, sizeof(nameBuf));
                if (btn == SWKBD_BUTTON_RIGHT && nameBuf[0] != '\0') {
                    if (createPlaylist(nameBuf)) {
                        logToBottomScreen((std::string) "Created: " + nameBuf);
                        playlistsDirty = true;
                    } else {
                        logToBottomScreen("Failed to create playlist");
                    }
                }
            }
        }

        u64 now_ms = osGetTime();

        if (kDown & KEY_UP) {
            upKeyPressTime_ms   = now_ms;
            upLastRepeatTime_ms = now_ms;
        }
        if (kDown & KEY_DOWN) {
            downKeyPressTime_ms   = now_ms;
            downLastRepeatTime_ms = now_ms;
        }

        bool firstItemSelected =
            (screenState == TopScreenState::FILEBROWSER   && fileController.selectedFile == 0) ||
            (screenState == TopScreenState::INFO          && fileController.selectedQueueItem == 0) ||
            (screenState == TopScreenState::PLAYLIST_BROWSER && selectedPlaylistIdx == 0) ||
            (screenState == TopScreenState::PLAYLIST_VIEW    && selectedPlaylistSongIdx == 0);
        bool shouldUpAutoRepeat =
            (kHeld & KEY_UP) && !firstItemSelected &&
            ((double)(now_ms - upKeyPressTime_ms)   > REPEAT_INITIAL_DELAY_MS) &&
            ((double)(now_ms - upLastRepeatTime_ms) > REPEAT_INTERVAL_MS);

        // DPad Up: select previous item
        if (!ctxHandled && ((kDown & KEY_UP) || shouldUpAutoRepeat)) {
            if (shouldUpAutoRepeat) upLastRepeatTime_ms = now_ms;

            if (screenState == TopScreenState::FILEBROWSER) {
                if (fileController.selectedFile > 0) {
                    fileController.selectedFile--;
                    if (fileController.selectedFile < fileBrowserScrollOffset) {
                        fileBrowserScrollOffset--;
                    }
                } else {
                    // wrap to bottom
                    fileController.selectedFile = fileController.files.size() - 1;
                    fileBrowserScrollOffset = (fileController.files.size() > MAX_FILES)
                                                ? fileController.files.size() - MAX_FILES
                                                : 0;
                }
            } else if (screenState == TopScreenState::INFO && !fileController.playQueue.empty()) {
                if (fileController.selectedQueueItem > 0) {
                    fileController.selectedQueueItem--;
                }
            } else if (screenState == TopScreenState::PLAYLIST_BROWSER) {
                if (selectedPlaylistIdx > 0) {
                    selectedPlaylistIdx--;
                    if (selectedPlaylistIdx < playlistBrowserScrollOffset) {
                        playlistBrowserScrollOffset--;
                    }
                } else if (!shouldUpAutoRepeat) {
                    // wrap to bottom
                    selectedPlaylistIdx = playlists.size() - 1;
                    playlistBrowserScrollOffset = (playlists.size() > (size_t)MAX_FILES)
                                                    ? playlists.size() - MAX_FILES
                                                    : 0;
                }
            } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
                if (selectedPlaylistSongIdx > 0) {
                    selectedPlaylistSongIdx--;
                    if (selectedPlaylistSongIdx < playlistViewScrollOffset) {
                        playlistViewScrollOffset--;
                    }
                } else if (!shouldUpAutoRepeat && !playlists.empty()) {
                    // wrap to bottom
                    const auto& songs = playlists[selectedPlaylistIdx].songs;
                    selectedPlaylistSongIdx = songs.size() - 1;
                    playlistViewScrollOffset = (songs.size() > (size_t)MAX_FILES)
                                                ? songs.size() - MAX_FILES
                                                : 0;
                }
            }
        }

        bool lastItemSelected =
            (screenState == TopScreenState::FILEBROWSER      && fileController.selectedFile == fileController.files.size() - 1) ||
            (screenState == TopScreenState::INFO             && (size_t)fileController.selectedQueueItem == fileController.playQueue.size() - 1) ||
            (screenState == TopScreenState::PLAYLIST_BROWSER && selectedPlaylistIdx == playlists.size() - 1) ||
            (screenState == TopScreenState::PLAYLIST_VIEW    && !playlists.empty() &&
                selectedPlaylistSongIdx == playlists[selectedPlaylistIdx].songs.size() - 1);
        bool shouldDownAutoRepeat =
            (kHeld & KEY_DOWN) && !lastItemSelected &&
            ((double)(now_ms - downKeyPressTime_ms)   > REPEAT_INITIAL_DELAY_MS) &&
            ((double)(now_ms - downLastRepeatTime_ms) > REPEAT_INTERVAL_MS);

        // DPad Down: select next item
        if (!ctxHandled && ((kDown & KEY_DOWN) || shouldDownAutoRepeat)) {
            if (shouldDownAutoRepeat) downLastRepeatTime_ms = now_ms;

            if (screenState == TopScreenState::FILEBROWSER) {
                if (fileController.selectedFile < fileController.files.size() - 1) {
                    fileController.selectedFile++;
                    if (fileController.selectedFile >= fileBrowserScrollOffset + MAX_FILES) {
                        fileBrowserScrollOffset++;
                    }
                } else {
                    // wrap to top
                    fileController.selectedFile = 0;
                    fileBrowserScrollOffset = 0;
                }
            } else if (screenState == TopScreenState::INFO && !fileController.playQueue.empty()) {
                if ((size_t)fileController.selectedQueueItem < fileController.playQueue.size() - 1) {
                    fileController.selectedQueueItem++;
                }
            } else if (screenState == TopScreenState::PLAYLIST_BROWSER) {
                if (selectedPlaylistIdx < playlists.size() - 1) {
                    selectedPlaylistIdx++;
                    if (selectedPlaylistIdx >= playlistBrowserScrollOffset + MAX_FILES) {
                        playlistBrowserScrollOffset++;
                    }
                } else if (!shouldDownAutoRepeat) {
                    // wrap to top
                    selectedPlaylistIdx = 0;
                    playlistBrowserScrollOffset = 0;
                }
            } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
                if (!playlists.empty() &&
                    selectedPlaylistSongIdx < playlists[selectedPlaylistIdx].songs.size() - 1) {
                    selectedPlaylistSongIdx++;
                    if (selectedPlaylistSongIdx >= playlistViewScrollOffset + MAX_FILES) {
                        playlistViewScrollOffset++;
                    }
                } else if (!shouldDownAutoRepeat && !playlists.empty()) {
                    // wrap to top
                    selectedPlaylistSongIdx = 0;
                    playlistViewScrollOffset = 0;
                }
            }
        }

        // Left shoulder: go to previous song in folder
        if (kDown & KEY_L && fileController.playingFile != 0 && opusController.songReady) {
            size_t nextSongIdx = fileController.playingFile - 1;
            stopPlaybackIfPlaying();
            std::string nextSongPath =
                fileController.cwd + fileController.files[nextSongIdx].d_name;
            playSong(nextSongPath);
            fileController.playingFile = nextSongIdx;
            logToBottomScreen(
                ("Playing previous song: " + (std::string)fileController.files[nextSongIdx].d_name)
                    .c_str());
        }

        // Right shoulder: go to next song in folder
        if (kDown & KEY_R && fileController.playingFile < fileController.files.size() - 1
            && opusController.songReady) {
            goToNextSong();
        }

        if (opusController.newSongStarted) {
            opusController.newSongStarted = false;
            
            updateFiles = true;  // for autoplay next where no button is pressed but we want to update

            bool ok = loadCoverArtForCurrentSong(image, tex, subtex, loadedImage);
            // if no cover art found don't display anything
            tryLoadImage = loadedImage && ok;
        }

        // Reload playlists if dirty and we're in a playlist screen
        if (playlistsDirty &&
            (screenState == TopScreenState::PLAYLIST_BROWSER ||
             screenState == TopScreenState::PLAYLIST_VIEW)) {
            playlists = loadPlaylists();
            playlistsDirty = false;
            // Clamp indices after reload
            if (!playlists.empty() && selectedPlaylistIdx >= playlists.size()) {
                selectedPlaylistIdx = playlists.size() - 1;
                playlistBrowserScrollOffset =
                    selectedPlaylistIdx >= MAX_FILES ? selectedPlaylistIdx - MAX_FILES + 1 : 0;
            }
        }

        // Rendering
        if (needsRender) {
            consoleClear();
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C2D_TargetClear(top, CLEAR_COLOR);
            C2D_SceneBegin(top);

            if (screenState == TopScreenState::FILEBROWSER) {
                printC2DText(fileController.cwd, 0);
                printFiles(fileController.files, fileController.selectedFile, fileBrowserScrollOffset, MAX_FILES, 1);
            } else if (screenState == TopScreenState::INFO) {
                if (displayCoverArt && tryLoadImage) {
                    drawCoverScaled(image, subtex, 10.0f, 10.0f);
                }
                printQueue(fileController.playQueue, fileController.selectedQueueItem, 1);

            } else if (screenState == TopScreenState::PLAYLIST_BROWSER) {
                printC2DText("Playlists  A=Open  X=Delete  SELECT=New", 0);
                std::vector<std::string> names;
                for (const auto& p : playlists) {
                    names.push_back(p.name);
                }
                printStringList(names, selectedPlaylistIdx, playlistBrowserScrollOffset, 1);

            } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
                if (!playlists.empty() && selectedPlaylistIdx < playlists.size()) {
                    const Playlist& pl = playlists[selectedPlaylistIdx];
                    printC2DText("< " + pl.name + "  A=Play  X=Remove", 0);
                    // Display just filenames, not full paths
                    std::vector<std::string> songNames;
                    for (const auto& s : pl.songs) {
                        size_t slash = s.find_last_of('/');
                        songNames.push_back(slash != std::string::npos ? s.substr(slash + 1) : s);
                    }
                    printStringList(songNames, selectedPlaylistSongIdx, playlistViewScrollOffset, 1);
                }
            }
            // Context-menu overlay (drawn on top of everything else)
            if (ctxState == ContextMenuState::MAIN) {
                const float LINE_Y_OFFSET = 16.0f;
                float visualRow = (float)(fileController.selectedFile - fileBrowserScrollOffset) + 1.0f;
                float anchorX = 10.0f + 40.0f;
                float anchorY = LINE_Y_OFFSET * visualRow;
                printContextMenu(CTX_MAIN_OPTIONS, ctxMenuIdx, anchorX, anchorY);
            } else if (ctxState == ContextMenuState::PLAYLIST_SELECT) {
                std::vector<std::string> pNames;
                for (const auto& p : playlists) pNames.push_back(p.name);
                const float LINE_Y_OFFSET = 16.0f;
                float visualRow = (float)(fileController.selectedFile - fileBrowserScrollOffset) + 1.0f;
                printContextMenu(pNames, ctxPlaylistIdx, 10.0f + 40.0f + 195.0f, LINE_Y_OFFSET * visualRow);
            }
            C3D_FrameEnd(0);
        }
        needsRender = false;
    }

    // necessary for exiting when a file is playing for some reason
    if (opusController.songReady) {
        opusController.stopPlayback = true;
        logToBottomScreen("cleanup, stopping playback...\n");
    }

    runThreads = false;
    // signal audio thread (it finishes since the flag is set to false)
    LightEvent_Signal(&opusController.startEvent);
    // for playNextThread 
    LightEvent_Signal(&opusController.doneEvent);

    // free threads
    threadJoin(threadId, UINT64_MAX);
    threadFree(threadId);
    threadJoin(playNextThreadId, UINT64_MAX);
    threadFree(playNextThreadId);

    audioExit();
    ndspExit();

    sceneExit();

    C2D_Fini();
    C3D_Fini();

    romfsExit();
    gfxExit();

    return 0;
}

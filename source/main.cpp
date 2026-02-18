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


enum class TopScreenState {
    FILEBROWSER,
    INFO,
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

    // loadC2DImage("romfs:/carina_nebula.png", image, tex, subtex);
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
                fileController.fileHistory.push_back(i);
                closedir(tmp);
                break;
            }
            i++;
        }
    }

    // for holding down with scrolling to auto repeat
    u64 lastUpScrollTime_ms = osGetTime();
    u64 lastDownScrollTime_ms = osGetTime();

    // for displaying embedded cover art
    C2D_Image image;
    C3D_Tex tex;
    Tex3DS_SubTexture subtex;
    // try to fix line in top left corner
    C3D_TexSetWrap(&tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    bool tryLoadImage = false;
    bool updateFiles = true;
    // if we previously loaded image, need to free memory before loading next
    bool loadedImage = false;
    OpusTagData opusMetadata;
    // if user wants to control cover art displaying/not displaying
    bool displayCoverArt = true;

    // for having multiple top screen views (defaults to filebrowser on startup)
    TopScreenState screenState = TopScreenState::FILEBROWSER;

    while (aptMainLoop()) {
        hidScanInput();

        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();
        touchPosition touchPos;
        hidTouchRead(&touchPos);

        if (kDown & KEY_START) {
            break;
        }

        // TODO test on 3ds how many files it takes to run out of memory (std::vector allocates on
        // heap). Based on that decide if storing all files in cwd at once is viable or if smth
        // different is needed std::vector<dirent> files = getFiles(cwd.c_str());

        // defaults to (0, 0)
        bool screenTouched = touchPos.px != 0 || touchPos.py != 0;
        if (kDown || kHeld || screenTouched) {
            updateFiles = true;  // only update screen when a button is pressed
        }

        if (updateFiles) {
            fileController.files = getFiles(fileController.cwd.c_str());
        }
        if (kDown & KEY_A) {
            if (screenState == TopScreenState::FILEBROWSER) {
                // A: enter directory
                auto fileType = fileController.files[fileController.selectedFile].d_type;
                if (fileType == DT_DIR) {
                    fileController.cwd += fileController.files[fileController.selectedFile].d_name;
                    fileController.cwd += '/';
                    fileController.fileHistory.push_back(fileController.selectedFile);
                    fileController.selectedFile = 0;  // reset to first file in new directory
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
            }
        }

        if (kDown & KEY_X) {
            if (screenState == TopScreenState::FILEBROWSER) {
                auto fileType = fileController.files[fileController.selectedFile].d_type;
                if (fileType == DT_REG) {
                    std::string songPath =
                        fileController.cwd + fileController.files[fileController.selectedFile].d_name;
                    enqueueSong(songPath);
                }
            } else if (screenState == TopScreenState::INFO && !fileController.playQueue.empty()) {
               fileController.playQueue.erase(
                   fileController.playQueue.begin() + fileController.selectedQueueItem);
               if (fileController.selectedQueueItem > 0 &&
                   (size_t)fileController.selectedQueueItem >= fileController.playQueue.size()) {
                       fileController.selectedQueueItem--;
                   }

            }
        }

        if (kDown & KEY_B) {
            // if song is playing and user presses B, stop playback instead of going up a directory
            if (screenState == TopScreenState::INFO) {
                // pause and go back
                stopPlaybackIfPlaying();
                screenState = TopScreenState::FILEBROWSER;
                // don't display song's cover art anymore
                tryLoadImage = false;
                logToBottomScreen("Stopping playback...\n");
            } else {
                // TODO: maybe extract going up dir into a function START
                // ignore last character (trailing '/')
                size_t lastSlashIdx = fileController.cwd.rfind('/', fileController.cwd.size() - 2);
                // since we ignore the trailing slash, when at root no slash will be found
                if (lastSlashIdx != fileController.cwd.npos) {
                    // include slash
                    fileController.cwd = fileController.cwd.substr(0, lastSlashIdx + 1);
                    if (!fileController.fileHistory.empty()) {
                        fileController.selectedFile = fileController.fileHistory.back();
                        fileController.fileHistory.pop_back();
                    } else {
                        // default to first file in parent directory
                        fileController.selectedFile = 0;
                    }
                    fileController.files = getFiles(fileController.cwd.c_str());
                }
                // maybe extract going up dir into a function END
            }
        }

        // Y: switch between top screen views
        if (kDown & KEY_Y) {
            if (screenState == TopScreenState::FILEBROWSER) {
                screenState = TopScreenState::INFO;
            } else {
                screenState = TopScreenState::FILEBROWSER;
            }
        }

        double elapsedUp_ms = osGetTime() - lastUpScrollTime_ms;
        bool firstFileSelected = fileController.selectedFile == 0;
        bool shouldUpAutoRepeat =
            elapsedUp_ms > REPEAT_DELAY_MS && (kHeld & KEY_UP) && (!firstFileSelected);
        // DPad Up/Circle Pad Up: select previous file
        if ((kDown & KEY_UP) || shouldUpAutoRepeat) {
            if (screenState == TopScreenState::FILEBROWSER) {
                if (fileController.selectedFile > 0) {
                    fileController.selectedFile--;
                } else {
                    fileController.selectedFile = fileController.files.size() - 1;
                }
                lastUpScrollTime_ms = osGetTime();
            } else if (screenState == TopScreenState::INFO && !fileController.playQueue.empty()) {
                if (fileController.selectedQueueItem > 0) {
                    fileController.selectedQueueItem--;
                }
                lastUpScrollTime_ms = osGetTime();
            }
        }
        // DPad Down/Circle Pad Down: select next file
        double elapsedDown_ms = osGetTime() - lastDownScrollTime_ms;
        bool lastFileSelected = fileController.selectedFile == fileController.files.size() - 1;
        bool shouldDownAutoRepeat =
            elapsedDown_ms > REPEAT_DELAY_MS && (kHeld & KEY_DOWN) && (!lastFileSelected);
        if ((kDown & KEY_DOWN) || shouldDownAutoRepeat) {
            if (screenState == TopScreenState::FILEBROWSER) {
                if (fileController.selectedFile < fileController.files.size() - 1) {
                    fileController.selectedFile++;
                } else {
                    fileController.selectedFile = 0;
                }
                lastDownScrollTime_ms = osGetTime();
            } else if (screenState == TopScreenState::INFO && !fileController.playQueue.empty()) {
                if ((size_t)fileController.selectedQueueItem < fileController.playQueue.size() - 1) {
                    fileController.selectedQueueItem++;
                }
                lastDownScrollTime_ms = osGetTime();
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
            size_t nextSongIdx = fileController.playingFile + 1;
            stopPlaybackIfPlaying();
            std::string nextSongPath =
                fileController.cwd + fileController.files[nextSongIdx].d_name;
            playSong(nextSongPath);
            fileController.playingFile = nextSongIdx;
            logToBottomScreen(
                ("Playing next song: " + (std::string)fileController.files[nextSongIdx].d_name)
                    .c_str());
        }

        // TODO (this is just for testing to have way to toggle on/off cover art), eventually remove this and add setting in settings menu
        if (kDown & KEY_SELECT) {
            displayCoverArt = !displayCoverArt;
        }

        if (opusController.newSongStarted) {
            opusController.newSongStarted = false;
            
            updateFiles = true;  // for autoplay next where no button is pressed but we want to update

            bool ok = loadCoverArtForCurrentSong(image, tex, subtex, loadedImage);
            // if no cover art found don't display anything
            tryLoadImage = loadedImage && ok;
        }

        if (updateFiles) {
            // TODO maybe extract this into class, getting a bit cluttered with checking enum for state of gui
            consoleClear();
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C2D_TargetClear(top, CLEAR_COLOR);
            C2D_SceneBegin(top);
            if (screenState == TopScreenState::FILEBROWSER) {
                printC2DText(fileController.cwd, 0);
                printFiles(fileController.files, fileController.selectedFile, MAX_FILES, 1);
            }  else if (screenState == TopScreenState::INFO) {
                if (displayCoverArt) {
                    // redraw image everytime rest of the screen updated (could change to smarter scheme
                    // later)
                    if (tryLoadImage) {
                        drawCoverScaled(image, subtex, 10.0f, 10.0f);
                    }
                }
                printQueue(fileController.playQueue, fileController.selectedQueueItem, 1);
            }
            C3D_FrameEnd(0);
        }
        updateFiles = false;
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

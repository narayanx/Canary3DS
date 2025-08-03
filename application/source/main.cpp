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
#include "opus.h"

int main(int argc, char *argv[]) {
    romfsInit();
    gfxInitDefault();

    // from 3ds-examples/graphics/printing/system-font/source/main.c START
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    // // Create screen
    // top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    // bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    // Initialize the scene
    sceneInit();

    // from 3ds-examples/graphics/printing/system-font/source/main.c END

    // Enable N3DS 804MHz operation, where available
    // osSetSpeedupEnable(true);

    // lets us keep playing when 3ds is closed
    aptSetSleepAllowed(false);

    // TODO add a msg telling ppl how to dump with luma3ds (likely bc dspfirm isn't dumped)
    ndspInit();

    LightEvent_Init(&opus_controller.startEvent, RESET_ONESHOT);
    LightEvent_Init(&opus_controller.doneEvent, RESET_ONESHOT);

    // we only want to initialize/deinit at program start/end not everytime a song is played
    if (!audioInit()) {
        printf("Failed to initialise audio\n");
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

    file_controller.cwd = START_PATH;

    // if Music folder doesn't exist, default to sd root
    DIR *tmp = opendir(file_controller.cwd.c_str());
    if (tmp == nullptr) {
        file_controller.cwd = "sdmc:/";
        file_controller.fileHistory.clear();
    } else {
        closedir(tmp);
        // assumes start path is in sd card root TODO make more robust
        tmp = opendir("sdmc:/");
        int i = 0;
        while (tmp != nullptr) {
            dirent *ent = readdir(tmp);
            if (ent == nullptr) {
                break;
            }
            // TODO assumes the path is sdmc:/Music/ and that the Music folder in the root, change
            if (ent->d_type == DT_DIR && strncmp(ent->d_name, "Music", sizeof("Music")) == 0) {
                file_controller.fileHistory.push_back(i);
                closedir(tmp);
                break;
            }
            i++;
        }
    }

    // for holding down with scrolling to auto repeat
    u64 lastUpScrollTime_ms = osGetTime();
    u64 lastDownScrollTime_ms = osGetTime();

    bool update_files = true;

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
            update_files = true;  // only update screen when a button is pressed
        }

        if (update_files) {
            file_controller.files = getFiles(file_controller.cwd.c_str());
        }
        // A: enter directory
        if (kDown & KEY_A) {
            auto file_type = file_controller.files[file_controller.selectedFile].d_type;
            if (file_type == DT_DIR) {
                file_controller.cwd += file_controller.files[file_controller.selectedFile].d_name;
                file_controller.cwd += '/';
                file_controller.fileHistory.push_back(file_controller.selectedFile);
                file_controller.selectedFile = 0;  // reset to first file in new directory
                if (file_controller.fileHistory.size() > MAX_DEPTH) {
                    file_controller.fileHistory.pop_front();
                }
                file_controller.files = getFiles(file_controller.cwd.c_str());
            } else if (file_type == DT_REG) {
                stopPlaybackIfPlaying();
                char *song_filename = file_controller.files[file_controller.selectedFile].d_name;
                // PrintConsole *prev = consoleSelect(&bottomConsole);
                logToBottomScreen(("Playing file: " + (std::string)song_filename).c_str());
                // consoleSelect(prev);
                playSong(file_controller.cwd + song_filename);
                file_controller.playingFile = file_controller.selectedFile;
            }
        }

        if (kDown & KEY_B) {
            // if song is playing and user presses B, stop playback instead of going up a directory
            if (opus_controller.songReady) {
                opus_controller.stopPlayback = true;

                opus_controller.interrupted = true;
                logToBottomScreen("Stopping playback...\n");
            } else {
                // TODO: maybe extract going up dir into a function START
                // ignore last character (trailing '/')
                size_t last_slash_idx =
                    file_controller.cwd.rfind('/', file_controller.cwd.size() - 2);
                // since we ignore the trailing slash, when at root no slash will be found
                if (last_slash_idx != file_controller.cwd.npos) {
                    // include slash
                    file_controller.cwd = file_controller.cwd.substr(0, last_slash_idx + 1);
                    if (!file_controller.fileHistory.empty()) {
                        file_controller.selectedFile = file_controller.fileHistory.back();
                        file_controller.fileHistory.pop_back();
                    } else {
                        // default to first file in parent directory
                        file_controller.selectedFile = 0;
                    }
                    file_controller.files = getFiles(file_controller.cwd.c_str());
                }
                // maybe extract going up dir into a function END
            }
        }

        double elapsedUp_ms = osGetTime() - lastUpScrollTime_ms;
        bool firstFileSelected = file_controller.selectedFile == 0;
        bool shouldUpAutoRepeat =
            elapsedUp_ms > REPEAT_DELAY_MS && (kHeld & KEY_UP) && (!firstFileSelected);
        // DPad Up/Circle Pad Up: select previous file
        if ((kDown & KEY_UP) || shouldUpAutoRepeat) {
            if (file_controller.selectedFile > 0) {
                file_controller.selectedFile--;
            } else {
                // wraparound TODO make it so holding up doesn't wraparound, only when tapping when
                // first file selected
                file_controller.selectedFile = file_controller.files.size() - 1;
            }
            lastUpScrollTime_ms = osGetTime();
        }

        // DPad Down/Circle Pad Down: select next file
        double elapsedDown_ms = osGetTime() - lastDownScrollTime_ms;
        bool lastFileSelected = file_controller.selectedFile == file_controller.files.size() - 1;
        bool shouldDownAutoRepeat =
            elapsedDown_ms > REPEAT_DELAY_MS && (kHeld & KEY_DOWN) && (!lastFileSelected);
        if ((kDown & KEY_DOWN) || shouldDownAutoRepeat) {
            if (file_controller.selectedFile < file_controller.files.size() - 1) {
                file_controller.selectedFile++;
            } else {
                file_controller.selectedFile = 0;
            }
            lastDownScrollTime_ms = osGetTime();
        }

        // Left shoulder: go to previous song in folder
        if (kDown & KEY_L && file_controller.playingFile != 0 && opus_controller.songReady) {
            size_t nextSongIdx = file_controller.playingFile - 1;
            stopPlaybackIfPlaying();
            std::string nextSongPath =
                file_controller.cwd + file_controller.files[nextSongIdx].d_name;
            // TODO maybe abstact so setting of this bool is done when playSong is called START
            playSong(nextSongPath);
            file_controller.playingFile = nextSongIdx;
            // TODO maybe abstact so setting of this bool is done when playSong is called END
            logToBottomScreen(
                ("Playing previous song: " + (std::string)file_controller.files[nextSongIdx].d_name)
                    .c_str());
        }

        // Right shoulder: go to next song in folder
        if (kDown & KEY_R && file_controller.playingFile < file_controller.files.size() - 1
            && opus_controller.songReady) {
            size_t nextSongIdx = file_controller.playingFile + 1;
            stopPlaybackIfPlaying();
            std::string nextSongPath =
                file_controller.cwd + file_controller.files[nextSongIdx].d_name;
            // TODO maybe abstact so setting of this bool is done when playSong is called START
            playSong(nextSongPath);
            file_controller.playingFile = nextSongIdx;
            // TODO maybe abstact so setting of this bool is done when playSong is called END
            logToBottomScreen(
                ("Playing next song: " + (std::string)file_controller.files[nextSongIdx].d_name)
                    .c_str());
        }

        if (update_files) {
            consoleClear();
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C2D_TargetClear(top, CLEAR_COLOR);
            C2D_SceneBegin(top);
            printC2DText(file_controller.cwd, 0);
            printC2DText("selected file index: " + std::to_string(file_controller.selectedFile), 1);
            printFiles(file_controller.files, file_controller.selectedFile, 10, 2);
            C3D_FrameEnd(0);
        }
        update_files = false;
    }
    // necessary for exiting when a file is playing for some reason
    if (opus_controller.songReady) {
        opus_controller.stopPlayback = true;
        logToBottomScreen("cleanup, stopping playback...\n");
    }

    run_threads = false;
    // signal audio thread (it finishes since the flag is set to false)
    LightEvent_Signal(&opus_controller.startEvent);

    // free threads
    threadJoin(threadId, UINT64_MAX);
    threadFree(threadId);
    threadJoin(playNextThreadId, UINT64_MAX);
    threadFree(playNextThreadId);

    audioExit();
    ndspExit();

    // from 3ds-examples/graphics/printing/system-font/source/main.c START
    // Deinitialize the scene
    sceneExit();

    // Deinitialize the libs
    C2D_Fini();
    C3D_Fini();
    // from 3ds-examples/graphics/printing/system-font/source/main.c END

    romfsExit();
    gfxExit();

    return 0;
}

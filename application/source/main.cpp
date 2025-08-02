#include <3ds.h>
#include <citro2d.h>
#include <dirent.h>
#include <opusfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <deque>
#include <string>
#include <vector>

// max file name seems to be 255, file paths are concatenated filenames
const int MAX_PATH_CHAR_LENGTH = 4096;
// max files to display at once TODO change back to 14 once I'm done debugging
const int MAX_FILES = 10;
// delay before auto repeat of input starts
const double REPEAT_DELAY_MS = 175.0;
// make sure to have trailing '/' character
const std::string START_PATH = "sdmc:/Music/";
// stop saving depth after this many directories (conserve memory, TODO allow changing in settings)
const size_t MAX_DEPTH = 20;

const u32 CLEAR_COLOR = C2D_Color32(0x0D, 0x1F, 0x2D, 0xFF);
C2D_TextBuf g_dynamicBuf;

// TODO kinda temporary for debuggging can probably remove later
PrintConsole topConsole, bottomConsole;
C3D_RenderTarget *top, *bottom;

// SOURCE 3ds-examples/audio/opus-decoding START
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
ndspWaveBuf s_waveBufs[3];
int16_t *s_audioBuffer = NULL;
static const int SAMPLE_RATE = 48000;                         // Opus is fixed at 48kHz
static const int SAMPLES_PER_BUF = SAMPLE_RATE * 120 / 1000;  // 120ms buffer
static const int CHANNELS_PER_SAMPLE = 2;                     // We ask libopusfile for
                                                              // stereo output; it will down
                                                              // -mix for us as necessary.

static const int THREAD_AFFINITY = -1;         // Execute thread on any core
static const int THREAD_STACK_SZ = 32 * 1024;  // 32kB stack for audio thread

static const size_t WAVEBUF_SIZE =
    SAMPLES_PER_BUF * CHANNELS_PER_SAMPLE * sizeof(int16_t);  // Size of NDSP wavebufs
// SOURCE 3ds-examples/audio/opus-decoding END
volatile bool run_threads = true;

struct OpusController {
    std::string songPath;
    OggOpusFile *file;
    volatile bool songReady;
    volatile bool stopPlayback;
    volatile bool interrupted;   // distinguish between end of song and user interrupting playback
    LightEvent startEvent;       // tells audio thread to start playback
    LightEvent doneEvent;        // for main thread to know when song actually stopped
    LightEvent fillBufferEvent;  // the callback function needs a way to signal the audio thread
};

OpusController opus_controller = {
    .songPath = "",
    .file = nullptr,
    .songReady = false,  // also can be used to check if song is playing
    .stopPlayback = false,
    .interrupted = false,  // don't autoplay next if user stopped song
    .startEvent = {0},
    .doneEvent = {0},
    .fillBufferEvent = {0}};

struct FileController {
    std::string cwd;
    std::vector<dirent> files;
    std::deque<size_t> fileHistory;
    size_t selectedFile;
    size_t playingFile;
};

FileController file_controller = {
    .cwd = "sdmc:/Music/",
    .files = {},
    .fileHistory = {},
    .selectedFile = 0,
    .playingFile = 0,
};

void printC2DText(std::string msg, size_t lineOffset = 0) {
    C2D_TextBufClear(g_dynamicBuf);

    const float BASE_Y_OFFSET = 8.0f;
    float y_offset = 16.0f * (lineOffset) + BASE_Y_OFFSET;

    char buf[160];
    C2D_Text dynText;
    snprintf(buf, sizeof(buf), "%s", msg.c_str());
    C2D_TextParse(&dynText, g_dynamicBuf, buf);
    C2D_TextOptimize(&dynText);
    C2D_DrawText(&dynText, C2D_AlignLeft | C2D_WithColor, 10.0f, y_offset, 0.5f, 0.5f, 0.5f,
                 C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f));
}

void logToBottomScreen(const char *message) {
    static int line = 0;
    if (line == 0) {
        C2D_TargetClear(bottom, CLEAR_COLOR);
    }
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_SceneBegin(bottom);
    printC2DText(message, line);
    C3D_FrameEnd(0);
    line++;
    // arbitrary cutoff
    if (line >= 14) {
        line = 0;
    }
}

// SOURCE 3ds-examples/audio/opus-decoding START
// Retrieve strings for libopusfile errors
// Sourced from David Gow's example code: https://davidgow.net/files/opusal.cpp
const char *opusStrError(int error) {
    switch (error) {
        case OP_FALSE:
            return "OP_FALSE: A request did not succeed.";
        case OP_HOLE:
            return "OP_HOLE: There was a hole in the page sequence numbers.";
        case OP_EREAD:
            return "OP_EREAD: An underlying read, seek or tell operation "
                   "failed.";
        case OP_EFAULT:
            return "OP_EFAULT: A NULL pointer was passed where none was "
                   "expected, or an internal library error was encountered.";
        case OP_EIMPL:
            return "OP_EIMPL: The stream used a feature which is not "
                   "implemented.";
        case OP_EINVAL:
            return "OP_EINVAL: One or more parameters to a function were "
                   "invalid.";
        case OP_ENOTFORMAT:
            return "OP_ENOTFORMAT: This is not a valid Ogg Opus stream.";
        case OP_EBADHEADER:
            return "OP_EBADHEADER: A required header packet was not properly "
                   "formatted.";
        case OP_EVERSION:
            return "OP_EVERSION: The ID header contained an unrecognised "
                   "version number.";
        case OP_EBADPACKET:
            return "OP_EBADPACKET: An audio packet failed to decode properly.";
        case OP_EBADLINK:
            return "OP_EBADLINK: We failed to find data we had seen before or "
                   "the stream was sufficiently corrupt that seeking is "
                   "impossible.";
        case OP_ENOSEEK:
            return "OP_ENOSEEK: An operation that requires seeking was "
                   "requested on an unseekable stream.";
        case OP_EBADTIMESTAMP:
            return "OP_EBADTIMESTAMP: The first or last granule position of a "
                   "link failed basic validity checks.";
        default:
            return "Unknown error.";
    }
}

// Main audio decoding logic
// This function pulls and decodes audio samples from opusFile_ to fill waveBuf_
bool fillBuffer(OggOpusFile *opusFile_, ndspWaveBuf *waveBuf_) {
#ifdef DEBUG
    // Setup timer for performance stats
    TickCounter timer;
    osTickCounterStart(&timer);
#endif  // DEBUG

    // Decode samples until our waveBuf is full
    int totalSamples = 0;
    while (totalSamples < SAMPLES_PER_BUF) {
        int16_t *buffer = waveBuf_->data_pcm16 + (totalSamples * CHANNELS_PER_SAMPLE);
        const size_t bufferSize = (SAMPLES_PER_BUF - totalSamples) * CHANNELS_PER_SAMPLE;

        // Decode bufferSize samples from opusFile_ into buffer,
        // storing the number of samples that were decoded (or error)
        const int samples = op_read_stereo(opusFile_, buffer, bufferSize);
        if (samples <= 0) {
            if (samples == 0) break;  // No error here

            printf("op_read_stereo: error %d (%s)", samples, opusStrError(samples));
            break;
        }

        totalSamples += samples;
    }

    // If no samples were read in the last decode cycle, we're done
    if (totalSamples == 0) {
        printf("Playback complete, press Start to exit\n");
        return false;
    }

    // Pass samples to NDSP
    waveBuf_->nsamples = totalSamples;
    ndspChnWaveBufAdd(0, waveBuf_);
    DSP_FlushDataCache(waveBuf_->data_pcm16, totalSamples * CHANNELS_PER_SAMPLE * sizeof(int16_t));

#ifdef DEBUG
    PrintConsole *prev = consoleSelect(&bottomConsole);
    // Print timing info
    osTickCounterUpdate(&timer);
    printf("fillBuffer %lfms in %lfms\n", totalSamples * 1000.0 / SAMPLE_RATE,
           osTickCounterRead(&timer));
    consoleSelect(prev);
#endif  // DEBUG

    return true;
}

// Pause until user presses a button
void waitForInput(void) {
    printf("Press any button to exit...\n");
    while (aptMainLoop()) {
        gspWaitForVBlank();
        gfxSwapBuffers();
        hidScanInput();

        if (hidKeysDown()) break;
    }
}

// Audio initialisation code
// This sets up NDSP and our primary audio buffer
bool audioInit(void) {
    // Setup NDSP
    ndspChnReset(0);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, SAMPLE_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    // Allocate audio buffer
    const size_t bufferSize = WAVEBUF_SIZE * ARRAY_SIZE(s_waveBufs);
    s_audioBuffer = (int16_t *)linearAlloc(bufferSize);
    if (!s_audioBuffer) {
        printf("Failed to allocate audio buffer\n");
        return false;
    }

    // Setup waveBufs for NDSP
    memset(&s_waveBufs, 0, sizeof(s_waveBufs));
    int16_t *buffer = s_audioBuffer;

    for (size_t i = 0; i < ARRAY_SIZE(s_waveBufs); ++i) {
        s_waveBufs[i].data_vaddr = buffer;
        s_waveBufs[i].status = NDSP_WBUF_DONE;

        buffer += WAVEBUF_SIZE / sizeof(buffer[0]);
    }

    return true;
}

// Audio de-initialisation code
// Stops playback and frees the primary audio buffer
void audioExit(void) {
    ndspChnReset(0);
    linearFree(s_audioBuffer);
}
// SOURCE 3ds-examples/audio/opus-decoding END

void audioThread(void *arg) {
    while (run_threads) {
        // wait until a song is ready to play
        LightEvent_Wait(&opus_controller.startEvent);

        // failsafe if somehow event is signaled when it shouldn't be
        if (!opus_controller.songReady) {
            continue;
        }

        OggOpusFile *file = opus_controller.file;

        while (run_threads && !opus_controller.stopPlayback) {
            for (size_t i = 0; i < ARRAY_SIZE(s_waveBufs); ++i) {
                if (s_waveBufs[i].status != NDSP_WBUF_DONE) {
                    continue;
                }

                // fill the buffer with audio data
                if (!fillBuffer(file, &s_waveBufs[i])) {
                    opus_controller.songReady = false;  // song finished playing
                    // LightEvent_Signal(&opus_controller.doneEvent);
                    // get outside of while loop until next song is played
                    opus_controller.stopPlayback = true;
                    break;
                }
            }
            LightEvent_Wait(&opus_controller.fillBufferEvent);
        }
        // reset flags
        op_free(opus_controller.file);
        opus_controller.file = nullptr;
        opus_controller.songReady = false;
        opus_controller.stopPlayback = false;

        LightEvent_Signal(&opus_controller.doneEvent);  // signal that playback is done
    }
}

bool playSong(std::string path) {
    opus_controller.songPath = path;

    int error = 0;
    opus_controller.file = op_open_file(opus_controller.songPath.c_str(), &error);
    if (error || opus_controller.file == nullptr) {
        // TODO maybe have some sort of logging system, maybe log to file later?
        printf("Error opening file: %s\n", opusStrError(error));
        return false;
    }
    opus_controller.songReady = true;
    opus_controller.stopPlayback = false;

    // signal to start playing song
    LightEvent_Signal(&opus_controller.startEvent);

    return true;
}

void opusCallback(void *arg) {
    (void)arg;  // suppress unused parameter warning

    if (!run_threads) {
        return;
    }

    LightEvent_Signal(&opus_controller.fillBufferEvent);
}

static void sceneInit(void) {
    g_dynamicBuf = C2D_TextBufNew(4096);
}

static void sceneExit(void) {
    // Delete the text buffers
    C2D_TextBufDelete(g_dynamicBuf);
}

std::vector<dirent> get_files(const char *path) {
    std::vector<dirent> file_list;
    DIR *dir = opendir(path);
    if (dir == nullptr) {
        printf("Failed to open directory: %s\n", path);
        return file_list;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        file_list.push_back(*ent);
    }

    closedir(dir);
    return file_list;
}

void printFiles(std::vector<dirent> files, size_t selectedFile, size_t maxFiles = MAX_FILES,
                size_t lineOffset = 0) {
    size_t iter = 0;
    for (size_t i = selectedFile; i < std::min(files.size(), (size_t)MAX_FILES + selectedFile);
         i++) {
        char buf[160];
        C2D_Text dynText;

        std::string fileName = "";
        std::string prefix = "";
        std::string postfix = "";

        if (i == selectedFile) {
            prefix = "-> ";
        } else {
            prefix = "   ";
        }
        fileName = files[i].d_name;
        if (files[i].d_type == DT_DIR) {
            postfix = "/";
        }
        snprintf(buf, sizeof(buf), "%s%s%s", prefix.c_str(), fileName.c_str(), postfix.c_str());
        C2D_TextParse(&dynText, g_dynamicBuf, buf);
        C2D_TextOptimize(&dynText);
        const float BASE_Y_OFFSET = 8.0f;
        float y_offset = 16.0f * (iter + lineOffset) + BASE_Y_OFFSET;
        C2D_DrawText(&dynText, C2D_AlignLeft | C2D_WithColor, 10.0f, y_offset, 0.5f, 0.5f, 0.5f,
                     C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f));
        iter++;
    }
}

void stopPlaybackIfPlaying() {
    if (opus_controller.songReady) {
        // if song is already playing, stop playback
        opus_controller.stopPlayback = true;
        opus_controller.interrupted = true;
    }
}

void playNextThread(void *arg) {
    while (run_threads) {
        LightEvent_Wait(&opus_controller.doneEvent);
        if (opus_controller.interrupted) {
            logToBottomScreen("not autoplaying next because user interrupted playback");
            // user interrupted playback, so we don't play the next song
            opus_controller.interrupted = false;
            continue;
        }
        if (file_controller.playingFile < file_controller.files.size() - 1) {
            size_t nextSongIdx = file_controller.playingFile + 1;
            std::string nextSongPath =
                file_controller.cwd + file_controller.files[nextSongIdx].d_name;
            playSong(nextSongPath);
            file_controller.playingFile = nextSongIdx;
            logToBottomScreen(
                ("autoplaying: " + (std::string)file_controller.files[nextSongIdx].d_name).c_str());
        }
    }
}

int main(int argc, char *argv[]) {
    romfsInit();
    gfxInitDefault();

    // from 3ds-examples/graphics/printing/system-font/source/main.c START
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    // Create screen
    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

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
        if (kDown & KEY_START) {
            break;
        }

        // TODO test on 3ds how many files it takes to run out of memory (std::vector allocates on
        // heap). Based on that decide if storing all files in cwd at once is viable or if smth
        // different is needed std::vector<dirent> files = get_files(cwd.c_str());

        if (kDown || kHeld) {
            update_files = true;  // only update screen when a button is pressed
        }

        if (update_files) {
            file_controller.files = get_files(file_controller.cwd.c_str());
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
                file_controller.files = get_files(file_controller.cwd.c_str());
            } else if (file_type == DT_REG) {
                stopPlaybackIfPlaying();
                char *song_filename = file_controller.files[file_controller.selectedFile].d_name;
                PrintConsole *prev = consoleSelect(&bottomConsole);
                logToBottomScreen(("Playing file: " + (std::string)song_filename).c_str());
                consoleSelect(prev);
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
                    file_controller.files = get_files(file_controller.cwd.c_str());
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

#include "opus.h"

#include <3ds.h>

#include <cstring>

#include "filebrowser.h"
#include "gfx.h"

ndspWaveBuf s_waveBufs[3];
int16_t *s_audioBuffer = NULL;

OpusController opus_controller = {
    .songPath = "",
    .file = nullptr,
    .songReady = false,  // also can be used to check if song is playing
    .stopPlayback = false,
    .interrupted = false,  // don't autoplay next if user stopped song
    .startEvent = {0},
    .doneEvent = {0},
    .fillBufferEvent = {0}};

volatile bool run_threads = true;

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
            // TODO allow more verbose logging with a VERBOSE flag (user can set)
            // logToBottomScreen("not autoplaying next because user interrupted playback");
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

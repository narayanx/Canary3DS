#include "audio_engine.h"

#include <3ds.h>
#include <cstring>
#include <string>

#include "audio_decoder.h"
#include "filebrowser.h"
#include "gfx.h"
#include "image.h"


ndspWaveBuf  s_waveBufs[3];
int16_t*     s_audioBuffer = nullptr;

AudioController audioController = {
    .songPath             = "",
    .songArtist           = "",
    .decoder              = nullptr,
    .songReady            = false,
    .stopPlayback         = false,
    .interrupted          = false,
    .newSongStarted       = false,
    .seekPending          = false,
    .seekTargetSeconds    = 0.0,
    .songPositionSeconds  = 0.0,
    .songDurationSeconds  = -1.0,
    .startEvent           = {0},
    .doneEvent            = {0},
    .fillBufferEvent      = {0},
    .sampleRate           = 48000,
};

volatile bool runThreads = true;

bool audioInit() {
    ndspChnReset(0);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, 48000.0f);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    const size_t total = AUDIO_WAVEBUF_SIZE * AUDIO_ARRAY_SIZE(s_waveBufs);
    s_audioBuffer = (int16_t*)linearAlloc(total);
    if (!s_audioBuffer) {
        logToBottomScreen("Failed to allocate audio buffer");
        return false;
    }

    memset(&s_waveBufs, 0, sizeof(s_waveBufs));
    int16_t* ptr = s_audioBuffer;
    for (size_t i = 0; i < AUDIO_ARRAY_SIZE(s_waveBufs); ++i) {
        s_waveBufs[i].data_vaddr = ptr;
        s_waveBufs[i].status     = NDSP_WBUF_DONE;
        ptr += AUDIO_WAVEBUF_SIZE / sizeof(int16_t);
    }
    return true;
}

void audioExit() {
    ndspChnReset(0);
    linearFree(s_audioBuffer);
    s_audioBuffer = nullptr;
}

static bool fillBuffer(IAudioDecoder* decoder, ndspWaveBuf* waveBuf) {
    int total = 0;
    while (total < AUDIO_SAMPLES_PER_BUF) {
        int16_t* dst = waveBuf->data_pcm16 + total * AUDIO_CHANNELS;
        int      rem = AUDIO_SAMPLES_PER_BUF - total;
        int      got = decoder->decode(dst, rem);
        if (got <= 0) break;
        total += got;
    }
    if (total == 0) return false;

    waveBuf->nsamples = (u32)total;
    ndspChnWaveBufAdd(0, waveBuf);
    DSP_FlushDataCache(waveBuf->data_pcm16,
                      total * AUDIO_CHANNELS * sizeof(int16_t));
    return true;
}

// Seek helper: called from audio thread when seekPending is set.
static void handlePendingSeek(IAudioDecoder* decoder) {
    double target = audioController.seekTargetSeconds;

    // Stop the DSP channel and mark all wave bufs as available
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, (float)audioController.sampleRate);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    for (auto& wb : s_waveBufs) wb.status = NDSP_WBUF_DONE;

    decoder->seekTo(target);
    audioController.songPositionSeconds = decoder->getPositionSeconds();
    audioController.seekPending = false;

    // Prime the first buffer immediately so there's no audible gap
    for (size_t i = 0; i < AUDIO_ARRAY_SIZE(s_waveBufs); ++i) {
        if (!fillBuffer(decoder, &s_waveBufs[i])) break;
    }
    ndspChnSetPaused(0, false);
}

void audioThread(void* /*arg*/) {
    while (runThreads) {
        LightEvent_Wait(&audioController.startEvent);
        if (!audioController.songReady) continue;

        IAudioDecoder* dec = audioController.decoder;

        while (runThreads && !audioController.stopPlayback) {
            // Service a pending seek before filling more audio
            if (audioController.seekPending) {
                handlePendingSeek(dec);
                continue;
            }

            for (size_t i = 0; i < AUDIO_ARRAY_SIZE(s_waveBufs); ++i) {
                if (s_waveBufs[i].status != NDSP_WBUF_DONE) continue;
                if (!fillBuffer(dec, &s_waveBufs[i])) {
                    audioController.stopPlayback = true;
                    break;
                }
            }

            // Update playback position for the UI (main thread reads this)
            audioController.songPositionSeconds = dec->getPositionSeconds();

            LightEvent_Wait(&audioController.fillBufferEvent);
        }

        audioController.songReady = false;
        delete audioController.decoder;
        audioController.decoder           = nullptr;
        audioController.stopPlayback      = false;
        audioController.seekPending       = false;
        audioController.songPositionSeconds = 0.0;

        LightEvent_Signal(&audioController.doneEvent);
    }
}

// NDSP callback (signals audio thread that a wave buffer finished)
void audioCallback(void* /*arg*/) {
    if (runThreads) LightEvent_Signal(&audioController.fillBufferEvent);
}

// Song control API (called from main thread)
bool playSong(const std::string& path) {
    auto dec = createDecoder(path);
    if (!dec) {
        logToBottomScreen("Unsupported format: " + path);
        return false;
    }
    if (!dec->open(path)) {
        logToBottomScreen("Failed to open: " + path);
        return false;
    }

    audioController.songPath            = path;
    audioController.songArtist          = dec->getArtist();
    audioController.sampleRate          = dec->getSampleRate();
    audioController.songDurationSeconds = dec->getDurationSeconds();
    audioController.songPositionSeconds = 0.0;
    audioController.seekPending         = false;
    audioController.decoder             = dec.release();
    audioController.songReady           = true;
    audioController.stopPlayback        = false;
    audioController.newSongStarted      = true;

    ndspChnSetRate(0, (float)audioController.sampleRate);
    LightEvent_Signal(&audioController.startEvent);
    return true;
}

void stopPlaybackIfPlaying() {
    if (audioController.songReady) {
        audioController.stopPlayback = true;
        audioController.interrupted  = true;
    }
}

bool goToNextSong() {
    if (audioController.songReady) {
        audioController.stopPlayback = true;
        return true;
    }
    return false;
}

void enqueueSong(const std::string& path) {
    fileController.playQueue.push_back(path);
    logToBottomScreen("Queued: " + path);
}

bool playNextFromQueue() {
    if (fileController.playQueue.empty()) return false;
    std::string next = fileController.playQueue.front();
    fileController.playQueue.pop_front();
    if (playSong(next)) {
        logToBottomScreen("Playing from queue: " + next);
        return true;
    }
    return false;
}

void playNextThread(void* /*arg*/) {
    while (runThreads) {
        LightEvent_Wait(&audioController.doneEvent);

        if (audioController.interrupted) {
            audioController.interrupted = false;
            continue;
        }

        if (playNextFromQueue()) continue;

        if (fileController.playingFile < fileController.files.size() - 1) {
            size_t next = fileController.playingFile + 1;
            std::string path = fileController.cwd + fileController.files[next].d_name;
            if (playSong(path)) {
                fileController.playingFile = next;
                logToBottomScreen("Autoplaying: " + (std::string)fileController.files[next].d_name);
            }
        }
    }
}

bool loadCoverArtForCurrentSong(C2D_Image& image, C3D_Tex& tex,
                                 Tex3DS_SubTexture& subtex, bool& loadedImage) {
    if (!audioController.decoder) { loadedImage = false; return false; }
    bool ok = audioController.decoder->loadCoverArt(image, tex, subtex, loadedImage);
    loadedImage = ok;
    return ok;
}

void waitForInput() {
    logToBottomScreen("Press any button to exit...");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown()) break;
    }
}

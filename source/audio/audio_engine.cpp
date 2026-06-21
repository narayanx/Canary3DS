#include "audio_engine.h"

#include <3ds.h>

#include <cstring>
#include <string>

#include "audio_decoder.h"
#include "filebrowser.h"
#include "gfx.h"
#include "image.h"
#include "settings.h"

ndspWaveBuf s_waveBufs[3];
int16_t *s_audioBuffer = nullptr;

AudioController audioController = {
    .songPath = "",
    .songArtist = "",
    .decoder = nullptr,
    .decoderLock = 0,
    .starting = false,
    .songReady = false,
    .stopPlayback = false,
    .interrupted = false,
    .newSongStarted = false,
    .loopOne = false,
    .skipNextHistoryEntry = false,
    .seekPending = false,
    .seekTargetSeconds = 0.0,
    .seekRestorePaused = false,
    .pendingStartPaused = false,
    .applyPendingStartPaused = false,
    .songPositionSeconds = 0.0,
    .songDurationSeconds = -1.0,
    .startEvent = {0},
    .fillBufferEvent = {0},
};

volatile bool runThreads = true;

bool audioInit() {
    LightLock_Init(&audioController.decoderLock);

    ndspChnReset(0);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, 48000.0f);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    const size_t totalBytes = AUDIO_WAVEBUF_SIZE * AUDIO_ARRAY_SIZE(s_waveBufs);
    s_audioBuffer = static_cast<int16_t *>(linearAlloc(totalBytes));
    if (!s_audioBuffer) {
        logToDebugScreen("Failed to allocate audio buffer");
        return false;
    }

    memset(s_waveBufs, 0, sizeof(s_waveBufs));
    int16_t *ptr = s_audioBuffer;
    for (auto &wb : s_waveBufs) {
        wb.data_vaddr = ptr;
        wb.status = NDSP_WBUF_DONE;
        ptr += AUDIO_WAVEBUF_SIZE / sizeof(int16_t);
    }
    return true;
}

void audioExit() {
    ndspChnReset(0);
    // Guard against a decoder left alive by a race during shutdown.
    delete audioController.decoder;
    audioController.decoder = nullptr;
    linearFree(s_audioBuffer);
    s_audioBuffer = nullptr;
}

// Fill one wave buffer with decoded audio.
// Loops until the buffer is full or the decoder signals EOF/error.
// Returns false when the decoder is done and no samples were produced.
static bool fillBuffer(IAudioDecoder *decoder, ndspWaveBuf *wb) {
    int total = 0;
    while (total < AUDIO_SAMPLES_PER_BUF) {
        int16_t *dst = wb->data_pcm16 + total * AUDIO_CHANNELS;
        int rem = AUDIO_SAMPLES_PER_BUF - total;
        int got = decoder->decode(dst, rem);
        if (got <= 0) {
            break;
        }
        total += got;
    }
    if (total == 0) {
        return false;
    }

    wb->nsamples = static_cast<u32>(total);
    DSP_FlushDataCache(wb->data_pcm16, total * AUDIO_CHANNELS * sizeof(int16_t));
    ndspChnWaveBufAdd(0, wb);
    return true;
}

// (Re-)configure NDSP channel 0 and mark every wave buffer as available.
// Called at the start of each song and after a seek to guarantee clean state.
static void resetChannel(int sampleRate) {
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, static_cast<float>(sampleRate));
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    for (auto &wb : s_waveBufs) {
        wb.status = NDSP_WBUF_DONE;
    }
}

// Execute a pending seek: reset the channel, move the decoder, then prime all
// three wave buffers with post-seek audio so there's no audible gap on resume.
static void handlePendingSeek(IAudioDecoder *dec) {
    // Capture before resetChannel clears channel state
    const bool wasPaused = audioController.seekRestorePaused;

    resetChannel(dec->getSampleRate());
    if (wasPaused) {
        ndspChnSetPaused(0, true);  // pause before filling to avoid blip on resume
    }
    dec->seekTo(audioController.seekTargetSeconds);
    audioController.songPositionSeconds = dec->getPositionSeconds();
    audioController.seekPending = false;

    for (auto &wb : s_waveBufs) {
        if (!fillBuffer(dec, &wb)) {
            break;
        }
    }
    ndspChnSetPaused(0, wasPaused);
}

// Audio thread handles the entire playback lifecycle.
void audioThread(void *) {
    while (runThreads) {
        // Wait for a song
        // playSong() sets songReady = true then signals startEvent.
        LightEvent_Wait(&audioController.startEvent);
        if (!runThreads || !audioController.songReady) {
            continue;
        }

        // Capture the decoder into a local pointer.
        // All further access to the decoder goes through `dec`, never through
        // audioController.decoder, so a concurrent playSong() call on the
        // main thread cannot cause us to delete the wrong object.
        IAudioDecoder *dec = audioController.decoder;
        std::string finishedPath = audioController.songPath;

        resetChannel(dec->getSampleRate());
        if (audioController.applyPendingStartPaused) {
            ndspChnSetPaused(0, audioController.pendingStartPaused);
            audioController.applyPendingStartPaused = false;
        }

        // Fill loop
        // When NDSP finishes one it fires audioCallback → signals fillBufferEvent → we
        // wake up and refill that buffer. If no buffers are free yet we
        // simply go back to sleep until the next callback.
        while (runThreads && !audioController.stopPlayback) {
            // A pending seek must be handled before we try to fill anything
            // because it resets the channel and repositions the decoder.
            if (audioController.seekPending) {
                handlePendingSeek(dec);
                continue;
            }

            for (auto &wb : s_waveBufs) {
                if (wb.status != NDSP_WBUF_DONE) {
                    continue;
                }
                if (!fillBuffer(dec, &wb)) {
                    // Song finished. Let the remaining submitted buffers
                    // play out, the loop exits naturally on the next iteration.
                    audioController.stopPlayback = true;
                    break;
                }
            }

            audioController.songPositionSeconds = dec->getPositionSeconds();

            // Sleep until NDSP frees a buffer, or until waked early by signalling fillBufferEvent.
            if (!audioController.stopPlayback) {
                LightEvent_Wait(&audioController.fillBufferEvent);
            }
        }

        // Teardown
        // Stop the DSP channel immediately. Any buffers that are still
        // queued are abandoned, we mark them all as done so the next song
        // starts from a clean slate.
        ndspChnReset(0);
        for (auto &wb : s_waveBufs) {
            wb.status = NDSP_WBUF_DONE;
        }

        audioController.stopPlayback = false;
        audioController.seekPending = false;
        audioController.songPositionSeconds = 0.0;

        // For going back in history navigation "slide" through history
        if (!finishedPath.empty() && !audioController.skipNextHistoryEntry) {
            fileController.playHistory.push_front(finishedPath);
            while (fileController.playHistory.size() > (size_t) g_settings.historySize) {
                fileController.playHistory.pop_back();
            }
        }
        audioController.skipNextHistoryEntry = false;

        LightLock_Lock(&audioController.decoderLock);
        if (audioController.decoder == dec) {
            audioController.decoder = nullptr;
            audioController.songReady = false;
        }
        LightLock_Unlock(&audioController.decoderLock);
        delete dec;
        dec = nullptr;

        if (!runThreads) {
            break;
        }

        // Autoplay
        // interrupted is set by stopPlaybackIfPlaying() (user wants to pause song).
        // In that case we go back to idle, the main thread is responsible for initiating
        // playback of a song.
        if (audioController.interrupted) {
            audioController.interrupted = false;
            continue;
        }

        if (audioController.loopOne && !finishedPath.empty()) {
            playSong(finishedPath);
            continue;
        }

        // Prioritize queue first
        if (!playNextFromQueue()) {
            size_t next = fileController.playingFile + 1;
            while (next < fileController.playingFiles.size()) {
                if (fileController.playingFiles[next].d_type == DT_REG &&
                    isSupportedAudioFile(fileController.playingCwd +
                                         fileController.playingFiles[next].d_name)) {
                    break;
                }
                ++next;
            }
            if (next < fileController.playingFiles.size()) {
                const std::string path =
                    fileController.playingCwd + fileController.playingFiles[next].d_name;
                if (playSong(path)) {
                    fileController.playingFile = next;
                    logToDebugScreen("Autoplaying: " +
                                     std::string(fileController.playingFiles[next].d_name));
                }
            } else if (g_settings.repeat == RepeatMode::ALL &&
                       !fileController.playingFiles.empty()) {
                // Wrap around to the first audio file in the directory
                for (size_t i = 0; i < fileController.playingFiles.size(); ++i) {
                    if (fileController.playingFiles[i].d_type == DT_REG) {
                        const std::string path =
                            fileController.playingCwd + fileController.playingFiles[i].d_name;
                        if (isSupportedAudioFile(path) && playSong(path)) {
                            fileController.playingFile = i;
                            logToDebugScreen("Repeat All: " +
                                             std::string(fileController.playingFiles[i].d_name));
                            break;
                        }
                    }
                }
            }
        }
    }
}

// Called by NDSP (from an interrupt context) each time a wave buffer finishes.
// We only signal, the audio thread does all the actual work.
void audioCallback(void *) {
    if (runThreads) {
        LightEvent_Signal(&audioController.fillBufferEvent);
    }
}

// Song-control API (called from the main thread)
// Open a new decoder for `path` and hand it to the audio thread.
// Can also be called from within audioThread (during autoplay), in that case
// the LightEvent_Signal is consumed immediately on the next iteration of the
// thread's outer loop.
bool playSong(const std::string &path) {
    LightLock_Lock(&audioController.decoderLock);
    if (audioController.starting) {
        LightLock_Unlock(&audioController.decoderLock);
        return false;
    }
    audioController.starting = true;
    LightLock_Unlock(&audioController.decoderLock);

    auto dec = createDecoder(path);
    if (!dec) {
        logToDebugScreen("Unsupported format: " + path);
        audioController.starting = false;
        return false;
    }
    if (!dec->open(path)) {
        logToDebugScreen("Failed to open: " + path);
        audioController.starting = false;
        return false;
    }

    // Write metadata fields before handing the decoder to the audio thread.
    audioController.songPath = path;
    audioController.songArtist = dec->getArtist();
    audioController.songDurationSeconds = dec->getDurationSeconds();
    audioController.songPositionSeconds = 0.0;
    audioController.seekPending = false;
    audioController.stopPlayback = false;
    LightLock_Lock(&audioController.decoderLock);
    audioController.decoder = dec.release();
    audioController.songReady = true;
    audioController.starting = false;
    LightLock_Unlock(&audioController.decoderLock);
    audioController.newSongStarted = true;

    LightEvent_Signal(&audioController.startEvent);
    return true;
}

// Stop the current song and suppress autoplay (user explicitly stopped).
// Signals fillBufferEvent so the audio thread wakes immediately rather than
// waiting up to 360 ms for the next NDSP callback.
void stopPlaybackIfPlaying() {
    if (!audioController.songReady) {
        return;
    }
    audioController.interrupted = true;
    audioController.stopPlayback = true;
    LightEvent_Signal(&audioController.fillBufferEvent);
}

// Returns true if there is another song to play (the queue is non empty, autoplay can advance)
static bool hasNextSong() {
    if (!fileController.playQueue.empty()) {
        return true;
    }
    if (!audioController.songReady) {
        return false;
    }
    for (size_t i = fileController.playingFile + 1; i < fileController.playingFiles.size(); ++i) {
        if (fileController.playingFiles[i].d_type == DT_REG &&
            isSupportedAudioFile(fileController.playingCwd +
                                 fileController.playingFiles[i].d_name)) {
            return true;
        }
    }
    if (g_settings.repeat == RepeatMode::ALL) {
        for (const auto &f : fileController.playingFiles) {
            if (f.d_type == DT_REG && isSupportedAudioFile(fileController.playingCwd + f.d_name)) {
                return true;
            }
        }
    }
    return false;
}

// Stop the current song and let autoplay advance to the next one.
// Does not set interrupted, so the audio thread will try the queue / next file.
bool goToNextSong() {
    if (!audioController.songReady) {
        return playNextFromQueue();
    }
    if (!hasNextSong()) {
        return false;
    }
    audioController.pendingStartPaused = ndspChnIsPaused(0);
    audioController.applyPendingStartPaused = true;
    audioController.stopPlayback = true;
    LightEvent_Signal(&audioController.fillBufferEvent);
    return true;
}

void enqueueSong(const std::string &path) {
    if (fileController.playQueue.size() >= (size_t) g_settings.queueSize) {
        logToDebugScreen("Queue full (" + std::to_string(g_settings.queueSize) +
                         "), skipping: " + path);
        return;
    }
    fileController.playQueue.push_back(path);
    logToDebugScreen("Queued: " + path);
}

// Pop the front of the play queue and start it. Returns false if queue empty
// or the song fails to open (the failed entry is discarded in that case).
bool playNextFromQueue() {
    while (!fileController.playQueue.empty()) {
        const std::string next = fileController.playQueue.front();
        fileController.playQueue.pop_front();
        if (playSong(next)) {
            logToDebugScreen("Playing from queue: " + next);
            return true;
        }
    }
    return false;
}

// Called from the main thread after audioController.newSongStarted fires.
// The decoder is guaranteed to be alive: songReady was just set, and the audio
// thread only deletes the decoder after songReady becomes false.
bool loadCoverArtForCurrentSong(C2D_Image &image,
                                C3D_Tex &tex,
                                Tex3DS_SubTexture &subtex,
                                bool &loadedImage) {
    LightLock_Lock(&audioController.decoderLock);
    IAudioDecoder *dec = audioController.decoder;
    if (!dec) {
        LightLock_Unlock(&audioController.decoderLock);
        loadedImage = false;
        return false;
    }
    const bool ok = dec->loadCoverArt(image, tex, subtex, loadedImage);
    LightLock_Unlock(&audioController.decoderLock);
    loadedImage = ok;
    return ok;
}

bool loadCoverArtFromFile(const std::string &songPath,
                          C2D_Image &image,
                          C3D_Tex &tex,
                          Tex3DS_SubTexture &subtex,
                          bool freeExisting) {
    auto dec = createDecoder(songPath);
    if (!dec) {
        return false;
    }
    if (!dec->open(songPath)) {
        return false;
    }
    bool ok = dec->loadCoverArt(image, tex, subtex, freeExisting);
    dec->close();
    return ok;
}

void waitForInput() {
    logToDebugScreen("Press any button to exit...");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown()) {
            break;
        }
    }
}

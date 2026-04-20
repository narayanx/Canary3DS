#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <string>

#include "audio_decoder.h"

#define AUDIO_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

inline constexpr int    AUDIO_SAMPLES_PER_BUF = 48000 * 120 / 1000;  // 5 760 stereo frames
inline constexpr int    AUDIO_CHANNELS        = 2;
inline constexpr size_t AUDIO_WAVEBUF_SIZE    =
    AUDIO_SAMPLES_PER_BUF * AUDIO_CHANNELS * sizeof(int16_t);

inline constexpr int AUDIO_THREAD_AFFINITY = -1;
inline constexpr int AUDIO_THREAD_STACK_SZ = 32 * 1024;

struct AudioController {
    // Metadata (written by main thread in playSong, read by main thread for UI)
    std::string    songPath;
    std::string    songArtist;
    std::string    songTrackNumber;

    // Decoder (owned exclusively by audioThread during playback)
    // Main thread only touches this in loadCoverArtForCurrentSong() and audioExit(),
    // both of which are safe because songReady is false at those call sites.
    IAudioDecoder* decoder;

    // Playback-control flags (written by main thread, read by audio thread)
    volatile bool  songReady;       // true while a decoder is loaded and filling buffers
    volatile bool  stopPlayback;    // audio thread exits the fill loop when this is set
    volatile bool  interrupted;     // suppresses autoplay; set when user explicitly stops
    volatile bool  newSongStarted;  // pulsed once per song; main thread resets to false

    // Seek request (main thread writes, audio thread consumes)
    volatile bool   seekPending;
    volatile double seekTargetSeconds;

    // Position/duration (audio thread writes, main thread reads for UI)
    volatile double songPositionSeconds;
    volatile double songDurationSeconds;  // -1.0 if unknown

    // Synchronisation events
    LightEvent startEvent;       // playSong() signals: audioThread wakes and begins filling
    LightEvent fillBufferEvent;  // NDSP callback signals: audioThread checks for free wave buffers, also signalled for immediate wakeup
};

extern AudioController audioController;
extern ndspWaveBuf     s_waveBufs[3];
extern int16_t*        s_audioBuffer;
extern volatile bool   runThreads;

bool audioInit();
void audioExit();

// The single audio thread. Handles filling, seeking, and autoplay.
void audioThread(void* arg);

// NDSP completion callback, signals fillBufferEvent.
void audioCallback(void* arg);

// Song-control API (called from main thread)
bool playSong(const std::string& path);
void stopPlaybackIfPlaying();   // stops current song, suppresses autoplay
bool goToNextSong();            // stops current song, allows autoplay to advance
void enqueueSong(const std::string& path);
bool playNextFromQueue();

bool loadCoverArtForCurrentSong(C2D_Image& image, C3D_Tex& tex,
                                Tex3DS_SubTexture& subtex, bool& loadedImage);
void waitForInput();

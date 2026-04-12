#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <string>

#include "audio_decoder.h"

#define AUDIO_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

inline constexpr int    AUDIO_SAMPLES_PER_BUF = 48000 * 120 / 1000;   // 5 760 stereo frames
inline constexpr int    AUDIO_CHANNELS        = 2;
inline constexpr size_t AUDIO_WAVEBUF_SIZE    =
    AUDIO_SAMPLES_PER_BUF * AUDIO_CHANNELS * sizeof(int16_t);

inline constexpr int AUDIO_THREAD_AFFINITY = -1;
inline constexpr int AUDIO_THREAD_STACK_SZ = 32 * 1024;

struct AudioController {
    std::string     songPath;
    std::string     songArtist;       // populated from tag metadata on song start
    IAudioDecoder*  decoder;          // owned by audio thread during playback

    volatile bool   songReady;        // true while a song is loaded & playing
    volatile bool   stopPlayback;
    volatile bool   interrupted;      // user-stopped (suppress autoplay)
    volatile bool   newSongStarted;   // pulsed when a new song begins

    // Seek request (written by main thread, consumed by audio thread)
    volatile bool   seekPending;
    volatile double seekTargetSeconds;

    // Position / duration (written by audio thread, read by main thread for UI)
    volatile double songPositionSeconds;
    volatile double songDurationSeconds;   // -1 if unknown

    LightEvent      startEvent;
    LightEvent      doneEvent;
    LightEvent      fillBufferEvent;

    int             sampleRate;
};

extern AudioController  audioController;
extern ndspWaveBuf      s_waveBufs[3];
extern int16_t*         s_audioBuffer;
extern volatile bool    runThreads;

bool audioInit();
void audioExit();

void audioThread(void* arg);
void playNextThread(void* arg);
void audioCallback(void* arg);

bool playSong(const std::string& path);
void stopPlaybackIfPlaying();
bool goToNextSong();
void enqueueSong(const std::string& path);
bool playNextFromQueue();

bool loadCoverArtForCurrentSong(C2D_Image& image, C3D_Tex& tex,
                                 Tex3DS_SubTexture& subtex, bool& loadedImage);
void waitForInput();
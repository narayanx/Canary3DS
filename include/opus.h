#ifndef CANARY_OPUS_H
#define CANARY_OPUS_H

#include <3ds.h>
#include <opusfile.h>

#include <string>

// SOURCE 3ds-examples/audio/opus-decoding START
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
inline constexpr int SAMPLE_RATE = 48000;                         // Opus is fixed at 48kHz
inline constexpr int SAMPLES_PER_BUF = SAMPLE_RATE * 120 / 1000;  // 120ms buffer
inline constexpr int CHANNELS_PER_SAMPLE = 2;                     // We ask libopusfile for
                                                                  // stereo output; it will down
                                                                  // -mix for us as necessary.

inline constexpr int THREAD_AFFINITY = -1;         // Execute thread on any core
inline constexpr int THREAD_STACK_SZ = 32 * 1024;  // 32kB stack for audio thread

inline constexpr size_t WAVEBUF_SIZE =
    SAMPLES_PER_BUF * CHANNELS_PER_SAMPLE * sizeof(int16_t);  // Size of NDSP wavebufs
// SOURCE 3ds-examples/audio/opus-decoding END

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

struct OpusTagData {
    u32 pictureType;
    u32 mediaStringByteLen;
    std::string mediaType = "";
    u32 pictureDescriptionByteLen;
    std::string pictureDescription = "";
    u32 pictureWidth;
    u32 pictureHeight;
    u32 colorDepthBits;
    u32 numColorsUsed;

    u32 pictureDataByteLen;
    size_t pictureByteOffset;  // not technically in the metadata, for convenience

    std::string coverArtDisplay;  // the raw image data (base64 encoded)
};

extern OpusController opusController;
extern ndspWaveBuf s_waveBufs[3];
extern int16_t *s_audioBuffer;

extern volatile bool runThreads;

const char *opusStrError(int);

bool fillBuffer(OggOpusFile *, ndspWaveBuf);

void waitForInput(void);

bool audioInit(void);

void audioExit(void);

void audioThread(void *arg);

bool playSong(std::string path);

void opusCallback(void *arg);

void stopPlaybackIfPlaying();

void playNextThread(void *arg);

const OpusTags *getMetadata(OpusController &controller);

const char *getCoverMetadataBase64(OpusController &controller, size_t &outSize);

OpusTagData parseMetadata(std::string coverArtMetadata);

#endif

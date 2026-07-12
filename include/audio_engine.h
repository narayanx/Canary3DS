#pragma once

#include <3ds.h>
#include <citro2d.h>

#include <random>
#include <string>

#include "audio_decoder.h"
#include "audio_dsp.h"

#define AUDIO_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

inline constexpr int AUDIO_SAMPLES_PER_BUF = 48000 * 120 / 1000;  // 5 760 stereo frames
inline constexpr int AUDIO_CHANNELS = 2;
inline constexpr size_t AUDIO_WAVEBUF_SIZE =
    AUDIO_SAMPLES_PER_BUF * AUDIO_CHANNELS * sizeof(int16_t);

inline constexpr int AUDIO_THREAD_AFFINITY = -1;
inline constexpr int AUDIO_THREAD_STACK_SZ = 32 * 1024;

struct AudioController {
    // Metadata (written by main thread in playSong, read by main thread for UI)
    std::string songPath;
    std::string songArtist;
    std::string songTrackNumber;

    // Decoder (owned exclusively by audioThread during playback)
    // Main thread touches this in loadCoverArtForCurrentSong() and audioExit().
    IAudioDecoder *decoder;
    LightLock decoderLock;   // guards against concurrent delete/replace during a song transition
    volatile bool starting;  // true while opening song file (blocks a second concurrent transition)

    // Speed/pitch DSP, sits between the decoder and the NDSP wave buffers.
    // Owned by the audio thread; settings changes are applied via
    // applySpeedPitch() from either thread (same relaxed-consistency
    // convention as the rest of this struct's settings-derived state).
    SpeedPitchProcessor speedPitch;

    // Playback-control flags (written by main thread, read by audio thread)
    volatile bool songReady;       // true while a decoder is loaded and filling buffers
    volatile bool stopPlayback;    // audio thread exits the fill loop when this is set
    volatile bool interrupted;     // suppresses autoplay; set when user explicitly stops
    volatile bool newSongStarted;  // pulsed once per song; main thread resets to false
    volatile bool loopOne;         // replay the current song when it finishes

    // Set by the main thread before stopping playback during history navigation,
    // the audio thread resets it to false after each stop.
    volatile bool skipNextHistoryEntry;

    // Seek request (main thread writes, audio thread consumes)
    volatile bool seekPending;
    volatile double seekTargetSeconds;
    volatile bool seekRestorePaused;

    // Preserving play/pause state for going to previous or next song
    volatile bool pendingStartPaused;
    volatile bool applyPendingStartPaused;

    // Position/duration (audio thread writes, main thread reads for UI)
    volatile double songPositionSeconds;
    volatile double songDurationSeconds;  // -1.0 if unknown

    // Synchronisation events
    LightEvent startEvent;       // playSong() signals: audioThread wakes and begins filling
    LightEvent fillBufferEvent;  // NDSP callback signals: audioThread checks for free wave buffers,
                                 // also signalled for immediate wakeup
};

extern AudioController audioController;
extern ndspWaveBuf s_waveBufs[3];
extern int16_t *s_audioBuffer;
extern volatile bool runThreads;

bool audioInit();
void audioExit();

// The single audio thread. Handles filling, seeking, and autoplay.
void audioThread(void *arg);

// NDSP completion callback, signals fillBufferEvent.
void audioCallback(void *arg);

// Song-control API (called from main thread)
bool playSong(const std::string &path);
void stopPlaybackIfPlaying();  // stops current song, suppresses autoplay
bool goToNextSong();           // stops current song, allows autoplay to advance
void enqueueSong(const std::string &path);
// Insert a song at the front of the queue, to play next.
void queuePlayNext(const std::string &path);
bool playNextFromQueue();
// Try the rest of the queue, then autoplay (shuffled or sequential),
// stopping at the first song that opens successfully. Shared by the natural
// end of song advance and by picking a queue/autoplay entry directly that
// turns out to fail to open, so a bad file never leaves playback stuck.
bool advanceQueueOrAutoplay();

// Remove the item at playback order index from both queue and playback order list.
void removeQueueItem(size_t idx);
// Move the playback order item at `from` to `to`. Also applied to playQueue
// when shuffle is off, since playbackOrder must mirror it in that state.
void reorderQueueItem(size_t from, size_t to);
// Discard the first `count` items of the playback order, keeping playQueue in sync.
void skipQueueItems(size_t count);
// Empty both playQueue and playbackOrder.
void clearQueue();

// Returns an mt19937 seeded from the system tick counter. std::random_device
// is not a reliable entropy source (it can return the same value on
// every call), so all one off shuffles should seed through this instead.
std::mt19937 makeShuffleRng();

// Toggle shuffle mode: turning it on generates a freshly shuffled playback
// order for the queue and builds a shuffled autoplay order for the current
// folder; turning it off restores the playback order to the queue's order.
void toggleShuffle();

// Rebuild shuffled autoplay vector from the eligible audio files in
// playingFiles. If wholeFolder is false, only files after playingFile are
// included; if true, every eligible file in the folder is included (used
// when the shuffled order runs out and the folder loops).
void reshuffleAutoplay(bool wholeFolder);

bool loadCoverArtForCurrentSong(C2D_Image &image,
                                C3D_Tex &tex,
                                Tex3DS_SubTexture &subtex,
                                bool &loadedImage);

// Load cover art from an arbitrary song file (main thread, not during active playback).
bool loadCoverArtFromFile(const std::string &songPath,
                          C2D_Image &image,
                          C3D_Tex &tex,
                          Tex3DS_SubTexture &subtex,
                          bool freeExisting);

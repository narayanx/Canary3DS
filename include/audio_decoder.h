#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <memory>
#include <string>

/**
 * Abstract decoder interface.
 *
 * All implementations output signed 16-bit stereo PCM at the file's native
 * sample rate (getSampleRate()).  Mono files are up-mixed to stereo
 * internally – the audio engine never needs to care about channel count.
 *
 * Thread safety contract:
 *   open(), decode(), seekTo(), getPositionSeconds(), close() are called
 *   exclusively from the audio thread.
 *   Functions to get song metadata are called from the main thread after
 *   open() returns – implementations must cache all metadata synchronously
 *   inside open().
 *   getDurationSeconds() / getSampleRate() are read-only after open() and
 *   may be called from any thread.
 */
class IAudioDecoder {
public:
    virtual ~IAudioDecoder() = default;

    // Open the file and cache any metadata (cover art, tags, duration).
    virtual bool open(const std::string& path) = 0;

    // Decode up to maxFrames interleaved stereo int16 pairs into buffer.
    // Returns frames decoded (>0), 0 on EOF, or <0 on error.
    virtual int decode(int16_t* buffer, int maxFrames) = 0;

    // Sample rate in Hz.
    virtual int getSampleRate() const = 0;

    // Total duration in seconds; -1.0 if unknown.
    virtual double getDurationSeconds() const = 0;

    // Current decode position in seconds (audio thread only).
    virtual double getPositionSeconds() const = 0;

    // Seek to targetSeconds; best-effort (audio thread only).
    virtual void seekTo(double targetSeconds) = 0;

    // Release all resources.
    virtual void close() = 0;

    // Upload embedded cover art (main thread, after open()).
    // freeExisting: call C3D_TexDelete on tex before re-initialising.
    virtual bool loadCoverArt(C2D_Image& image, C3D_Tex& tex,
                               Tex3DS_SubTexture& subtex, bool freeExisting) {
        (void)image; (void)tex; (void)subtex; (void)freeExisting;
        return false;
    }

    // Artist tag string (main thread, after open()); empty if unavailable.
    virtual std::string getArtist() const { return ""; }

    // Track number string (main thread, after open()); empty if unavailable.
    virtual std::string getTrackNumber() const { return ""; }

    virtual bool isOpen() const = 0;
};

std::unique_ptr<IAudioDecoder> createDecoder(const std::string& path);
bool isSupportedAudioFile(const std::string& filename);

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include "audio_decoder.h"
#include "gfx.h"
#include <string>


class WavDecoder final : public IAudioDecoder {
public:
    WavDecoder()  = default;
    ~WavDecoder() override { close(); }

    bool open(const std::string& path) override {
        if (!drwav_init_file(&wav_, path.c_str(), nullptr)) {
            logToDebugScreen("dr_wav: failed to open " + path);
            return false;
        }
        open_       = true;
        sampleRate_ = (int)wav_.sampleRate;
        channels_   = (int)wav_.channels;
        duration_   = (wav_.sampleRate > 0 && wav_.totalPCMFrameCount > 0)
                      ? (double)wav_.totalPCMFrameCount / (double)wav_.sampleRate
                      : -1.0;
        framesDecoded_ = 0;
        return true;
    }

    int decode(int16_t* buffer, int maxFrames) override {
        if (!open_) return -1;

        if (channels_ == 2) {
            drwav_uint64 got = drwav_read_pcm_frames_s16(&wav_, maxFrames, buffer);
            framesDecoded_ += got;
            return (int)got;
        }
        // Mono → stereo
        int clamp = (maxFrames <= MONO_BUF) ? maxFrames : MONO_BUF;
        drwav_uint64 got = drwav_read_pcm_frames_s16(&wav_, (drwav_uint64)clamp, monoTmp_);
        for (int i = (int)got - 1; i >= 0; --i) {
            buffer[2 * i]     = monoTmp_[i];
            buffer[2 * i + 1] = monoTmp_[i];
        }
        framesDecoded_ += got;
        return (int)got;
    }

    int    getSampleRate()      const override { return sampleRate_; }
    double getDurationSeconds() const override { return duration_; }
    bool   isOpen()             const override { return open_; }

    double getPositionSeconds() const override {
        return (sampleRate_ > 0) ? (double)framesDecoded_ / (double)sampleRate_ : 0.0;
    }

    void seekTo(double seconds) override {
        if (!open_ || sampleRate_ <= 0) return;
        drwav_uint64 target = (drwav_uint64)(seconds * sampleRate_);
        drwav_seek_to_pcm_frame(&wav_, target);
        framesDecoded_ = target;
    }

    void close() override {
        if (open_) { drwav_uninit(&wav_); open_ = false; }
    }

    bool loadCoverArt(C2D_Image&, C3D_Tex&, Tex3DS_SubTexture&, bool) override {
        return false; // WAV has no standardised cover art
    }

private:
    drwav          wav_{};
    bool           open_          = false;
    int            sampleRate_    = 44100;
    int            channels_      = 2;
    double         duration_      = -1.0;
    drwav_uint64   framesDecoded_ = 0;

    static constexpr int MONO_BUF = 48000 * 120 / 1000;
    int16_t monoTmp_[MONO_BUF];
};

std::unique_ptr<IAudioDecoder> makeWavDecoder() {
    return std::make_unique<WavDecoder>();
}

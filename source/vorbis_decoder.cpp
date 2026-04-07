#include "audio_decoder.h"
#include "image.h"
#include "gfx.h"
#include "base64.h"

// 3ds-libvorbisidec (Tremor) – integer-only Vorbis decoder
#include <tremor/ivorbisfile.h>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// VorbisDecoder – wraps Tremor / libvorbisidec (3ds-libvorbisidec)
//
// Tremor differences from libvorbisfile:
//   - ov_read() has no endian/word/sign params; always outputs LE s16.
//   - ov_time_tell / ov_time_total return ogg_int64_t in milliseconds.
//   - ov_time_seek takes ogg_int64_t milliseconds.
// ---------------------------------------------------------------------------

class VorbisDecoder final : public IAudioDecoder {
public:
    VorbisDecoder()  = default;
    ~VorbisDecoder() override { close(); }

    bool open(const std::string& path) override {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) {
            return false;
        }

        if (ov_open(f, &vf_, nullptr, 0) < 0) {
            fclose(f);
            logToBottomScreen("Tremor: failed to open " + path);
            return false;
        }
       
        open_ = true;

        vorbis_info* vi = ov_info(&vf_, -1);
        sampleRate_ = vi ? (int)vi->rate     : 44100;
        channels_   = vi ? (int)vi->channels : 2;

        ogg_int64_t totalMs = ov_time_total(&vf_, -1);
        duration_ = (totalMs > 0) ? (double)totalMs / 1000.0 : -1.0;

        cacheCoverArt();
        return true;
    }

    int decode(int16_t* buffer, int maxFrames) override {
        if (!open_) return -1;

        int     section = 0;
        int     total   = 0;

        while (total < maxFrames) {
            int bytesWanted = (maxFrames - total) * channels_ * (int)sizeof(int16_t);
            char* dst = reinterpret_cast<char*>(buffer) +
                        total * channels_ * sizeof(int16_t);

            long got = ov_read(&vf_, dst, bytesWanted, &section);
            if (got == 0) break;        // EOF
            if (got < 0) return -1;     // error

            total += (int)got / (channels_ * (int)sizeof(int16_t));
        }

        if (total == 0) return 0;

        // Mono → stereo up-mix in-place (back-to-front to avoid overwrite)
        if (channels_ == 1) {
            for (int i = total - 1; i >= 0; --i) {
                buffer[2 * i]     = buffer[i];
                buffer[2 * i + 1] = buffer[i];
            }
        }
        return total;
    }

    int    getSampleRate()      const override { return sampleRate_; }
    double getDurationSeconds() const override { return duration_; }
    bool   isOpen()             const override { return open_; }

    double getPositionSeconds() const override {
        if (!open_) return 0.0;
        ogg_int64_t ms = ov_time_tell(const_cast<OggVorbis_File*>(&vf_));
        return (ms >= 0) ? (double)ms / 1000.0 : 0.0;
    }

    void seekTo(double seconds) override {
        if (!open_) return;
        ogg_int64_t ms = (ogg_int64_t)(seconds * 1000.0);
        ov_time_seek(&vf_, ms);
    }

    void close() override {
        if (open_) { ov_clear(&vf_); open_ = false; }
    }

    bool loadCoverArt(C2D_Image& image, C3D_Tex& tex,
                       Tex3DS_SubTexture& subtex, bool freeExisting) override {
        if (coverArtBytes_.empty()) return false;
        return loadCoverArtFromBytes(
            reinterpret_cast<const unsigned char*>(coverArtBytes_.data()),
            (int)coverArtBytes_.size(), image, tex, subtex, freeExisting);
    }

private:
    OggVorbis_File vf_{};
    bool           open_       = false;
    int            sampleRate_ = 44100;
    int            channels_   = 2;
    double         duration_   = -1.0;
    std::string    coverArtBytes_;

    static constexpr int TAG_PFX = 23; // len("METADATA_BLOCK_PICTURE=")

    void cacheCoverArt() {
        vorbis_comment* vc = ov_comment(&vf_, -1);
        if (!vc) return;
        for (int i = 0; i < vc->comments; ++i) {
            if (strncasecmp(vc->user_comments[i],
                            "METADATA_BLOCK_PICTURE=", TAG_PFX) != 0) continue;
            const char* b64    = vc->user_comments[i] + TAG_PFX;
            size_t      b64Len = (size_t)vc->comment_lengths[i] - TAG_PFX;
            std::string decoded = base64_decode(std::string(b64, b64Len));
            coverArtBytes_ = extractImageFromPictureBlock(decoded);
            return;
        }
    }
};

std::unique_ptr<IAudioDecoder> makeVorbisDecoder() {
    return std::make_unique<VorbisDecoder>();
}

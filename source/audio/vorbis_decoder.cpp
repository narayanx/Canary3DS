#include "audio_decoder.h"
#include "image.h"
#include "gfx.h"
#include "base64.h"

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

        cacheTags();
        return true;
    }

    int decode(int16_t* buffer, int maxFrames) override {
        if (!open_) return -1;

        int section = 0;
        int total   = 0;

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

    std::string getArtist() const override { return artist_; }
    std::string getTrackNumber() const override { return trackNumber_; }

private:
    OggVorbis_File vf_{};
    bool           open_       = false;
    int            sampleRate_ = 44100;
    int            channels_   = 2;
    double         duration_   = -1.0;
    std::string    coverArtBytes_;
    std::string    artist_;
    std::string    trackNumber_;

    static constexpr int PIC_PFX   = 23; // strlen("METADATA_BLOCK_PICTURE=")
    static constexpr int ART_PFX   = 7;  // strlen("ARTIST=")
    static constexpr int TRACK_PFX = 12; // strlen("TRACKNUMBER=")

    void cacheTags() {
        vorbis_comment* vc = ov_comment(&vf_, -1);
        if (!vc) return;
        for (int i = 0; i < vc->comments; ++i) {
            const char* c   = vc->user_comments[i];
            int         len = vc->comment_lengths[i];
            if (strncasecmp(c, "METADATA_BLOCK_PICTURE=", PIC_PFX) == 0) {
                if (coverArtBytes_.empty()) {
                    std::string decoded = base64_decode(std::string(c + PIC_PFX, len - PIC_PFX));
                    coverArtBytes_ = extractImageFromPictureBlock(decoded);
                }
            } else if (strncasecmp(c, "ARTIST=", ART_PFX) == 0) {
                if (artist_.empty())
                    artist_ = std::string(c + ART_PFX, len - ART_PFX);
            } else if (strncasecmp(c, "TRACKNUMBER=", TRACK_PFX) == 0) {
                if (trackNumber_.empty())
                    trackNumber_ = std::string(c + TRACK_PFX, len - TRACK_PFX);
            }
        }
    }
};

std::unique_ptr<IAudioDecoder> makeVorbisDecoder() {
    return std::make_unique<VorbisDecoder>();
}

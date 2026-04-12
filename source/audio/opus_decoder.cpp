#include "audio_decoder.h"
#include "image.h"
#include "gfx.h"
#include "base64.h"

#include <opusfile.h>
#include <cstring>
#include <string>

class OpusDecoder final : public IAudioDecoder {
public:
    ~OpusDecoder() override { close(); }

    bool open(const std::string& path) override {
        int err = 0;
        file_ = op_open_file(path.c_str(), &err);
        if (err || !file_) {
            logToBottomScreen("Opus open error: " + std::to_string(err));
            return false;
        }
        ogg_int64_t total = op_pcm_total(file_, -1);
        duration_ = (total >= 0) ? (double)total / 48000.0 : -1.0;
        cacheTags();
        return true;
    }

    int decode(int16_t* buffer, int maxFrames) override {
        int total = 0;
        while (total < maxFrames) {
            int got = op_read_stereo(file_,
                                     buffer + total * 2,
                                     (maxFrames - total) * 2);
            if (got == 0) break;
            if (got < 0) return -1;
            total += got;
        }
        return total;
    }

    int    getSampleRate()      const override { return 48000; }
    double getDurationSeconds() const override { return duration_; }
    bool   isOpen()             const override { return file_ != nullptr; }

    double getPositionSeconds() const override {
        if (!file_) return 0.0;
        ogg_int64_t pos = op_pcm_tell(file_);
        return (pos >= 0) ? (double)pos / 48000.0 : 0.0;
    }

    void seekTo(double seconds) override {
        if (!file_) return;
        ogg_int64_t sample = (ogg_int64_t)(seconds * 48000.0);
        op_pcm_seek(file_, sample);
    }

    void close() override {
        if (file_) { op_free(file_); file_ = nullptr; }
    }

    bool loadCoverArt(C2D_Image& image, C3D_Tex& tex,
                       Tex3DS_SubTexture& subtex, bool freeExisting) override {
        if (coverArtBytes_.empty()) return false;
        return loadCoverArtFromBytes(
            reinterpret_cast<const unsigned char*>(coverArtBytes_.data()),
            (int)coverArtBytes_.size(), image, tex, subtex, freeExisting);
    }

    std::string getArtist() const override { return artist_; }

private:
    OggOpusFile* file_      = nullptr;
    double       duration_  = -1.0;
    std::string  coverArtBytes_;
    std::string  artist_;
    std::string  trackNumber_;

    static constexpr int PIC_PFX   = 23; // strlen("METADATA_BLOCK_PICTURE=")
    static constexpr int ART_PFX   = 7;  // strlen("ARTIST=")
    static constexpr int TRACK_PFX = 12; // strlen("TRACKNUMBER=")

    void cacheTags() {
        const OpusTags* tags = op_tags(file_, -1);
        if (!tags) return;
        for (int i = 0; i < tags->comments; ++i) {
            const char* c   = tags->user_comments[i];
            int         len = tags->comment_lengths[i];
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

std::unique_ptr<IAudioDecoder> makeOpusDecoder() {
    return std::make_unique<OpusDecoder>();
}

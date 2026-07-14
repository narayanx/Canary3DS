#include <cstring>
#include <mpg123.h>
#include <string>

#include "audio_decoder.h"
#include "gfx.h"
#include "image.h"

// ---------------------------------------------------------------------------
// Mp3Decoder – wraps libmpg123 (3ds-mpg123)
// ---------------------------------------------------------------------------

class Mp3Decoder final : public IAudioDecoder {
  public:
    Mp3Decoder() = default;
    ~Mp3Decoder() override {
        close();
    }

    bool open(const std::string &path) override {
        mpg123_init();

        int err = MPG123_OK;
        mh_ = mpg123_new(nullptr, &err);
        if (!mh_) {
            logToDebugScreen("mpg123_new failed: " + std::to_string(err));
            return false;
        }

        // Without this flag mpg123 ignores picture data entirely and v2->pictures is always 0
        mpg123_param(mh_, MPG123_ADD_FLAGS, MPG123_PICTURE, 0.0);
        // Must be set before mpg123_open(). The channel count gets locked in
        // the moment the core format is probed (during mpg123_getformat()
        // below), so forcing stereo afterwards via mpg123_format() has no
        // effect on mono sources
        mpg123_param(mh_, MPG123_ADD_FLAGS, MPG123_FORCE_STEREO, 0.0);

        if (mpg123_open(mh_, path.c_str()) != MPG123_OK) {
            logToDebugScreen("mpg123_open failed: " + path);
            mpg123_delete(mh_);
            mh_ = nullptr;
            return false;
        }

        // Force stereo signed-16 output so we never need mono up-mix
        long rate = 0;
        int chans = 0, enc = 0;
        mpg123_getformat(mh_, &rate, &chans, &enc);
        mpg123_format_none(mh_);
        mpg123_format(mh_, rate, MPG123_STEREO, MPG123_ENC_SIGNED_16);

        sampleRate_ = (int) rate;

        // Duration from Xing/Info VBR header or CBR bitrate (no full scan).
        off_t total = mpg123_length(mh_);
        duration_ = (total > 0 && sampleRate_ > 0) ? (double) total / (double) sampleRate_ : -1.0;

        cacheTags();
        return true;
    }

    int decode(int16_t *buffer, int maxFrames) override {
        if (!mh_) {
            return -1;
        }
        size_t done = 0;
        size_t bytes = (size_t) maxFrames * 2 * sizeof(int16_t);
        int rc = mpg123_read(mh_, (unsigned char *) buffer, bytes, &done);
        if (rc == MPG123_DONE || done == 0) {
            return 0;
        }
        if (rc != MPG123_OK && rc != MPG123_NEW_FORMAT) {
            return -1;
        }
        return (int) (done / (2 * sizeof(int16_t)));
    }

    int getSampleRate() const override {
        return sampleRate_;
    }
    double getDurationSeconds() const override {
        return duration_;
    }
    bool isOpen() const override {
        return mh_ != nullptr;
    }

    double getPositionSeconds() const override {
        if (!mh_ || sampleRate_ <= 0) {
            return 0.0;
        }
        off_t pos = mpg123_tell(mh_);
        return (pos >= 0) ? (double) pos / (double) sampleRate_ : 0.0;
    }

    void seekTo(double seconds) override {
        if (!mh_ || sampleRate_ <= 0) {
            return;
        }
        off_t sample = (off_t) (seconds * sampleRate_);
        mpg123_seek(mh_, sample, SEEK_SET);
    }

    void close() override {
        if (mh_) {
            mpg123_close(mh_);
            mpg123_delete(mh_);
            mh_ = nullptr;
        }
    }

    bool loadCoverArt(C2D_Image &image,
                      C3D_Tex &tex,
                      Tex3DS_SubTexture &subtex,
                      bool freeExisting) override {
        if (coverArtBytes_.empty()) {
            return false;
        }
        return loadCoverArtFromBytes(reinterpret_cast<const unsigned char *>(coverArtBytes_.data()),
                                     (int) coverArtBytes_.size(),
                                     image,
                                     tex,
                                     subtex,
                                     freeExisting);
    }
    const std::string &getCoverArtBytes() const override {
        return coverArtBytes_;
    }

    std::string getArtist() const override {
        return artist_;
    }
    std::string getTrackNumber() const override {
        return trackNumber_;
    }

  private:
    mpg123_handle *mh_ = nullptr;
    int sampleRate_ = 44100;
    double duration_ = -1.0;
    std::string coverArtBytes_;
    std::string artist_;
    std::string trackNumber_;

    void cacheTags() {
        if (!mh_) {
            return;
        }
        int meta = mpg123_meta_check(mh_);
        if (!(meta & MPG123_ID3)) {
            return;
        }

        mpg123_id3v1 *v1 = nullptr;
        mpg123_id3v2 *v2 = nullptr;
        if (mpg123_id3(mh_, &v1, &v2) != MPG123_OK) {
            return;
        }

        // ID3v2 – preferred source for all tags
        if (v2) {
            if (v2->artist && v2->artist->p && v2->artist->p[0]) {
                artist_ = v2->artist->p;
            }

            // TRCK frame holds track number (may be "N/M" – store as-is)
            for (size_t i = 0; i < v2->texts; ++i) {
                if (memcmp(v2->text[i].id, "TRCK", 4) == 0 && v2->text[i].text.p &&
                    v2->text[i].text.p[0]) {
                    trackNumber_ = v2->text[i].text.p;
                    break;
                }
            }

            // Cover art (APIC frames)
            if (coverArtBytes_.empty()) {
                for (size_t i = 0; i < v2->pictures; ++i) {
                    mpg123_picture &pic = v2->picture[i];
                    if (pic.size > 0 && pic.data) {
                        coverArtBytes_.assign(reinterpret_cast<char *>(pic.data), pic.size);
                        break;
                    }
                }
            }
        }

        // ID3v1 fallback for artist if v2 didn't provide one
        if (artist_.empty() && v1 && v1->artist[0]) {
            artist_ = std::string(v1->artist, strnlen(v1->artist, sizeof(v1->artist)));
        }
    }
};

std::unique_ptr<IAudioDecoder> makeMp3Decoder() {
    return std::make_unique<Mp3Decoder>();
}

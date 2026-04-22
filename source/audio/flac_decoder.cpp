#include "audio_decoder.h"
#include "image.h"
#include "gfx.h"

#include <FLAC/stream_decoder.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// FlacDecoder – wraps libFLAC (3ds-flac)
//
// libFLAC is callback-based.  We bridge it to IAudioDecoder's pull interface
// by accumulating decoded int16 stereo pairs in an internal ring buffer that
// decode() drains.
// ---------------------------------------------------------------------------

class FlacDecoder final : public IAudioDecoder {
public:
    FlacDecoder()  = default;
    ~FlacDecoder() override { close(); }

    bool open(const std::string& path) override {
        dec_ = FLAC__stream_decoder_new();
        if (!dec_) { logToDebugScreen("FLAC: alloc failed"); return false; }

        // Request both PICTURE and VORBIS_COMMENT metadata blocks.
        FLAC__stream_decoder_set_metadata_respond(dec_, FLAC__METADATA_TYPE_PICTURE);
        FLAC__stream_decoder_set_metadata_respond(dec_, FLAC__METADATA_TYPE_VORBIS_COMMENT);

        FLAC__StreamDecoderInitStatus st =
            FLAC__stream_decoder_init_file(dec_, path.c_str(),
                                           writeCallback, metadataCallback,
                                           errorCallback, this);
        if (st != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
            logToDebugScreen("FLAC init error: " + std::to_string((int)st));
            FLAC__stream_decoder_delete(dec_); dec_ = nullptr;
            return false;
        }

        // Process until end of metadata to populate sampleRate_, totalSamples_, channels_
        if (!FLAC__stream_decoder_process_until_end_of_metadata(dec_)) {
            logToDebugScreen("FLAC: metadata read failed");
            FLAC__stream_decoder_delete(dec_); dec_ = nullptr;
            return false;
        }
        return true;
    }

    int decode(int16_t* buffer, int maxFrames) override {
        if (!dec_) return -1;

        // Pull frames from decoder until we have enough or reach EOF
        while ((int)pcmReady() < maxFrames && !eof_) {
            if (!FLAC__stream_decoder_process_single(dec_)) {
                eof_ = true;
                break;
            }
            if (FLAC__stream_decoder_get_state(dec_) ==
                FLAC__STREAM_DECODER_END_OF_STREAM) eof_ = true;
        }

        int frames = std::min(maxFrames, (int)pcmReady());
        if (frames == 0) return 0;

        memcpy(buffer, pcmBuf_.data() + readPos_, (size_t)frames * 2 * sizeof(int16_t));
        readPos_ += (size_t)frames * 2;

        // Compact when we've consumed at least half the buffer
        if (readPos_ > pcmBuf_.size() / 2) compact();

        return frames;
    }

    int    getSampleRate()      const override { return sampleRate_; }
    double getDurationSeconds() const override {
        return (sampleRate_ > 0 && totalSamples_ > 0)
               ? (double)totalSamples_ / (double)sampleRate_
               : -1.0;
    }
    bool   isOpen()             const override { return dec_ != nullptr; }

    double getPositionSeconds() const override {
        return (sampleRate_ > 0)
               ? (double)currentSample_ / (double)sampleRate_
               : 0.0;
    }

    void seekTo(double seconds) override {
        if (!dec_ || sampleRate_ <= 0) return;
        FLAC__uint64 target = (FLAC__uint64)(seconds * sampleRate_);
        // Clear our buffer before seeking; write callback will refill it
        pcmBuf_.clear();
        readPos_ = 0;
        eof_     = false;
        if (!FLAC__stream_decoder_seek_absolute(dec_, target)) {
            // Seek failed (e.g. unseekable); attempt recovery
            FLAC__stream_decoder_flush(dec_);
        }
        currentSample_ = target;
    }

    void close() override {
        if (dec_) {
            FLAC__stream_decoder_finish(dec_);
            FLAC__stream_decoder_delete(dec_);
            dec_ = nullptr;
        }
        pcmBuf_.clear();
        readPos_ = 0;
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

    // Public so static callbacks can access via client_data
    int          sampleRate_    = 44100;
    int          channels_      = 2;
    int          bitsPerSample_ = 16;
    FLAC__uint64 totalSamples_  = 0;
    FLAC__uint64 currentSample_ = 0;
    bool         eof_           = false;

    std::vector<int16_t> pcmBuf_;   // decoded stereo int16 pairs
    size_t               readPos_ = 0;

    std::string coverArtBytes_;
    std::string artist_;
    std::string trackNumber_;

private:
    FLAC__StreamDecoder* dec_ = nullptr;

    size_t pcmReady() const {
        return (pcmBuf_.size() - readPos_) / 2; // frames, not samples
    }

    void compact() {
        pcmBuf_.erase(pcmBuf_.begin(), pcmBuf_.begin() + (ptrdiff_t)readPos_);
        readPos_ = 0;
    }

    static FLAC__StreamDecoderWriteStatus writeCallback(
        const FLAC__StreamDecoder* /*dec*/,
        const FLAC__Frame* frame,
        const FLAC__int32* const buffer[],
        void* client_data)
    {
        auto* self = static_cast<FlacDecoder*>(client_data);
        const unsigned n   = frame->header.blocksize;
        const int      bps = (int)frame->header.bits_per_sample;

        // Scale samples to int16 range
        auto toInt16 = [&](FLAC__int32 s) -> int16_t {
            if (bps > 16) return (int16_t)(s >> (bps - 16));
            if (bps < 16) return (int16_t)(s << (16 - bps));
            return (int16_t)s;
        };

        const int ch = (int)frame->header.channels;
        self->pcmBuf_.reserve(self->pcmBuf_.size() + n * 2);

        if (ch >= 2) {
            for (unsigned i = 0; i < n; ++i) {
                self->pcmBuf_.push_back(toInt16(buffer[0][i]));
                self->pcmBuf_.push_back(toInt16(buffer[1][i]));
            }
        } else {
            // Mono → stereo
            for (unsigned i = 0; i < n; ++i) {
                int16_t s = toInt16(buffer[0][i]);
                self->pcmBuf_.push_back(s);
                self->pcmBuf_.push_back(s);
            }
        }
        self->currentSample_ += n;
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }

    static void metadataCallback(
        const FLAC__StreamDecoder* /*dec*/,
        const FLAC__StreamMetadata* metadata,
        void* client_data)
    {
        auto* self = static_cast<FlacDecoder*>(client_data);

        if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
            self->sampleRate_    = (int)metadata->data.stream_info.sample_rate;
            self->channels_      = (int)metadata->data.stream_info.channels;
            self->bitsPerSample_ = (int)metadata->data.stream_info.bits_per_sample;
            self->totalSamples_  = metadata->data.stream_info.total_samples;
        }

        if (metadata->type == FLAC__METADATA_TYPE_PICTURE &&
            self->coverArtBytes_.empty()) {
            const auto& pic = metadata->data.picture;
            if (pic.data && pic.data_length > 0)
                self->coverArtBytes_.assign(
                    reinterpret_cast<const char*>(pic.data), pic.data_length);
        }

        if (metadata->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
            const auto& vc = metadata->data.vorbis_comment;
            for (FLAC__uint32 i = 0; i < vc.num_comments; ++i) {
                const char*   entry = reinterpret_cast<const char*>(vc.comments[i].entry);
                FLAC__uint32  len   = vc.comments[i].length;
                if (strncasecmp(entry, "ARTIST=", 7) == 0 && self->artist_.empty())
                    self->artist_ = std::string(entry + 7, len - 7);
                else if (strncasecmp(entry, "TRACKNUMBER=", 12) == 0 && self->trackNumber_.empty())
                    self->trackNumber_ = std::string(entry + 12, len - 12);
            }
        }
    }

    static void errorCallback(
        const FLAC__StreamDecoder* /*dec*/,
        FLAC__StreamDecoderErrorStatus status,
        void* /*client_data*/)
    {
        logToDebugScreen("FLAC decode error: " + std::to_string((int)status));
    }
};

std::unique_ptr<IAudioDecoder> makeFlacDecoder() {
    return std::make_unique<FlacDecoder>();
}

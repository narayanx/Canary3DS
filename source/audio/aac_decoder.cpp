#include "audio_decoder.h"
#include "gfx.h"
#include "image.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Custom AVIOContext callbacks - back FFmpeg's I/O with a plain FILE*
// so libctru's fopen patch handles the sdmc:/ path transparently.
static constexpr int AVIO_BUF_SIZE = 32 * 1024;  // 32 KB read buffer

static int avioRead(void* opaque, uint8_t* buf, int bufSize) {
    FILE* f = static_cast<FILE*>(opaque);
    int got = (int)fread(buf, 1, (size_t)bufSize, f);
    if (got == 0) return AVERROR_EOF;
    return got;
}

static int64_t avioSeek(void* opaque, int64_t offset, int whence) {
    FILE* f = static_cast<FILE*>(opaque);
    if (whence == AVSEEK_SIZE) {
        long cur = ftell(f);
        fseek(f, 0, SEEK_END);
        long end = ftell(f);
        fseek(f, cur, SEEK_SET);
        return (int64_t)end;
    }
    int stdWhence = (whence == SEEK_SET) ? SEEK_SET : (whence == SEEK_CUR) ? SEEK_CUR : SEEK_END;
    if (fseek(f, (long)offset, stdWhence) != 0) return -1;
    return (int64_t)ftell(f);
}

// ---------------------------------------------------------------------------
// AacDecoder
// M4A / AAC decoder - wraps FFmpeg
//
// Uses libavformat (demuxing the M4A/MP4 container),
//      libavcodec  (AAC decoding, output is AV_SAMPLE_FMT_FLTP),
//      libswresample (FLTP planar-float → S16 interleaved stereo).
// ---------------------------------------------------------------------------

class AacDecoder final : public IAudioDecoder {
public:
    AacDecoder()  = default;
    ~AacDecoder() override { close(); }

    bool open(const std::string& path) override {
        // Open the file via fopen so libctru handles sdmc:/
        file_ = fopen(path.c_str(), "rb");
        if (!file_) {
            logToBottomScreen("AAC: fopen failed: " + path);
            return false;
        }

        uint8_t* ioBuf = static_cast<uint8_t*>(av_malloc(AVIO_BUF_SIZE));
        if (!ioBuf) {
            logToBottomScreen("AAC: av_malloc for avio buffer failed");
            fclose(file_); file_ = nullptr;
            return false;
        }

        avioCtx_ = avio_alloc_context(ioBuf, AVIO_BUF_SIZE,
                                      /*write_flag=*/0,
                                      /*opaque=*/file_,
                                      avioRead, nullptr, avioSeek);
        if (!avioCtx_) {
            logToBottomScreen("AAC: avio_alloc_context failed");
            av_free(ioBuf);
            fclose(file_); file_ = nullptr;
            return false;
        }

        fmtCtx_ = avformat_alloc_context();
        if (!fmtCtx_) {
            logToBottomScreen("AAC: avformat_alloc_context failed");
            av_freep(&avioCtx_->buffer);
            avio_context_free(&avioCtx_);
            fclose(file_); file_ = nullptr;
            return false;
        }
        fmtCtx_->pb = avioCtx_;

        // Pass nullptr as the URL - the container is identified by probing
        // the byte stream, not by the file extension.
        int rc = avformat_open_input(&fmtCtx_, nullptr, nullptr, nullptr);
        if (rc != 0) {
            logToBottomScreen("AAC: avformat_open_input failed");
            // avformat_open_input frees fmtCtx_ on failure
            fmtCtx_ = nullptr;
            av_freep(&avioCtx_->buffer);
            avio_context_free(&avioCtx_);
            fclose(file_); file_ = nullptr;
            return false;
        }

        rc = avformat_find_stream_info(fmtCtx_, nullptr);
        if (rc < 0) {
            logToBottomScreen("AAC: avformat_find_stream_info failed");
            avformat_close_input(&fmtCtx_); fmtCtx_ = nullptr;
            av_freep(&avioCtx_->buffer);
            avio_context_free(&avioCtx_);
            fclose(file_); file_ = nullptr;
            return false;
        }

        // Find the first audio stream
        audioStreamIdx_ = -1;
        for (unsigned i = 0; i < fmtCtx_->nb_streams; ++i) {
            AVStream* s = fmtCtx_->streams[i];
            if (s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIdx_ < 0) {
                audioStreamIdx_ = (int)i;
            }
        }
        if (audioStreamIdx_ < 0) {
            logToBottomScreen("AAC: no audio stream found");
            avformat_close_input(&fmtCtx_); fmtCtx_ = nullptr;
            av_freep(&avioCtx_->buffer);
            avio_context_free(&avioCtx_);
            fclose(file_); file_ = nullptr;
            return false;
        }

        AVStream* audioStream = fmtCtx_->streams[audioStreamIdx_];
        AVCodecParameters* par = audioStream->codecpar;

        // Set up codec context
        const AVCodec* codec = avcodec_find_decoder(par->codec_id);
        if (!codec) {
            logToBottomScreen("AAC: no decoder for codec id " + std::to_string(par->codec_id));
            avformat_close_input(&fmtCtx_); fmtCtx_ = nullptr;
            av_freep(&avioCtx_->buffer);
            avio_context_free(&avioCtx_);
            fclose(file_); file_ = nullptr;
            return false;
        }

        codecCtx_ = avcodec_alloc_context3(codec);
        if (!codecCtx_) {
            logToBottomScreen("AAC: avcodec_alloc_context3 failed");
            avformat_close_input(&fmtCtx_); fmtCtx_ = nullptr;
            av_freep(&avioCtx_->buffer);
            avio_context_free(&avioCtx_);
            fclose(file_); file_ = nullptr;
            return false;
        }

        rc = avcodec_parameters_to_context(codecCtx_, par);
        if (rc < 0) {
            logToBottomScreen("AAC: avcodec_parameters_to_context failed");
            avcodec_free_context(&codecCtx_);
            avformat_close_input(&fmtCtx_); fmtCtx_ = nullptr;
            av_freep(&avioCtx_->buffer);
            avio_context_free(&avioCtx_);
            fclose(file_); file_ = nullptr;
            return false;
        }

        rc = avcodec_open2(codecCtx_, codec, nullptr);
        if (rc != 0) {
            logToBottomScreen("AAC: avcodec_open2 failed: " + std::to_string(rc));
            avcodec_free_context(&codecCtx_);
            avformat_close_input(&fmtCtx_); fmtCtx_ = nullptr;
            av_freep(&avioCtx_->buffer);
            avio_context_free(&avioCtx_);
            fclose(file_); file_ = nullptr;
            return false;
        }

        sampleRate_ = codecCtx_->sample_rate;
        inChannels_ = codecCtx_->ch_layout.nb_channels;
        if (inChannels_ < 1) inChannels_ = 1;

        // Duration
        if (fmtCtx_->duration != AV_NOPTS_VALUE) {
            duration_ = (double)fmtCtx_->duration / (double)AV_TIME_BASE;
        } else if (audioStream->duration != AV_NOPTS_VALUE) {
            duration_ = (double)audioStream->duration * av_q2d(audioStream->time_base);
        }

        // Set up swresample for FLTP → S16 interleaved stereo
        AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
        AVChannelLayout inLayout = codecCtx_->ch_layout;

        // If the codec context has no valid channel layout, derive one from count
        if (inLayout.nb_channels == 0) {
            av_channel_layout_default(&inLayout, inChannels_);
        }

        rc = swr_alloc_set_opts2(&swrCtx_,
                                 &outLayout, AV_SAMPLE_FMT_S16,  sampleRate_,
                                 &inLayout, codecCtx_->sample_fmt, sampleRate_,
                                 0, nullptr);
        if (rc != 0 || !swrCtx_) {
            logToBottomScreen("AAC: swr_alloc_set_opts2 failed");
            avcodec_free_context(&codecCtx_);
            avformat_close_input(&fmtCtx_); fmtCtx_ = nullptr;
            av_freep(&avioCtx_->buffer);
            avio_context_free(&avioCtx_);
            fclose(file_); file_ = nullptr;
            return false;
        }

        rc = swr_init(swrCtx_);
        if (rc != 0) {
            logToBottomScreen("AAC: swr_init failed: " + std::to_string(rc));
            swr_free(&swrCtx_);
            avcodec_free_context(&codecCtx_);
            avformat_close_input(&fmtCtx_); fmtCtx_ = nullptr;
            av_freep(&avioCtx_->buffer);
            avio_context_free(&avioCtx_);
            fclose(file_); file_ = nullptr;
            return false;
        }

        // Allocate reusable decode packet and frame
        pkt_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        if (!pkt_ || !frame_) {
            logToBottomScreen("AAC: av_packet/frame_alloc failed");
            close();
            return false;
        }

        cacheTags();
        cacheCoverArt();

        open_ = true;
        return true;
    }

    // Pull up to maxFrames interleaved S16 stereo pairs into buffer.
    int decode(int16_t* buffer, int maxFrames) override {
        if (!open_) return -1;

        int produced = 0;

        while (produced < maxFrames) {
            // Drain the PCM ring buffer first
            if (!pcmBuf_.empty()) {
                size_t available = pcmBuf_.size() / 2;  // stereo frames
                size_t want = (size_t)(maxFrames - produced);
                size_t copy = (available < want) ? available : want;

                memcpy(buffer + produced * 2, pcmBuf_.data(), copy * 2 * sizeof(int16_t));
                pcmBuf_.erase(pcmBuf_.begin(), pcmBuf_.begin() + (ptrdiff_t)(copy * 2));
                produced += (int)copy;
                continue;
            }

            // Receive a decoded frame (may be buffered inside codec)
            int rc = avcodec_receive_frame(codecCtx_, frame_);
            if (rc == 0) {
                pushFrameToRingBuf();
                av_frame_unref(frame_);
                continue;
            }
            if (rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) {
                // Hard decode error
                return produced > 0 ? produced : -1;
            }

            // Read the next audio packet from the container
            bool gotPacket = false;
            while (!gotPacket) {
                av_packet_unref(pkt_);
                rc = av_read_frame(fmtCtx_, pkt_);
                if (rc == AVERROR_EOF) {
                    // Flush the codec
                    avcodec_send_packet(codecCtx_, nullptr);
                    int flushRc = avcodec_receive_frame(codecCtx_, frame_);
                    if (flushRc == 0) {
                        pushFrameToRingBuf();
                        av_frame_unref(frame_);
                    }
                    eof_ = true;
                    goto drainRemainder;
                }
                if (rc < 0) {
                    // Read error
                    goto drainRemainder;
                }
                if (pkt_->stream_index == audioStreamIdx_) {
                    gotPacket = true;
                }
                // Packets from other streams (video / cover art) are skipped
            }

            rc = avcodec_send_packet(codecCtx_, pkt_);
            if (rc < 0 && rc != AVERROR(EAGAIN)) {
                // Send error – skip this packet and continue
                av_packet_unref(pkt_);
            }
        }

    drainRemainder:
        // One final drain of whatever landed in pcmBuf_ during flush
        if (!pcmBuf_.empty() && produced < maxFrames) {
            size_t available = pcmBuf_.size() / 2;
            size_t want = (size_t)(maxFrames - produced);
            size_t copy = (available < want) ? available : want;
            memcpy(buffer + produced * 2, pcmBuf_.data(), copy * 2 * sizeof(int16_t));
            pcmBuf_.erase(pcmBuf_.begin(), pcmBuf_.begin() + (ptrdiff_t)(copy * 2));
            produced += (int)copy;
        }

        return produced;
    }

    int    getSampleRate()      const override { return sampleRate_; }
    double getDurationSeconds() const override { return duration_; }
    bool   isOpen()             const override { return open_; }

    double getPositionSeconds() const override {
        if (!open_ || sampleRate_ <= 0) return 0.0;
        return (double)samplesDecoded_ / (double)sampleRate_;
    }

    void seekTo(double seconds) override {
        if (!open_) return;

        // Convert target time to the audio stream's time base
        AVStream* s = fmtCtx_->streams[audioStreamIdx_];
        int64_t targetPts = (int64_t)(seconds / av_q2d(s->time_base));

        // Seek to a keyframe at or before the target
        avformat_seek_file(fmtCtx_, audioStreamIdx_, INT64_MIN, targetPts, targetPts, 0);

        // Discard any buffered codec state and our PCM ring buffer
        avcodec_flush_buffers(codecCtx_);
        pcmBuf_.clear();
        eof_ = false;

        samplesDecoded_ = (int64_t)(seconds * sampleRate_);
    }

    void close() override {
        if (swrCtx_)  { swr_free(&swrCtx_);               swrCtx_   = nullptr; }
        if (frame_)   { av_frame_free(&frame_);           frame_    = nullptr; }
        if (pkt_)     { av_packet_free(&pkt_);            pkt_      = nullptr; }
        if (codecCtx_){ avcodec_free_context(&codecCtx_); codecCtx_ = nullptr; }
        if (fmtCtx_) {
            // Detach our custom AVIO before close so FFmpeg doesn't flush/free it
            fmtCtx_->pb = nullptr;
            avformat_close_input(&fmtCtx_);
        }
        if (avioCtx_) {
            av_freep(&avioCtx_->buffer);
            avio_context_free(&avioCtx_);
        }
        if (file_) {
            fclose(file_); file_ = nullptr;
        }
        pcmBuf_.clear();
        open_ = false;
    }

    bool loadCoverArt(C2D_Image& image, C3D_Tex& tex, Tex3DS_SubTexture& subtex,
                      bool freeExisting) override {
        if (coverArtBytes_.empty()) return false;
        return loadCoverArtFromBytes(reinterpret_cast<const unsigned char*>(coverArtBytes_.data()),
                                     (int)coverArtBytes_.size(), image, tex, subtex, freeExisting);
    }

    std::string getArtist() const override { return artist_; }

private:
    // FFmpeg handles
    FILE*            file_     = nullptr;
    AVIOContext*     avioCtx_  = nullptr;
    AVFormatContext* fmtCtx_   = nullptr;
    AVCodecContext*  codecCtx_ = nullptr;
    SwrContext*      swrCtx_   = nullptr;
    AVPacket*        pkt_      = nullptr;
    AVFrame*         frame_    = nullptr;

    int    audioStreamIdx_ = -1;
    int    sampleRate_     = 44100;
    int    inChannels_     = 2;
    double duration_       = -1.0;
    bool   open_           = false;
    bool   eof_            = false;

    // Position tracking (in input samples at native rate)
    int64_t samplesDecoded_ = 0;

    // Decoded but not-yet-consumed interleaved S16 stereo samples
    std::vector<int16_t> pcmBuf_;

    // Cached metadata
    std::string coverArtBytes_;
    std::string artist_;

    // Convert one decoded AVFrame through swr and push to pcmBuf_.
    void pushFrameToRingBuf() {
        if (!swrCtx_ || !frame_) return;

        const int outFrames = frame_->nb_samples;
        if (outFrames <= 0) return;

        // swr_convert wants uint8_t** for output
        // Allocate a temporary S16 interleaved stereo buffer
        std::vector<int16_t> tmp((size_t)outFrames * 2);
        uint8_t* outPtr = reinterpret_cast<uint8_t*>(tmp.data());

        int converted = swr_convert(swrCtx_,
                                    &outPtr, outFrames,
                                    (const uint8_t**)frame_->data,
                                    frame_->nb_samples);
        if (converted <= 0) return;

        pcmBuf_.insert(pcmBuf_.end(), tmp.begin(), tmp.begin() + converted * 2);

        samplesDecoded_ += converted;
    }

    // Read tags from the format-level metadata dictionary.
    // M4A stores tags (artist, album, title …) there as iTunes atoms
    // which FFmpeg maps to standard lowercase key names.
    void cacheTags() {
        if (!fmtCtx_) return;
        AVDictionaryEntry* entry = nullptr;

        // "artist" covers both ©ART (iTunes) and ID3-style ARTIST
        entry = av_dict_get(fmtCtx_->metadata, "artist", nullptr, AV_DICT_IGNORE_SUFFIX);
        if (entry && entry->value && entry->value[0]) artist_ = entry->value;

        // Fallback: "album_artist" tag
        if (artist_.empty()) {
            entry = av_dict_get(fmtCtx_->metadata, "album_artist", nullptr, AV_DICT_IGNORE_SUFFIX);
            if (entry && entry->value && entry->value[0]) artist_ = entry->value;
        }
    }

    // Extract cover art from an attached-picture stream.
    // In M4A/MP4, iTunes embeds cover art as a video stream whose
    // disposition has AV_DISPOSITION_ATTACHED_PIC set.  The raw image
    // bytes (JPEG or PNG) live in stream->attached_pic.data / size.
    void cacheCoverArt() {
        if (!fmtCtx_) return;

        for (unsigned i = 0; i < fmtCtx_->nb_streams; ++i) {
            AVStream* s = fmtCtx_->streams[i];
            if (!(s->disposition & AV_DISPOSITION_ATTACHED_PIC)) continue;

            const AVPacket& pic = s->attached_pic;
            if (pic.data && pic.size > 0) {
                coverArtBytes_.assign(reinterpret_cast<const char*>(pic.data), (size_t)pic.size);
                break;  // use the first attached picture
            }
        }
    }
};

std::unique_ptr<IAudioDecoder> makeAacDecoder() {
    return std::make_unique<AacDecoder>();
}

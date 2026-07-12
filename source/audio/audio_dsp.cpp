#include "audio_dsp.h"

#include <algorithm>
#include <cmath>

namespace {
    constexpr float kPi = 3.14159265358979323846f;
    // Frames pulled from `source` per iteration while topping up wsolaIn_.
    constexpr int kSourceReadChunk = 512;
    // Frames pulled from wsolaPull() per iteration while topping up resIn_.
    constexpr int kResampleReadChunk = 256;

    // Drop samples already consumed (more than `marginFrames` behind `pos`) from
    // the front of `buf`, once that's more than `thresholdFrames`, and rebase
    // `pos` to match. Used by both the WSOLA input buffer and the resample
    // input buffer to keep them from growing for the whole length of a song.
    //
    // This is an O(n) erase, run roughly every 50-100ms of audio depending on
    // speed/pitch settings. At kCompactThreshold's size that's a memmove of a
    // few tens of KB, which is cheap next to a 268 MHz CPU's per-second budget;
    // a ring buffer would avoid it entirely but isn't warranted at this rate.
    void compactBuffer(std::vector<int16_t> &buf,
                       double &pos,
                       int64_t marginFrames,
                       int64_t thresholdFrames,
                       int channels) {
        int64_t keepFrom = (int64_t) pos - marginFrames;
        if (keepFrom > thresholdFrames) {
            size_t dropSamples = (size_t) keepFrom * channels;
            dropSamples = std::min(dropSamples, buf.size());
            buf.erase(buf.begin(), buf.begin() + (long) dropSamples);
            pos -= (double) (dropSamples / channels);
        }
    }
}  // namespace

SpeedPitchProcessor::SpeedPitchProcessor() {
    window_.resize((size_t) kFrameSize);
    for (int n = 0; n < kFrameSize; ++n) {
        // Periodic Hann window: satisfies constant overlap-add at 50% hop,
        // so alpha == 1.0 reconstructs the input without amplitude
        // modulation (COLA holds exactly for this window/hop combination).
        window_[(size_t) n] = 0.5f - 0.5f * std::cos(2.0f * kPi * (float) n / (float) kFrameSize);
    }

    // Each buffer ramps up to roughly kCompactThreshold (plus a small
    // margin for wsolaIn_) between compactions, then gets trimmed back
    // down; reserving that ceiling up front avoids reallocation churn
    // once a song is playing. This processor outlives any single song
    // (reset() clears contents, not capacity), so it's a one-time cost.
    wsolaIn_.reserve((size_t) (kCompactThreshold + kSearchRadius + kFrameSize) * kChannels);
    wsolaOut_.reserve((size_t) kCompactThreshold * kChannels);
    resIn_.reserve((size_t) kCompactThreshold * kChannels);
    lastTail_.reserve((size_t) kCompareLen);
}

void SpeedPitchProcessor::recomputeAlpha() {
    alpha_ = speed_ / pitch_;
}

void SpeedPitchProcessor::setSpeed(float speed) {
    speed = std::clamp(speed, 0.25f, 4.0f);
    if (std::abs(speed_ - speed) < 1e-6f) {
        return;
    }
    speed_ = speed;
    recomputeAlpha();
}

void SpeedPitchProcessor::setPitch(float ratio) {
    ratio = std::clamp(ratio, 0.25f, 4.0f);
    if (std::abs(pitch_ - ratio) < 1e-6f) {
        return;
    }
    pitch_ = ratio;
    recomputeAlpha();
}

bool SpeedPitchProcessor::isIdentity() const {
    return std::abs(speed_ - 1.0f) < 1e-4f && std::abs(pitch_ - 1.0f) < 1e-4f;
}

void SpeedPitchProcessor::reset() {
    wsolaIn_.clear();
    wsolaSourceEOF_ = false;
    analysisPos_ = 0.0;
    haveTail_ = false;
    lastTail_.clear();
    accum_.clear();
    wsolaOut_.clear();
    wsolaOutPos_ = 0;
    wsolaFinalizing_ = false;
    resIn_.clear();
    resPos_ = 0.0;
    resDrained_ = false;
}

void SpeedPitchProcessor::wsolaEnsureInput(const SourceFn &source, double upToPos) {
    int64_t needed = (int64_t) std::ceil(upToPos);
    while (!wsolaSourceEOF_ && (int64_t) (wsolaIn_.size() / kChannels) < needed) {
        int16_t chunk[kSourceReadChunk * kChannels];
        int got = source(chunk, kSourceReadChunk);
        if (got <= 0) {
            wsolaSourceEOF_ = true;
            break;
        }
        wsolaIn_.insert(wsolaIn_.end(), chunk, chunk + (size_t) got * kChannels);
    }
}

bool SpeedPitchProcessor::wsolaProduceHop(const SourceFn &source) {
    double maxNeeded = analysisPos_ + kSearchRadius + kFrameSize;
    wsolaEnsureInput(source, maxNeeded);

    int64_t avail = (int64_t) (wsolaIn_.size() / kChannels);
    int64_t basePos = (int64_t) (analysisPos_ + 0.5);

    bool framePastEnd = wsolaSourceEOF_ && basePos >= avail;
    if (framePastEnd) {
        if (wsolaFinalizing_) {
            return false;  // already flushed the final tail; fully drained
        }
        wsolaFinalizing_ = true;
    }
    if (avail <= 0 && wsolaSourceEOF_) {
        return false;
    }

    int64_t bestPos = std::max<int64_t>(basePos, 0);

    if (haveTail_ && avail >= kCompareLen) {
        int64_t maxCand = avail - kCompareLen;
        int64_t lo = std::clamp<int64_t>(basePos - kSearchRadius, 0, maxCand);
        int64_t hi = std::clamp<int64_t>(basePos + kSearchRadius, 0, maxCand);
        double bestDiff = 0.0;
        bool found = false;
        for (int64_t cand = lo; cand <= hi; ++cand) {
            double diff = 0.0;
            for (int64_t n = 0; n < kCompareLen; ++n) {
                int64_t idx = (cand + n) * kChannels;
                int mono = wsolaIn_[(size_t) idx] + wsolaIn_[(size_t) idx + 1];
                diff += std::abs(mono - lastTail_[(size_t) n]);
            }
            if (!found || diff < bestDiff) {
                bestDiff = diff;
                bestPos = cand;
                found = true;
            }
        }
    }

    if (accum_.empty()) {
        accum_.assign((size_t) kFrameSize * kChannels, 0.0f);
    }
    for (int64_t n = 0; n < kFrameSize; ++n) {
        int64_t srcIdx = bestPos + n;
        float w = window_[(size_t) n];
        for (int ch = 0; ch < kChannels; ++ch) {
            int16_t s =
                (srcIdx >= 0 && srcIdx < avail) ? wsolaIn_[(size_t) srcIdx * kChannels + ch] : 0;
            accum_[(size_t) n * kChannels + ch] += w * (float) s;
        }
    }

    size_t outBase = wsolaOut_.size();
    wsolaOut_.resize(outBase + (size_t) kSynthHop * kChannels);
    for (int64_t n = 0; n < kSynthHop; ++n) {
        for (int ch = 0; ch < kChannels; ++ch) {
            float v = accum_[(size_t) n * kChannels + ch];
            v = std::clamp(v, -32768.0f, 32767.0f);
            wsolaOut_[outBase + (size_t) n * kChannels + ch] = (int16_t) v;
        }
    }

    for (int64_t n = 0; n < kFrameSize - kSynthHop; ++n) {
        for (int ch = 0; ch < kChannels; ++ch) {
            accum_[(size_t) n * kChannels + ch] = accum_[(size_t) (n + kSynthHop) * kChannels + ch];
        }
    }
    for (int64_t n = kFrameSize - kSynthHop; n < kFrameSize; ++n) {
        for (int ch = 0; ch < kChannels; ++ch) {
            accum_[(size_t) n * kChannels + ch] = 0.0f;
        }
    }

    lastTail_.assign((size_t) kCompareLen, 0);
    for (int64_t n = 0; n < kCompareLen; ++n) {
        int64_t srcIdx = bestPos + kSynthHop + n;
        int32_t l = (srcIdx >= 0 && srcIdx < avail) ? wsolaIn_[(size_t) srcIdx * kChannels] : 0;
        int32_t r = (srcIdx >= 0 && srcIdx < avail) ? wsolaIn_[(size_t) srcIdx * kChannels + 1] : 0;
        lastTail_[(size_t) n] = l + r;
    }
    haveTail_ = true;

    analysisPos_ += (double) kSynthHop * alpha_;

    compactBuffer(wsolaIn_, analysisPos_, kSearchRadius + kFrameSize, kCompactThreshold, kChannels);

    return true;
}

int SpeedPitchProcessor::wsolaPull(const SourceFn &source, int16_t *out, int maxFrames) {
    int produced = 0;
    while (produced < maxFrames) {
        size_t availOut = wsolaOut_.size() / kChannels - wsolaOutPos_;
        if (availOut == 0) {
            if (!wsolaProduceHop(source)) {
                break;
            }
            continue;
        }
        size_t take = std::min((size_t) (maxFrames - produced), availOut);
        std::copy(wsolaOut_.begin() + (long) (wsolaOutPos_ * kChannels),
                  wsolaOut_.begin() + (long) ((wsolaOutPos_ + take) * kChannels),
                  out + (size_t) produced * kChannels);
        wsolaOutPos_ += take;
        produced += (int) take;
    }
    if (wsolaOutPos_ > 0 && wsolaOutPos_ * kChannels * 2 > wsolaOut_.size()) {
        wsolaOut_.erase(wsolaOut_.begin(), wsolaOut_.begin() + (long) (wsolaOutPos_ * kChannels));
        wsolaOutPos_ = 0;
    }
    return produced;
}

void SpeedPitchProcessor::resEnsureInput(const SourceFn &source, double upToPos) {
    int64_t needed = (int64_t) std::ceil(upToPos) + 1;
    while (!resDrained_ && (int64_t) (resIn_.size() / kChannels) < needed) {
        int16_t chunk[kResampleReadChunk * kChannels];
        int got = wsolaPull(source, chunk, kResampleReadChunk);
        if (got <= 0) {
            resDrained_ = true;
            break;
        }
        resIn_.insert(resIn_.end(), chunk, chunk + (size_t) got * kChannels);
    }
}

int SpeedPitchProcessor::resamplePull(const SourceFn &source, int16_t *out, int maxFrames) {
    int produced = 0;
    while (produced < maxFrames) {
        resEnsureInput(source, resPos_ + 1.0);
        int64_t avail = (int64_t) (resIn_.size() / kChannels);
        int64_t idx = (int64_t) resPos_;
        if (idx + 1 >= avail) {
            // Only reachable once wsolaPull (and the decoder behind it) has
            // truly run out: resEnsureInput above always tries to keep one
            // extra frame past resPos_ buffered. At that point there's no
            // second sample left to interpolate towards, so the final
            // fractional frame is dropped rather than duplicating or
            // zero-extending the last real sample; at most ~1 sample
            // (<1/48000s) of trailing audio, inaudible.
            break;
        }
        double frac = resPos_ - (double) idx;
        for (int ch = 0; ch < kChannels; ++ch) {
            float a = (float) resIn_[(size_t) idx * kChannels + ch];
            float b = (float) resIn_[(size_t) (idx + 1) * kChannels + ch];
            float v = a + (float) frac * (b - a);
            out[(size_t) produced * kChannels + ch] = (int16_t) v;
        }
        resPos_ += pitch_;
        ++produced;

        compactBuffer(resIn_, resPos_, 0, kCompactThreshold, kChannels);
    }
    return produced;
}

int SpeedPitchProcessor::process(const SourceFn &source, int16_t *out, int maxFrames) {
    if (isIdentity()) {
        return source(out, maxFrames);
    }
    return resamplePull(source, out, maxFrames);
}

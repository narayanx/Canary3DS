#pragma once

#include <cstdint>
#include <functional>
#include <vector>

// Real-time speed/pitch processor for interleaved 16-bit stereo PCM.
//
// setSpeed() changes playback speed while preserving pitch (WSOLA
// time-stretch). setPitch() changes pitch while preserving speed (a
// compensating WSOLA stretch feeding a linear-interpolation resample
// stage). Both can be set independently and combined; when both are at
// their default of 1.0f, process() is a direct passthrough to `source`
// with no extra CPU cost or latency.
class SpeedPitchProcessor {
  public:
    // Matches IAudioDecoder::decode()'s contract: decode up to maxFrames
    // interleaved stereo frames into buffer, return frames produced (>0),
    // 0 on EOF, or <0 on error.
    using SourceFn = std::function<int(int16_t *buffer, int maxFrames)>;

    SpeedPitchProcessor();

    void setSpeed(float speed);  // 1.0 = normal, >1 faster, <1 slower
    void setPitch(float ratio);  // 1.0 = normal, 2^(semitones/12) per octave

    // Discard all buffered audio/history. Call on seek or song change.
    void reset();

    // True when speed == pitch == 1.0 (identity; process() is a direct
    // passthrough to `source`).
    bool isIdentity() const;

    // Produce up to maxFrames processed stereo frames into `out`, pulling
    // raw PCM from `source` as needed. Returns frames written; 0 only once
    // `source` has reported EOF and all buffered/pending audio is drained.
    int process(const SourceFn &source, int16_t *out, int maxFrames);

  private:
    static constexpr int kChannels = 2;
    static constexpr int kFrameSize = 1024;  // analysis/synthesis window, frames
    static constexpr int kSynthHop = kFrameSize / 2;
    // WSOLA search: kept deliberately small since this runs on a 268 MHz
    // ARM11 with no float SIMD. A short mono-downmixed comparison window is
    // enough to avoid most phase-cancellation artifacts at a fraction of the
    // cost of comparing the full hop in stereo.
    static constexpr int kSearchRadius = 64;
    static constexpr int kCompareLen = 128;
    static constexpr int kCompactThreshold = kFrameSize * 8;

    void recomputeAlpha();

    // WSOLA time-stretch stage (preserves pitch, changes duration)
    float speed_ = 1.0f;
    float pitch_ = 1.0f;
    float alpha_ = 1.0f;  // analysisHop / synthesisHop = speed_ / pitch_

    std::vector<int16_t> wsolaIn_;  // buffered raw input, not yet consumed
    bool wsolaSourceEOF_ = false;
    double analysisPos_ = 0.0;  // next ideal analysis frame pos, rel. to wsolaIn_[0]
    bool haveTail_ = false;
    std::vector<int32_t> lastTail_;  // mono (L+R) downmix of the previous frame's overlap region
    std::vector<float> accum_;       // overlap-add accumulator, kFrameSize*ch
    std::vector<int16_t> wsolaOut_;  // finalized output not yet consumed by stage B
    size_t wsolaOutPos_ = 0;         // read cursor into wsolaOut_
    std::vector<float> window_;      // periodic Hann, kFrameSize

    bool wsolaEnsureInput(const SourceFn &source, double upToPos);
    bool wsolaProduceHop(const SourceFn &source);
    int wsolaPull(const SourceFn &source, int16_t *out, int maxFrames);
    bool wsolaFinalizing_ = false;

    // Linear-interpolation resample stage (changes pitch, preserves
    // the duration produced by the WSOLA stage)
    std::vector<int16_t> resIn_;  // buffered WSOLA output, not yet consumed
    double resPos_ = 0.0;         // fractional read position into resIn_
    bool resDrained_ = false;

    bool resEnsureInput(const SourceFn &source, double upToPos);
    int resamplePull(const SourceFn &source, int16_t *out, int maxFrames);
};

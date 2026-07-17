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
// with no extra CPU cost or latency, unless the pipeline has already been
// engaged this song (see pipelineEngaged_), in which case it keeps running
// through the pipeline so nothing buffered gets silently dropped.
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
    // WSOLA search: a mono-downmixed comparison keeps this affordable on a
    // 268 MHz ARM11 with no float SIMD. kCompareLen/kSearchRadius need to
    // span at least one pitch period of typical speech (down to ~100 Hz,
    // i.e. ~440 samples at 44.1 kHz) or the search can't find a phase-aligned
    // splice point, which shows up as a periodic buzz/static riding on voice
    // audio (most audible on audiobooks/podcasts). kCompareStride subsamples
    // the comparison so the window can be widened without a proportional
    // rise in per-hop cost.
    static constexpr int kSearchRadius = 320;
    static constexpr int kCompareLen = 576;
    static constexpr int kCoarseStride = 5;
    static constexpr int kFineRadius = 8;
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

    void wsolaEnsureInput(const SourceFn &source, double upToPos);
    bool wsolaProduceHop(const SourceFn &source);
    int wsolaPull(const SourceFn &source, int16_t *out, int maxFrames);
    bool wsolaFinalizing_ = false;

    // Linear-interpolation resample stage (changes pitch, preserves
    // the duration produced by the WSOLA stage)
    std::vector<int16_t> resIn_;  // buffered WSOLA output, not yet consumed
    double resPos_ = 0.0;         // fractional read position into resIn_
    bool resDrained_ = false;

    void resEnsureInput(const SourceFn &source, double upToPos);
    int resamplePull(const SourceFn &source, int16_t *out, int maxFrames);

    // True once process() has pulled anything through the WSOLA/resample
    // pipeline since the last reset(). While set, process() keeps using
    // that pipeline even if speed/pitch briefly return to identity, instead
    // of bouncing to the raw passthrough, which would silently drop
    // whatever lookahead audio is already buffered mid-pipeline.
    bool pipelineEngaged_ = false;
};

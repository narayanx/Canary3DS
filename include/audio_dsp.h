#pragma once

#include <cstdint>
#include <functional>

#include "SoundTouch.h"

// Real-time speed/pitch processor for interleaved 16-bit stereo PCM, backed
// by SoundTouch (WSOLA time-stretch + linear-interpolation rate transpose).
//
// setSpeed() changes playback speed while preserving pitch. setPitch()
// changes pitch while preserving speed. Both can be set independently and
// combined; when both are at their default of 1.0f, process() is a direct
// passthrough to `source` with no extra CPU cost or latency, unless the
// pipeline has already been engaged this song (see pipelineEngaged_), in
// which case it keeps running through SoundTouch so nothing buffered gets
// silently dropped.
class SpeedPitchProcessor {
  public:
    // Matches IAudioDecoder::decode()'s contract: decode up to maxFrames
    // interleaved stereo frames into buffer, return frames produced (>0),
    // 0 on EOF, or <0 on error.
    using SourceFn = std::function<int(int16_t *buffer, int maxFrames)>;

    SpeedPitchProcessor();

    void setSpeed(float speed);  // 1.0 = normal, >1 faster, <1 slower
    void setPitch(float ratio);  // 1.0 = normal, 2^(semitones/12) per octave

    // Discard all buffered audio/history and (re)configure for a stream at
    // sampleRate. Call on seek or song change; SoundTouch needs the true
    // rate to size its analysis windows correctly.
    void reset(int sampleRate = 48000);

    // True when speed == pitch == 1.0 (identity; process() is a direct
    // passthrough to `source`).
    bool isIdentity() const;

    // Produce up to maxFrames processed stereo frames into `out`, pulling
    // raw PCM from `source` as needed. Returns frames written; 0 only once
    // `source` has reported EOF and all buffered/pending audio is drained.
    int process(const SourceFn &source, int16_t *out, int maxFrames);

  private:
    static constexpr int kChannels = 2;
    // Frames pulled from `source` per iteration while topping up SoundTouch.
    static constexpr int kSourceReadChunk = 512;

    // Applies speed_/pitch_ into st_ if they've changed since the last call.
    // Only ever invoked from process(), which always runs on the audio
    // thread, so st_'s setTempo()/setPitch() (which may reallocate internal
    // filter state) are never touched concurrently with putSamples()/
    // receiveSamples(). setSpeed()/setPitch() themselves just write plain
    // floats and may be called from either thread (same relaxed-consistency
    // convention as the rest of AudioController's settings-derived state).
    void applyPending();

    soundtouch::SoundTouch st_;
    int sampleRate_ = 48000;

    float speed_ = 1.0f;
    float pitch_ = 1.0f;
    float appliedSpeed_ = 1.0f;
    float appliedPitch_ = 1.0f;

    bool sourceEOF_ = false;

    // True once process() has pulled anything through the SoundTouch
    // pipeline since the last reset(). While set, process() keeps using
    // that pipeline even if speed/pitch briefly return to identity, instead
    // of bouncing to the raw passthrough, which would silently drop
    // whatever lookahead audio is already buffered mid-pipeline.
    bool pipelineEngaged_ = false;
};

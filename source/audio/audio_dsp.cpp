#include "audio_dsp.h"

#include <algorithm>
#include <cmath>

SpeedPitchProcessor::SpeedPitchProcessor() {
    st_.setChannels((uint) kChannels);
    st_.setSampleRate((uint) sampleRate_);
    // Trades a small amount of search quality for reduced CPU cost.
    st_.setSetting(SETTING_USE_QUICKSEEK, 1);
}

void SpeedPitchProcessor::setSpeed(float speed) {
    speed = std::clamp(speed, 0.25f, 4.0f);
    if (std::abs(speed_ - speed) < 1e-6f) {
        return;
    }
    speed_ = speed;
}

void SpeedPitchProcessor::setPitch(float ratio) {
    ratio = std::clamp(ratio, 0.25f, 4.0f);
    if (std::abs(pitch_ - ratio) < 1e-6f) {
        return;
    }
    pitch_ = ratio;
}

bool SpeedPitchProcessor::isIdentity() const {
    return std::abs(speed_ - 1.0f) < 1e-4f && std::abs(pitch_ - 1.0f) < 1e-4f;
}

void SpeedPitchProcessor::applyPending() {
    if (speed_ != appliedSpeed_) {
        st_.setTempo(speed_);
        appliedSpeed_ = speed_;
    }
    if (pitch_ != appliedPitch_) {
        st_.setPitch(pitch_);
        appliedPitch_ = pitch_;
    }
}

void SpeedPitchProcessor::reset(int sampleRate) {
    st_.clear();
    if (sampleRate != sampleRate_) {
        sampleRate_ = sampleRate;
        st_.setSampleRate((uint) sampleRate_);
    }
    sourceEOF_ = false;
    pipelineEngaged_ = false;
}

int SpeedPitchProcessor::process(const SourceFn &source, int16_t *out, int maxFrames) {
    applyPending();

    if (isIdentity() && !pipelineEngaged_) {
        return source(out, maxFrames);
    }
    pipelineEngaged_ = true;

    int produced = 0;
    while (produced < maxFrames) {
        uint got =
            st_.receiveSamples((soundtouch::SAMPLETYPE *) (out + (size_t) produced * kChannels),
                               (uint) (maxFrames - produced));
        produced += (int) got;
        if (got > 0) {
            continue;  // keep draining while there's ready output
        }
        if (sourceEOF_) {
            break;  // fully drained, nothing left to feed
        }

        int16_t chunk[kSourceReadChunk * kChannels];
        int readGot = source(chunk, kSourceReadChunk);
        if (readGot <= 0) {
            sourceEOF_ = true;
            st_.flush();
            continue;
        }
        st_.putSamples((const soundtouch::SAMPLETYPE *) chunk, (uint) readGot);
    }
    return produced;
}

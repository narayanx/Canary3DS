#include "audio_decoder.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

// Forward declarations – each decoder is defined in its own translation unit.
std::unique_ptr<IAudioDecoder> makeOpusDecoder();
std::unique_ptr<IAudioDecoder> makeMp3Decoder();
std::unique_ptr<IAudioDecoder> makeFlacDecoder();
std::unique_ptr<IAudioDecoder> makeVorbisDecoder();
std::unique_ptr<IAudioDecoder> makeWavDecoder();
std::unique_ptr<IAudioDecoder> makeAacDecoder();

static std::string getExtension(const std::string &path) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) {
        return "";
    }
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return (char) std::tolower(c);
    });
    return ext;
}

std::unique_ptr<IAudioDecoder> createDecoder(const std::string &path) {
    const std::string ext = getExtension(path);

    if (ext == "opus") {
        return makeOpusDecoder();
    }
    if (ext == "mp3") {
        return makeMp3Decoder();
    }
    if (ext == "flac") {
        return makeFlacDecoder();
    }
    if (ext == "ogg" || ext == "oga") {
        return makeVorbisDecoder();
    }
    if (ext == "wav") {
        return makeWavDecoder();
    }
    if (ext == "m4a" || ext == "aac") {
        return makeAacDecoder();
    }
    return nullptr;
}

bool isSupportedAudioFile(const std::string &filename) {
    const std::string ext = getExtension(filename);
    return ext == "opus" || ext == "mp3" || ext == "flac" || ext == "ogg" || ext == "oga" ||
           ext == "wav" || ext == "m4a" || ext == "aac";
}

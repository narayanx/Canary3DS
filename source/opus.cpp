#include "opus.h"

#include <3ds.h>
#include <opusfile.h>

#include <cstring>
#include <string>

#include "filebrowser.h"
#include "gfx.h"

ndspWaveBuf s_waveBufs[3];
int16_t *s_audioBuffer = NULL;

OpusController opusController = {
    .songPath = "",
    .file = nullptr,
    .songReady = false,  // also can be used to check if song is playing
    .stopPlayback = false,
    .interrupted = false,  // don't autoplay next if user stopped song
    .startEvent = {0},
    .doneEvent = {0},
    .fillBufferEvent = {0}};

volatile bool runThreads = true;

// SOURCE 3ds-examples/audio/opus-decoding START
// Retrieve strings for libopusfile errors
// Sourced from David Gow's example code: https://davidgow.net/files/opusal.cpp
const char *opusStrError(int error) {
    switch (error) {
        case OP_FALSE:
            return "OP_FALSE: A request did not succeed.";
        case OP_HOLE:
            return "OP_HOLE: There was a hole in the page sequence numbers.";
        case OP_EREAD:
            return "OP_EREAD: An underlying read, seek or tell operation "
                   "failed.";
        case OP_EFAULT:
            return "OP_EFAULT: A NULL pointer was passed where none was "
                   "expected, or an internal library error was encountered.";
        case OP_EIMPL:
            return "OP_EIMPL: The stream used a feature which is not "
                   "implemented.";
        case OP_EINVAL:
            return "OP_EINVAL: One or more parameters to a function were "
                   "invalid.";
        case OP_ENOTFORMAT:
            return "OP_ENOTFORMAT: This is not a valid Ogg Opus stream.";
        case OP_EBADHEADER:
            return "OP_EBADHEADER: A required header packet was not properly "
                   "formatted.";
        case OP_EVERSION:
            return "OP_EVERSION: The ID header contained an unrecognised "
                   "version number.";
        case OP_EBADPACKET:
            return "OP_EBADPACKET: An audio packet failed to decode properly.";
        case OP_EBADLINK:
            return "OP_EBADLINK: We failed to find data we had seen before or "
                   "the stream was sufficiently corrupt that seeking is "
                   "impossible.";
        case OP_ENOSEEK:
            return "OP_ENOSEEK: An operation that requires seeking was "
                   "requested on an unseekable stream.";
        case OP_EBADTIMESTAMP:
            return "OP_EBADTIMESTAMP: The first or last granule position of a "
                   "link failed basic validity checks.";
        default:
            return "Unknown error.";
    }
}

// Main audio decoding logic
// This function pulls and decodes audio samples from opusFile_ to fill waveBuf_
bool fillBuffer(OggOpusFile *opusFile_, ndspWaveBuf *waveBuf_) {
#ifdef DEBUG
    // Setup timer for performance stats
    TickCounter timer;
    osTickCounterStart(&timer);
#endif  // DEBUG

    // Decode samples until our waveBuf is full
    int totalSamples = 0;
    while (totalSamples < SAMPLES_PER_BUF) {
        int16_t *buffer = waveBuf_->data_pcm16 + (totalSamples * CHANNELS_PER_SAMPLE);
        const size_t bufferSize = (SAMPLES_PER_BUF - totalSamples) * CHANNELS_PER_SAMPLE;

        // Decode bufferSize samples from opusFile_ into buffer,
        // storing the number of samples that were decoded (or error)
        const int samples = op_read_stereo(opusFile_, buffer, bufferSize);
        if (samples <= 0) {
            if (samples == 0) break;  // No error here

            // TODO change to logToBottomScreen
            printf("op_read_stereo: error %d (%s)", samples, opusStrError(samples));
            break;
        }

        totalSamples += samples;
    }

    // If no samples were read in the last decode cycle, we're done
    if (totalSamples == 0) {
        // printf("Playback complete, press Start to exit\n");
        return false;
    }

    // Pass samples to NDSP
    waveBuf_->nsamples = totalSamples;
    ndspChnWaveBufAdd(0, waveBuf_);
    DSP_FlushDataCache(waveBuf_->data_pcm16, totalSamples * CHANNELS_PER_SAMPLE * sizeof(int16_t));

#ifdef DEBUG
    PrintConsole *prev = consoleSelect(&bottomConsole);
    // Print timing info
    osTickCounterUpdate(&timer);
    printf("fillBuffer %lfms in %lfms\n", totalSamples * 1000.0 / SAMPLE_RATE,
           osTickCounterRead(&timer));
    consoleSelect(prev);
#endif  // DEBUG

    return true;
}

// Pause until user presses a button
void waitForInput(void) {
    logToBottomScreen("Press any button to exit...\n");
    while (aptMainLoop()) {
        // gspWaitForVBlank();
        // gfxSwapBuffers();
        hidScanInput();

        if (hidKeysDown()) break;
    }
}

// Audio initialisation code
// This sets up NDSP and our primary audio buffer
bool audioInit(void) {
    // Setup NDSP
    ndspChnReset(0);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, SAMPLE_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    // Allocate audio buffer
    const size_t bufferSize = WAVEBUF_SIZE * ARRAY_SIZE(s_waveBufs);
    s_audioBuffer = (int16_t *)linearAlloc(bufferSize);
    if (!s_audioBuffer) {
        logToBottomScreen("Failed to allocate audio buffer\n");
        return false;
    }

    // Setup waveBufs for NDSP
    memset(&s_waveBufs, 0, sizeof(s_waveBufs));
    int16_t *buffer = s_audioBuffer;

    for (size_t i = 0; i < ARRAY_SIZE(s_waveBufs); ++i) {
        s_waveBufs[i].data_vaddr = buffer;
        s_waveBufs[i].status = NDSP_WBUF_DONE;

        buffer += WAVEBUF_SIZE / sizeof(buffer[0]);
    }

    return true;
}

// Audio de-initialisation code
// Stops playback and frees the primary audio buffer
void audioExit(void) {
    ndspChnReset(0);
    linearFree(s_audioBuffer);
}
// SOURCE 3ds-examples/audio/opus-decoding END

void audioThread(void *arg) {
    while (runThreads) {
        // wait until a song is ready to play
        LightEvent_Wait(&opusController.startEvent);

        // failsafe if somehow event is signaled when it shouldn't be
        if (!opusController.songReady) {
            continue;
        }

        OggOpusFile *file = opusController.file;

        while (runThreads && !opusController.stopPlayback) {
            for (size_t i = 0; i < ARRAY_SIZE(s_waveBufs); ++i) {
                if (s_waveBufs[i].status != NDSP_WBUF_DONE) {
                    continue;
                }

                // fill the buffer with audio data
                if (!fillBuffer(file, &s_waveBufs[i])) {
                    opusController.songReady = false;  // song finished playing
                    // LightEvent_Signal(&opusController.doneEvent);
                    // get outside of while loop until next song is played
                    opusController.stopPlayback = true;
                    break;
                }
            }
            LightEvent_Wait(&opusController.fillBufferEvent);
        }
        // reset flags
        op_free(opusController.file);
        opusController.file = nullptr;
        opusController.songReady = false;
        opusController.stopPlayback = false;

        LightEvent_Signal(&opusController.doneEvent);  // signal that playback is done
    }
}

bool playSong(std::string path) {
    opusController.songPath = path;

    int error = 0;
    opusController.file = op_open_file(opusController.songPath.c_str(), &error);
    if (error || opusController.file == nullptr) {
        // TODO maybe have some sort of logging system, maybe log to file later?
        logToBottomScreen(
            ("Error opening file: " + (std::string)(opusStrError(error)) + '\n').c_str());
        return false;
    }
    opusController.songReady = true;
    opusController.stopPlayback = false;

    // signal to start playing song
    LightEvent_Signal(&opusController.startEvent);

    return true;
}

void opusCallback(void *arg) {
    (void)arg;  // suppress unused parameter warning

    if (!runThreads) {
        return;
    }

    LightEvent_Signal(&opusController.fillBufferEvent);
}

void stopPlaybackIfPlaying() {
    if (opusController.songReady) {
        // if song is already playing, stop playback
        opusController.stopPlayback = true;
        opusController.interrupted = true;
    }
}

void playNextThread(void *arg) {
    while (runThreads) {
        LightEvent_Wait(&opusController.doneEvent);
        if (opusController.interrupted) {
            // TODO allow more verbose logging with a VERBOSE flag (user can set)
            // logToBottomScreen("not autoplaying next because user interrupted playback");
            // user interrupted playback, so we don't play the next song
            opusController.interrupted = false;
            continue;
        }
        if (fileController.playingFile < fileController.files.size() - 1) {
            size_t nextSongIdx = fileController.playingFile + 1;
            std::string nextSongPath =
                fileController.cwd + fileController.files[nextSongIdx].d_name;
            playSong(nextSongPath);
            fileController.playingFile = nextSongIdx;
            logToBottomScreen(
                ("autoplaying: " + (std::string)fileController.files[nextSongIdx].d_name).c_str());
        }
    }
}

const OpusTags *getMetadata(OpusController &controller) {
    // opus_tags_parse
    auto tags = op_tags(controller.file, -1);
    for (int i = 0; i < tags->comments; i++) {
        // TODO maybe parse tags->user_comments[i] to get specific metadata fields
        // like title, artist, album, etc.
        // logToBottomScreen(std::string(tags->user_comments[i], tags->comment_lengths[i]).c_str());
    }
    return op_tags(controller.file, -1);
}

const char *getCoverMetadataBase64(OpusController &controller, size_t &outSize) {
    auto tags = op_tags(controller.file, -1);
    const int COVER_METADATA_TAG_LENGTH = 23;
    for (int i = 0; i < tags->comments; i++) {
        // tag names are case insensitive
        if (strncasecmp(tags->user_comments[i], "METADATA_BLOCK_PICTURE=", COVER_METADATA_TAG_LENGTH) == 0) {
            // don't include METADATA_BLOCK_PICTURE=
            char *base64Data = tags->user_comments[i] + COVER_METADATA_TAG_LENGTH;
            outSize = tags->comment_lengths[i] - COVER_METADATA_TAG_LENGTH;
            // logToBottomScreen(std::string(base64Data, outSize).c_str());
            return base64Data;
        }
    }

    return nullptr;
}

// helper function for parsing vorbis comment
u32 get_u32_be(const std::string& data, size_t index) {
    return (static_cast<u8>(data[index]) << 24) | (static_cast<u8>(data[index + 1]) << 16)
           | (static_cast<u8>(data[index + 2]) << 8) | static_cast<u8>(data[index + 3]);
}

OpusTagData parseMetadata(std::string coverArtMetadata) {
    OpusTagData metadata;
    // source: https://www.ietf.org/archive/id/draft-ietf-cellar-flac-08.pdf
    // (flac cover art metadata is same as opus)
    /* Data   Description
     * u(32)  The picture type according to next table
     * u(32)  The length of the media type string in bytes.
     * u(n*8) The media type string, in printable ASCII characters 0x20-0x7E. The
     * media type MAY also be --> to signify that the data part is a URI of the
     * picture instead of the picture data itself.
     * u(32)  The length of the description string in bytes.
     * u(n*8) The description of the picture, in UTF-8.
     * u(32)  The width of the picture in pixels.
     * u(32)  The height of the picture in pixels.
     * u(32)  The color depth of the picture in bits-per-pixel.
     * u(32)  For indexed-color pictures (e.g. GIF), the number of colors used, or 0
     * for non-indexed pictures.
     * u(32)  The length of the picture data in bytes.
     * u(n*8) The binary picture data. */
    // parse metadata START (10/18/25)
    // big endian

    metadata.pictureType = get_u32_be(coverArtMetadata, 0);
    metadata.mediaStringByteLen = get_u32_be(coverArtMetadata, 4);
    for (u32 i = 0; i < metadata.mediaStringByteLen; i++) {
        metadata.mediaType += coverArtMetadata[8 + i];
    }
    metadata.pictureDescriptionByteLen =
        get_u32_be(coverArtMetadata, 8 + metadata.mediaStringByteLen);
    for (u32 i = 0; i < metadata.pictureDescriptionByteLen; i++) {
        metadata.pictureDescription += coverArtMetadata[12 + metadata.mediaStringByteLen + i];
    }
    metadata.pictureWidth = get_u32_be(
        coverArtMetadata, 12 + metadata.mediaStringByteLen + metadata.pictureDescriptionByteLen);
    metadata.pictureHeight = get_u32_be(
        coverArtMetadata, 16 + metadata.mediaStringByteLen + metadata.pictureDescriptionByteLen);
    metadata.colorDepthBits = get_u32_be(
        coverArtMetadata, 20 + metadata.mediaStringByteLen + metadata.pictureDescriptionByteLen);
    metadata.numColorsUsed = get_u32_be(
        coverArtMetadata, 24 + metadata.mediaStringByteLen + metadata.pictureDescriptionByteLen);

    metadata.pictureDataByteLen = get_u32_be(
        coverArtMetadata, 28 + metadata.mediaStringByteLen + metadata.pictureDescriptionByteLen);
    metadata.pictureByteOffset = 32 + metadata.mediaStringByteLen + metadata.pictureDescriptionByteLen;

    metadata.coverArtDisplay =
        coverArtMetadata.substr(metadata.pictureByteOffset, metadata.pictureDataByteLen);
    return metadata;
}

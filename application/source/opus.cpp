#include "opus.h"

ndspWaveBuf s_waveBufs[3];
int16_t *s_audioBuffer = NULL;

OpusController opus_controller = {
    .songPath = "",
    .file = nullptr,
    .songReady = false,  // also can be used to check if song is playing
    .stopPlayback = false,
    .interrupted = false,  // don't autoplay next if user stopped song
    .startEvent = {0},
    .doneEvent = {0},
    .fillBufferEvent = {0}};
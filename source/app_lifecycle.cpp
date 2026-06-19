#include "app_lifecycle.h"

#include <3ds.h>
#include <citro2d.h>

#include "audio_engine.h"
#include "gfx.h"
#include "settings.h"

static Thread g_audioTid;

bool appInit() {
    romfsInit();
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    sceneInit();

    // lets us keep playing when 3ds is closed
    aptSetSleepAllowed(false);
    ndspInit();

    LightEvent_Init(&audioController.startEvent, RESET_ONESHOT);
    LightEvent_Init(&audioController.fillBufferEvent, RESET_ONESHOT);

    // Load settings before everything else that depends on them
    loadSettings();
    aptSetSleepAllowed(g_settings.sleepAllowed);

    // TODO add a msg telling ppl how to dump with luma3ds (likely bc dspfirm isn't dumped)
    if (!audioInit()) {
        logToDebugScreen("Failed to init audio");
        waitForInput();
        gfxExit();
        ndspExit();
        romfsExit();
        return false;
    }
    applyVolume();  // push saved volume to NDSP
    applyBrightness();
    applyAccentColor();
    applySecondaryColor();

    int32_t mainPrio = 0x30;
    svcGetThreadPriority(&mainPrio, CUR_THREAD_HANDLE);
    g_audioTid = threadCreate(
        audioThread, nullptr, AUDIO_THREAD_STACK_SZ, mainPrio - 1, AUDIO_THREAD_AFFINITY, false);

    ndspSetCallback(audioCallback, nullptr);
    return true;
}

void appExit() {
    restoreBrightness();

    if (audioController.songReady) {
        audioController.stopPlayback = true;
    }
    audioController.interrupted = true;
    runThreads = false;
    LightEvent_Signal(&audioController.startEvent);
    LightEvent_Signal(&audioController.fillBufferEvent);

    threadJoin(g_audioTid, UINT64_MAX);
    threadFree(g_audioTid);

    audioExit();
    ndspExit();
    sceneExit();
    C2D_Fini();
    C3D_Fini();
    romfsExit();
    gfxExit();
}

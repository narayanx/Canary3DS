#pragma once

#include "app_state.h"
#include "ctx_menu.h"

void renderFrame(TopScreenState screenState,
                 FileBrowserState &fb,
                 PlaylistState &pl,
                 InfoState &info,
                 const SettingsState &st,
                 const CtxMenu &s_ctx,
                 const CtxMenu &s_sub,
                 const SpeedPitchPadState &sp,
                 bool showLog,
                 float seekBarX,
                 float seekBarY,
                 float seekBarW,
                 float seekBarH);

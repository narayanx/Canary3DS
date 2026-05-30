#pragma once

#include <3ds.h>

#include "app_state.h"
#include "ctx_menu.h"

// how long to hold before auto-repeat begins (ms)
inline constexpr double REPEAT_INITIAL_DELAY_MS = 400.0;
// interval between each repeated scroll step once repeating (ms)
inline constexpr double REPEAT_INTERVAL_MS = 30.0;

// interval between repeated seek steps (ms)
inline constexpr double SEEK_REPEAT_INTERVAL_MS = 300.0;

inline constexpr int INFO_MAX_VIS_CARD = 10;
inline constexpr int INFO_MAX_VIS = 13;

void handleNavTouch(touchPosition touchPos,
                    bool newTouch,
                    TopScreenState &screenState,
                    PlaylistState &pl,
                    InfoState &info,
                    SettingsState &st,
                    CtxMenu &s_ctx,
                    CtxMenu &s_sub);

void handleSeekTouch(touchPosition touchPos,
                     bool newTouch,
                     bool screenTouched,
                     bool touchReleased,
                     InfoState &info,
                     float seekBarX,
                     float seekBarY,
                     float seekBarW,
                     float seekBarH);

// Returns updated ctxHandled (false if Y closed the menu with no 3rd item)
bool handleContextMenu(u32 kDown, CtxMenu &s_ctx, CtxMenu &s_sub);

void handleAButton(u32 &kDown,
                   TopScreenState &screenState,
                   FileBrowserState &fb,
                   PlaylistState &pl,
                   InfoState &info,
                   SettingsState &st,
                   CtxMenu &s_ctx,
                   CtxMenu &s_sub);

void handleXButton(u32 kDown,
                   TopScreenState screenState,
                   FileBrowserState &fb,
                   PlaylistState &pl,
                   InfoState &info,
                   CtxMenu &s_ctx,
                   CtxMenu &s_sub);

void handleBButton(u32 kDown, TopScreenState &screenState, FileBrowserState &fb, PlaylistState &pl);

void handleYButton(u32 kDown, TopScreenState &screenState, InfoState &info);

void handleSettingsInput(u32 kDown,
                         TopScreenState screenState,
                         SettingsState &st,
                         InfoState &info,
                         PlaylistState &pl,
                         bool seekLeftRepeat,
                         bool seekRightRepeat);

void handleShoulderTaps(
    u32 kDown, u64 now, u64 &lTapTime, int &lTapCount, u64 &rTapTime, int &rTapCount);

void handleUpNav(u32 kDown,
                 bool upRepeat,
                 u64 &upRepeatMs,
                 TopScreenState screenState,
                 FileBrowserState &fb,
                 PlaylistState &pl,
                 InfoState &info,
                 int minInfoIdx,
                 int maxInfoIdx);

void handleDownNav(u32 kDown,
                   bool downRepeat,
                   u64 &downRepeatMs,
                   TopScreenState screenState,
                   FileBrowserState &fb,
                   PlaylistState &pl,
                   InfoState &info,
                   int maxInfoIdx);

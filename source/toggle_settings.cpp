#include "toggle_settings.h"

#include <3ds.h>

#include "app_state.h"

const ToggleSetting TOGGLE_SETTINGS[] = {
    {&Settings::lockToStartPath,
     SettingsState::ROW_LOCK_START,
     "lock_to_music_folder",
     "Prevent Exiting Music Folder"},
    {&Settings::loopFolder, SettingsState::ROW_REPEAT, "loop_folder", "Loop Folder"},
    {&Settings::showCoverArt,
     SettingsState::ROW_COVER_ART,
     "show_cover_art",
     "Cover Art",
     [](InfoState &info) { info.displayCover = g_settings.showCoverArt; }},
    {&Settings::allowClosedLidPlayback,
     SettingsState::ROW_SLEEP,
     "allow_closed_lid_playback",
     "Play with Lid Closed",
     [](InfoState &) {
         // disallow sleep to allow playback
         aptSetSleepAllowed(!g_settings.allowClosedLidPlayback);
     }},
    {&Settings::autoSwitchToPlayer,
     SettingsState::ROW_AUTO_SWITCH_PLAYER,
     "auto_switch_to_player",
     "Auto Switch to Player Screen"},
    {&Settings::lockShoulderButtons,
     SettingsState::ROW_LOCK_SHOULDER_BUTTONS,
     "lock_shoulder_buttons",
     "Lock Shoulder Buttons"},
    {&Settings::pauseOnHeadphoneDisconnect,
     SettingsState::ROW_PAUSE_ON_HEADPHONE_DISCONNECT,
     "pause_on_headphone_disconnect",
     "Pause On Headphone Disconnect"},
    {&Settings::showDebugScreen, SettingsState::ROW_DEBUG, "show_debug", "Enable Dev Debug Screen"},
};

const size_t TOGGLE_SETTINGS_COUNT = sizeof(TOGGLE_SETTINGS) / sizeof(TOGGLE_SETTINGS[0]);

const ToggleSetting *findToggle(size_t row) {
    for (size_t i = 0; i < TOGGLE_SETTINGS_COUNT; ++i) {
        if (TOGGLE_SETTINGS[i].row == row) {
            return &TOGGLE_SETTINGS[i];
        }
    }
    return nullptr;
}

#pragma once

#include <3ds.h>
#include <citro2d.h>

#include <deque>
#include <dirent.h>
#include <string>
#include <vector>

inline constexpr u32 CLEAR_COLOR = C2D_Color32(0x12, 0x12, 0x12, 0xFF);
inline constexpr u32 BOTTOM_CLEAR_COLOR = C2D_Color32(0x0E, 0x0E, 0x0E, 0xFF);
inline constexpr int MAX_LOG_LINES = 14;  // max lines kept in the log buffer

// Bottom screen nav buttons
inline constexpr float NAV_BTN_Y = 3.0f;
inline constexpr float NAV_BTN_H = 32.0f;
inline constexpr float NAV_BTN_W = 32.0f;
inline constexpr float NAV_BTN_X[4] = {3.0f, 37.0f, 71.0f, 105.0f};
inline constexpr int NAV_BTN_COUNT = 4;

inline constexpr float LOOP_BTN_X = 75.0f;
inline constexpr float LOOP_BTN_Y = 100.0f;
inline constexpr float LOOP_BTN_W = 80.0f;
inline constexpr float LOOP_BTN_H = 22.0f;
inline constexpr float SHUFFLE_BTN_X = 165.0f;  // LOOP_BTN_X + LOOP_BTN_W + 10

inline constexpr float PLAY_BTN_Y = 130.0f;
inline constexpr float PLAY_BTN_H = 22.0f;
inline constexpr float PLAY_BTN_GAP = 6.0f;
inline constexpr float PREV_BTN_W = 48.0f;
inline constexpr float PLAY_PAUSE_W = 56.0f;
inline constexpr float NEXT_BTN_W = 48.0f;
inline constexpr float PREV_BTN_X = 78.0f;  // (320 - (48+6+56+6+48)) / 2
inline constexpr float PLAY_PAUSE_X = PREV_BTN_X + PREV_BTN_W + PLAY_BTN_GAP;
inline constexpr float NEXT_BTN_X = PLAY_PAUSE_X + PLAY_PAUSE_W + PLAY_BTN_GAP;

// Maximum number of rows visible at once before scrolling kicks in
inline constexpr int MAX_CTX_VISIBLE = 8;
inline constexpr int MAX_FILES = 14;

// Accent color palette
inline constexpr u32 ACCENT_COLORS[] = {
    C2D_Color32(0x30, 0x7A, 0xB8, 0xFF),  // 0 Blue (default)
    C2D_Color32(0x20, 0x9A, 0x88, 0xFF),  // 1 Teal
    C2D_Color32(0x7A, 0x44, 0xB0, 0xFF),  // 2 Purple
    C2D_Color32(0xA8, 0x28, 0x28, 0xFF),  // 3 Red
    C2D_Color32(0xC8, 0x60, 0x18, 0xFF),  // 4 Orange
    C2D_Color32(0xB8, 0x40, 0x80, 0xFF),  // 5 Pink
    C2D_Color32(0x98, 0x7A, 0x18, 0xFF),  // 6 Gold
    C2D_Color32(0x99, 0x99, 0x99, 0xFF),  // 7 White
};
inline constexpr int ACCENT_COLOR_COUNT = 8;
inline constexpr const char *ACCENT_COLOR_NAMES[] = {
    "Blue", "Teal", "Purple", "Red", "Orange", "Pink", "Gold", "White"};

// Secondary color palette
inline constexpr u32 SECONDARY_COLORS[] = {
    C2D_Color32(0x33, 0xCC, 0x55, 0xFF),  // 0 Green (default)
    C2D_Color32(0x22, 0xCC, 0xCC, 0xFF),  // 1 Cyan
    C2D_Color32(0xCC, 0xC0, 0x22, 0xFF),  // 2 Yellow
    C2D_Color32(0xCC, 0x55, 0x33, 0xFF),  // 3 Coral
    C2D_Color32(0xAA, 0x66, 0xCC, 0xFF),  // 4 Lavender
    C2D_Color32(0xCC, 0xCC, 0xCC, 0xFF),  // 5 White
};
inline constexpr int SECONDARY_COLOR_COUNT = 6;
inline constexpr const char *SECONDARY_COLOR_NAMES[] = {
    "Green", "Cyan", "Yellow", "Coral", "Lavender", "White"};

extern u32 g_accentColor;
extern u32 g_secondaryColor;

extern C2D_TextBuf g_dynamicBuf;
extern C3D_RenderTarget *top, *bottom;
extern C2D_Font g_font;

void sceneInit();
void sceneExit();

void printC2DText(std::string msg, size_t lineOffset = 0);

void printFiles(std::vector<dirent> files,
                size_t selectedFile,
                size_t scrollOffset,
                size_t shownCount,
                size_t lineOffset,
                size_t totalFiles = 0);

void printNowPlayingList(const std::deque<std::string> &history,
                         const std::deque<std::string> &queue,
                         const std::vector<std::string> &autoplay,
                         int selectedVirtualIdx,
                         int topVirtualIdx,
                         const std::string &nowPlayingName,
                         const std::string &nowPlayingArtist,
                         const std::string &nowPlayingTrack);

void printStringList(const std::vector<std::string> &items,
                     size_t selectedIdx,
                     size_t scrollOffset,
                     size_t lineOffset = 0);

// selectedIdx : cursor row (A executes this item)
// scrollOffset: first visible row index
void printContextMenu(const std::vector<std::string> &options,
                      size_t selectedIdx,
                      size_t scrollOffset,
                      float anchorX,
                      float anchorY);

// progress is clamped to [0, 1].
void drawProgressBar(float x, float y, float w, float h, float progress);

// draw "M:SS / M:SS" (if durationSeconds <= 0 only the position is shown)
void drawTimeText(double positionSeconds,
                  double durationSeconds,
                  float x,
                  float y,
                  float scaleX = 0.45f,
                  float scaleY = 0.45f);

// Append a log message
void logToDebugScreen(const char *message);
void logToDebugScreen(const std::string &message);

// Semi-transparent log overlay drawn on the top screen.
// Call after all other top-screen rendering within the same C3D frame.
void renderLogOverlay();

// Draw the settings screen on the top screen.
void printSettingsMenu(const std::vector<std::string> &items,
                       size_t selectedIdx,
                       size_t scrollOffset);

// Draw a fallback cover art scaled to targetW x targetH.
// nowPlaying=true uses musical-note-square-button, false uses
// music-note-symbol-in-a-rounded-square.
void drawNoteCover(float x, float y, float targetW, float targetH, bool nowPlaying);

// Draw the playlist view: name header, Play/Shuffle buttons, and song list.
// inHeader: cursor is on the button row; headerBtnSel: 0=Play 1=Shuffle.
// coverImage: optional pointer to a loaded cover art image (nullptr = no cover).
void printPlaylistView(const std::string &playlistName,
                       const std::vector<std::string> &songNames,
                       size_t selSong,
                       size_t viewScroll,
                       bool inHeader,
                       int headerBtnSel,
                       C2D_Image *coverImage = nullptr);

// Render the entire bottom screen.  Call this inside C3D_FrameBegin/End after
// C2D_TargetClear(bottom, …) and C2D_SceneBegin(bottom).
// activeTab:            0=Files, 1=Now Playing, 2=Playlists; highlights the
//                       matching nav button.
// seekProgressOverride: when >= 0 the progress bar and timestamp are drawn at
//                       this normalised position (0-1) instead of computing
//                       from positionSeconds/durationSeconds.  Pass -1 (the
//                       default) for normal playback display.
void renderBottomScreen(bool songPlaying,
                        double positionSeconds,
                        double durationSeconds,
                        const std::string &songName,
                        const std::string &songArtist,
                        float seekBarX,
                        float seekBarY,
                        float seekBarW,
                        float seekBarH,
                        float seekProgressOverride = -1.0f,
                        int activeTab = 0,
                        bool loopActive = false);

void clearTextWidthCache();

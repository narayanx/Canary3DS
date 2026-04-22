#ifndef CANARY_GFX_H
#define CANARY_GFX_H

#include <3ds.h>
#include <citro2d.h>
#include <dirent.h>

#include <deque>
#include <string>
#include <vector>

inline constexpr u32 CLEAR_COLOR = C2D_Color32(0x12, 0x12, 0x12, 0xFF);
inline constexpr u32 BOTTOM_CLEAR_COLOR = C2D_Color32(0x0E, 0x0E, 0x0E, 0xFF);
inline constexpr int MAX_LOG_LINES = 16;  // max lines kept in the log buffer

extern C2D_TextBuf   g_dynamicBuf;
extern C3D_RenderTarget *top, *bottom;

void sceneInit();
void sceneExit();

void printC2DText(std::string msg, size_t lineOffset = 0);

void printFiles(std::vector<dirent> files, size_t selectedFile,
                size_t scrollOffset, size_t maxFiles, size_t lineOffset);

void printNowPlayingList(
    const std::deque<std::string>& history,
    const std::deque<std::string>& queue,
    const std::vector<std::string>& autoplay,
    int selectedVirtualIdx,
    int topVirtualIdx,
    const std::string& nowPlayingName,
    const std::string& nowPlayingArtist,
    const std::string& nowPlayingTrack);

void printStringList(const std::vector<std::string>& items,
                     size_t selectedIdx, size_t scrollOffset,
                     size_t lineOffset = 0);

void printContextMenu(const std::vector<std::string>& options,
                      size_t selectedIdx, float anchorX, float anchorY);

// progress is clamped to [0, 1].
void drawProgressBar(float x, float y, float w, float h, float progress);

// draw "M:SS / M:SS" (if durationSeconds <= 0 only the position is shown)
void drawTimeText(double positionSeconds, double durationSeconds,
                  float x, float y, float scaleX = 0.45f, float scaleY = 0.45f);

// Append a log message
void logToBottomScreen(const char* message);
void logToBottomScreen(const std::string& message);

// Semi-transparent log overlay drawn on the top screen.
void renderLogOverlay();

// Render the entire bottom screen.  Call this inside C3D_FrameBegin/End after
// C2D_TargetClear(bottom, …) and C2D_SceneBegin(bottom).
// seekProgressOverride: when >= 0 the progress bar and timestamp are drawn at
//                       this normalised position (0–1) instead of computing
//                       from positionSeconds/durationSeconds.  Pass -1 (the
//                       default) for normal playback display.
void renderBottomScreen(bool              songPlaying,
                        double            positionSeconds,
                        double            durationSeconds,
                        const std::string& songName,
                        const std::string& songArtist,
                        float seekBarX, float seekBarY,
                        float seekBarW, float seekBarH,
                        float seekProgressOverride = -1.0f);

#endif

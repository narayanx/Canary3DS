#ifndef CANARY_GFX_H
#define CANARY_GFX_H

#include <3ds.h>
#include <citro2d.h>
#include <dirent.h>

#include <string>
#include <vector>
#include <deque>

inline constexpr u32 CLEAR_COLOR = C2D_Color32(0x0D, 0x1F, 0x2D, 0xFF);
inline constexpr int MAX_BOTTOM_SCREEN_LINES = 14;

extern C2D_TextBuf g_dynamicBuf;
// PrintConsole topConsole, bottomConsole;
extern C3D_RenderTarget *top, *bottom;

void printC2DText(std::string, size_t);

void logToBottomScreen(const char *message);
void logToBottomScreen(const std::string& message);

void sceneInit(void);

void sceneExit(void);

void printFiles(std::vector<dirent>, size_t, size_t, size_t);

void printQueue(const std::deque<std::string>& queue, size_t lineOffset = 0);

#endif

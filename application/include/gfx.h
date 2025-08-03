#ifndef CANARY_GFX_H
#define CANARY_GFX_H

#include <3ds.h>
#include <citro2d.h>
#include <string>

inline constexpr u32 CLEAR_COLOR = C2D_Color32(0x0D, 0x1F, 0x2D, 0xFF);

extern C2D_TextBuf g_dynamicBuf;
// PrintConsole topConsole, bottomConsole;
extern C3D_RenderTarget *top, *bottom;

void printC2DText(std::string, size_t);

void logToBottomScreen(const char *message);

void sceneInit(void);

void sceneExit(void);

#endif

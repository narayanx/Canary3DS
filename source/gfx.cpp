#include "gfx.h"

#include <3ds.h>
#include <citro2d.h>
#include <dirent.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "constants.h"

C3D_RenderTarget* top    = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

C2D_TextBuf g_dynamicBuf;

// Log buffer (written from any thread, rendered from main thread)
static LightLock          s_logLock;
static bool               s_logLockInited = false;
static std::vector<std::string> s_logLines;

static void ensureLogLock() {
    if (!s_logLockInited) {
        LightLock_Init(&s_logLock);
        s_logLockInited = true;
    }
}

void sceneInit() {
    g_dynamicBuf = C2D_TextBufNew(4096);
    ensureLogLock();
}

void sceneExit() {
    C2D_TextBufDelete(g_dynamicBuf);
}

void logToBottomScreen(const char* message) {
    ensureLogLock();
    LightLock_Lock(&s_logLock);
    s_logLines.emplace_back(message);
    while ((int)s_logLines.size() > MAX_BOTTOM_LOG_LINES)
        s_logLines.erase(s_logLines.begin());
    LightLock_Unlock(&s_logLock);
}

void logToBottomScreen(const std::string& message) {
    logToBottomScreen(message.c_str());
}

// Parse, optimise, and draw a string in one call.
// The buf must outlive C2D_DrawText (it does since we draw immediately).
static void drawStr(const char* str, float x, float y, float z,
                    float sx, float sy, u32 col, int flags = C2D_AlignLeft | C2D_WithColor) {
    char      tmp[192];
    C2D_Text  t;
    snprintf(tmp, sizeof(tmp), "%s", str);
    C2D_TextParse(&t, g_dynamicBuf, tmp);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, flags, x, y, z, sx, sy, col);
}

void printC2DText(std::string msg, size_t lineOffset) {
    C2D_TextBufClear(g_dynamicBuf);
    drawStr(msg.c_str(), 10.0f, 16.0f * (float)lineOffset, 0.5f,
            0.5f, 0.5f, C2D_Color32f(1, 1, 1, 1));
}

void printFiles(std::vector<dirent> files, size_t selectedFile,
                size_t scrollOffset, size_t /*maxFiles*/, size_t lineOffset) {
    const float LINE_H = 16.0f;
    C2D_TextBufClear(g_dynamicBuf);

    size_t iter = 0;
    for (size_t i = scrollOffset;
         i < std::min(files.size(), scrollOffset + (size_t)MAX_FILES); ++i) {

        float y = LINE_H * (iter + lineOffset);

        if (i == selectedFile)
            C2D_DrawRectSolid(0, y - 1, 0.4f, 400, LINE_H,
                              C2D_Color32(0x2D, 0x2D, 0x2D, 0xFF));

        std::string name = files[i].d_name;
        if (files[i].d_type == DT_DIR) name += '/';

        u32 col = (i == selectedFile)
                  ? C2D_Color32f(1, 1, 1, 1)
                  : C2D_Color32f(0.7f, 0.7f, 0.7f, 1);
        drawStr(name.c_str(), 10, y, 0.5f, 0.5f, 0.5f, col);
        ++iter;
    }
}

void printQueue(const std::deque<std::string>& queue, size_t selectedItem,
                size_t lineOffset) {
    if (queue.empty()) return;

    const float START_X  = 210.0f;
    const float BASE_Y   = 8.0f;
    const float LINE_H   = 16.0f;
    const size_t maxVis  = 10;

    C2D_TextBufClear(g_dynamicBuf);
    drawStr("Queue", START_X, BASE_Y, 0.5f, 0.5f, 0.5f,
            C2D_Color32f(0.5f, 0.5f, 0.5f, 1));

    for (size_t i = 0; i < std::min(queue.size(), maxVis); ++i) {
        std::string name = queue[i];
        size_t sl = name.find_last_of('/');
        if (sl != std::string::npos) name = name.substr(sl + 1);
        if (name.length() > 20) name = name.substr(0, 17) + "...";

        float y = BASE_Y + LINE_H * (float)(i + lineOffset);
        if (i == selectedItem)
            C2D_DrawRectSolid(START_X, y - 1, 0.4f, 400 - START_X, LINE_H,
                              C2D_Color32(0x2D, 0x2D, 0x2D, 0xFF));

        u32 col = (i == selectedItem)
                  ? C2D_Color32f(1, 1, 1, 1)
                  : C2D_Color32f(0.7f, 0.7f, 0.7f, 1);
        drawStr(name.c_str(), START_X + 4, y, 0.5f, 0.45f, 0.45f, col);
    }

    if (queue.size() > maxVis)
        drawStr("...", START_X, BASE_Y + LINE_H * (float)(maxVis + 1), 0.5f,
                0.45f, 0.45f, C2D_Color32f(0.4f, 0.4f, 0.4f, 0.8f));
}

void printStringList(const std::vector<std::string>& items, size_t selectedIdx,
                     size_t scrollOffset, size_t lineOffset) {
    if (items.empty()) { printC2DText("(empty)", lineOffset); return; }

    const float LINE_H = 16.0f;
    C2D_TextBufClear(g_dynamicBuf);

    size_t iter = 0;
    for (size_t i = scrollOffset;
         i < std::min(items.size(), scrollOffset + (size_t)MAX_FILES); ++i) {

        float y = LINE_H * (iter + lineOffset);
        if (i == selectedIdx)
            C2D_DrawRectSolid(0, y - 1, 0.4f, 400, LINE_H,
                              C2D_Color32(0x2D, 0x2D, 0x2D, 0xFF));

        u32 col = (i == selectedIdx)
                  ? C2D_Color32f(1, 1, 1, 1)
                  : C2D_Color32f(0.7f, 0.7f, 0.7f, 1);
        drawStr(items[i].c_str(), 10, y, 0.5f, 0.5f, 0.5f, col);
        ++iter;
    }
}

void printContextMenu(const std::vector<std::string>& options, size_t selectedIdx,
                      float anchorX, float anchorY) {
    if (options.empty()) return;

    const float PAD   = 6.0f;
    const float LINE_H = 16.0f;
    const float BOX_W  = 190.0f;
    float BOX_H = LINE_H * (float)options.size() + PAD * 2.0f;
    float BOX_X = std::min(anchorX, 400.0f - BOX_W - 2.0f);
    float BOX_Y = std::min(anchorY, 240.0f - BOX_H - 2.0f);

    C2D_DrawRectSolid(BOX_X + 3, BOX_Y + 3, 0.55f, BOX_W, BOX_H, C2D_Color32(0, 0, 0, 0xB0));
    C2D_DrawRectSolid(BOX_X,     BOX_Y,     0.60f, BOX_W, BOX_H, C2D_Color32(0x1E, 0x1E, 0x1E, 0xF8));

    u32 border = C2D_Color32(0x30, 0x7A, 0xB8, 0xFF);
    C2D_DrawRectSolid(BOX_X,           BOX_Y,           0.65f, BOX_W, 1, border);
    C2D_DrawRectSolid(BOX_X,           BOX_Y + BOX_H-1, 0.65f, BOX_W, 1, border);
    C2D_DrawRectSolid(BOX_X,           BOX_Y,           0.65f, 1, BOX_H, border);
    C2D_DrawRectSolid(BOX_X + BOX_W-1, BOX_Y,           0.65f, 1, BOX_H, border);

    C2D_TextBufClear(g_dynamicBuf);
    for (size_t i = 0; i < options.size(); ++i) {
        bool  sel  = (i == selectedIdx);
        float itemY = BOX_Y + PAD + LINE_H * (float)i;
        if (sel)
            C2D_DrawRectSolid(BOX_X + 1, itemY - 1, 0.62f,
                              BOX_W - 2, LINE_H, C2D_Color32(0x2D, 0x2D, 0x2D, 0xFF));
        u32 col = sel ? C2D_Color32f(1, 1, 1, 1) : C2D_Color32f(0.6f, 0.6f, 0.6f, 1);
        drawStr(options[i].c_str(), BOX_X + PAD, itemY, 0.7f, 0.46f, 0.46f, col);
    }
}

void drawProgressBar(float x, float y, float w, float h, float progress) {
    progress = std::max(0.0f, std::min(1.0f, progress));

    // Track background
    C2D_DrawRectSolid(x, y, 0.5f, w, h, C2D_Color32(0x33, 0x33, 0x33, 0xFF));
    // Filled portion
    if (progress > 0.0f)
        C2D_DrawRectSolid(x, y, 0.55f, w * progress, h,
                          C2D_Color32(0x30, 0x7A, 0xB8, 0xFF));
    // Border
    u32 border = C2D_Color32(0x55, 0x55, 0x55, 0xFF);
    C2D_DrawRectSolid(x,         y,         0.6f, w,   1,   border);
    C2D_DrawRectSolid(x,         y + h - 1, 0.6f, w,   1,   border);
    C2D_DrawRectSolid(x,         y,         0.6f, 1,   h,   border);
    C2D_DrawRectSolid(x + w - 1, y,         0.6f, 1,   h,   border);
    // Thumb circle (drawn as a small square for simplicity)
    float thumbX = x + w * progress - 3.0f;
    C2D_DrawRectSolid(thumbX, y - 2, 0.65f, 6, h + 4,
                      C2D_Color32(0xFF, 0xFF, 0xFF, 0xCC));
}

static std::string fmtTime(double secs) {
    if (secs < 0) secs = 0;
    int s = (int)secs;
    int m = s / 60;
    s %= 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

void drawTimeText(double positionSeconds, double durationSeconds,
                  float x, float y, float scaleX, float scaleY) {
    std::string text;
    if (durationSeconds > 0)
        text = fmtTime(positionSeconds) + " / " + fmtTime(durationSeconds);
    else
        text = fmtTime(positionSeconds);

    C2D_TextBufClear(g_dynamicBuf);
    drawStr(text.c_str(), x, y, 0.5f, scaleX, scaleY,
            C2D_Color32f(0.85f, 0.85f, 0.85f, 1));
}

void renderBottomScreen(bool songPlaying, double positionSeconds,
                        double durationSeconds, const std::string& songName,
                        float seekBarX, float seekBarY,
                        float seekBarW, float seekBarH) {
    C2D_TextBufClear(g_dynamicBuf);

    // Log lines
    {
        LightLock_Lock(&s_logLock);
        std::vector<std::string> lines = s_logLines; // snapshot
        LightLock_Unlock(&s_logLock);

        const float LINE_H = 16.0f;
        for (size_t i = 0; i < lines.size(); ++i) {
            drawStr(lines[i].c_str(), 4, LINE_H * (float)i, 0.5f,
                    0.42f, 0.42f, C2D_Color32f(0.65f, 0.65f, 0.65f, 1));
        }
    }

    // Separator
    C2D_DrawRectSolid(0, 161, 0.5f, 320, 1, C2D_Color32(0x30, 0x30, 0x30, 0xFF));

    if (!songPlaying) {
        // Greyed-out seek bar when nothing is playing
        drawStr("No song playing", 4, 167, 0.5f,
                0.42f, 0.42f, C2D_Color32f(0.35f, 0.35f, 0.35f, 1));
        C2D_DrawRectSolid(seekBarX, seekBarY, 0.5f, seekBarW, seekBarH,
                          C2D_Color32(0x22, 0x22, 0x22, 0xFF));
        return;
    }

    // Song name
    {
        std::string name = songName;
        size_t sl = name.find_last_of('/');
        if (sl != std::string::npos) name = name.substr(sl + 1);
        // Strip extension for cleanliness
        size_t dot = name.rfind('.');
        if (dot != std::string::npos) name = name.substr(0, dot);
        if (name.length() > 38) name = name.substr(0, 35) + "...";

        drawStr(name.c_str(), 4, 165, 0.5f,
                0.44f, 0.44f, C2D_Color32f(0.90f, 0.90f, 0.90f, 1));
    }

    // Time text
    {
        std::string t;
        if (durationSeconds > 0)
            t = fmtTime(positionSeconds) + " / " + fmtTime(durationSeconds);
        else
            t = fmtTime(positionSeconds);

        // Right-align by drawing at a fixed right margin
        drawStr(t.c_str(), 4, 181, 0.5f,
                0.46f, 0.46f, C2D_Color32f(0.80f, 0.80f, 0.80f, 1));
    }

    // Seek bar
    float progress = (durationSeconds > 0)
                     ? (float)(positionSeconds / durationSeconds)
                     : 0.0f;
    drawProgressBar(seekBarX, seekBarY, seekBarW, seekBarH, progress);

    // Touch hint
    drawStr("Touch to seek", 4, seekBarY + seekBarH + 5, 0.5f,
            0.38f, 0.38f, C2D_Color32f(0.35f, 0.35f, 0.35f, 1));
}

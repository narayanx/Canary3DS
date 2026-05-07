#include "gfx.h"

#include <3ds.h>
#include <citro2d.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
C2D_Font g_font = nullptr;

C2D_TextBuf g_dynamicBuf;

static LightLock s_logLock;
static bool s_logLockInited = false;
static std::vector<std::string> s_logLines;

static void ensureLogLock() {
    if (!s_logLockInited) {
        LightLock_Init(&s_logLock);
        s_logLockInited = true;
    }
}

void sceneInit() {
    g_font = C2D_FontLoad("romfs:/font/OpenSans-Semibold.bcfnt");
    g_dynamicBuf = C2D_TextBufNew(4096);
    ensureLogLock();
}

void sceneExit() {
    C2D_TextBufDelete(g_dynamicBuf);
    if (g_font) {
        C2D_FontFree(g_font);
        g_font = nullptr;
    }
}

void logToDebugScreen(const char *message) {
    ensureLogLock();
    LightLock_Lock(&s_logLock);
    s_logLines.emplace_back(message);
    while ((int) s_logLines.size() > MAX_LOG_LINES) {
        s_logLines.erase(s_logLines.begin());
    }
    LightLock_Unlock(&s_logLock);
}

void logToDebugScreen(const std::string &message) {
    logToDebugScreen(message.c_str());
}

// Count UTF-8 code points (characters, not bytes)
static size_t utf8Len(const std::string &s) {
    size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) {
            ++n;
        }
    }
    return n;
}

// Return the first maxChars code points of s as a string.
static std::string utf8Truncate(const std::string &s, size_t maxChars) {
    size_t cp = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if ((((unsigned char) (s[i])) & 0xC0) != 0x80) {
            if (cp == maxChars) {
                return s.substr(0, i);
            }
            ++cp;
        }
    }
    return s;
}

// Split a line into wrapped segments of at most maxChars code points.
static std::vector<std::string> wrapLine(const std::string &line, size_t maxChars) {
    std::vector<std::string> result;
    std::string rem = line;
    while (utf8Len(rem) > maxChars) {
        size_t bytePos = 0, cp = 0;
        size_t lastSpaceByte = std::string::npos;
        while (bytePos < rem.size() && cp < maxChars) {
            if ((((unsigned char) rem[bytePos]) & 0xC0) != 0x80) {
                if (rem[bytePos] == ' ') {
                    lastSpaceByte = bytePos;
                }
                ++cp;
            }
            ++bytePos;
        }
        if (lastSpaceByte != std::string::npos) {
            result.push_back(rem.substr(0, lastSpaceByte));
            rem = rem.substr(lastSpaceByte + 1);
        } else {
            result.push_back(rem.substr(0, bytePos));
            rem = rem.substr(bytePos);
        }
    }
    result.push_back(rem);
    return result;
}

static void drawStr(const char *str,
                    float x,
                    float y,
                    float z,
                    float sx,
                    float sy,
                    u32 col,
                    int flags = C2D_AlignLeft | C2D_WithColor) {
    char tmp[512];
    C2D_Text t;
    snprintf(tmp, sizeof(tmp), "%s", str);
    if (g_font) {
        C2D_TextFontParse(&t, g_font, g_dynamicBuf, tmp);
    } else {
        C2D_TextParse(&t, g_dynamicBuf, tmp);
    }
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, flags, x, y, z, sx, sy, col);
}

void printC2DText(std::string msg, size_t lineOffset) {
    C2D_TextBufClear(g_dynamicBuf);
    drawStr(
        msg.c_str(), 10.0f, 16.0f * (float) lineOffset, 0.5f, 0.5f, 0.5f, C2D_Color32f(1, 1, 1, 1));
}

void printFiles(std::vector<dirent> files,
                size_t selectedFile,
                size_t scrollOffset,
                size_t shownCount,
                size_t lineOffset,
                size_t totalFiles) {
    const float LINE_H = 16.0f;
    C2D_TextBufClear(g_dynamicBuf);

    size_t iter = 0;
    for (size_t i = scrollOffset; i < std::min(shownCount, scrollOffset + (size_t) MAX_FILES);
         ++i) {

        float y = LINE_H * (iter + lineOffset);
        if (i == selectedFile) {
            C2D_DrawRectSolid(0, y - 1, 0.4f, 400, LINE_H, C2D_Color32(0x2D, 0x2D, 0x2D, 0xFF));
        }

        std::string name = files[i].d_name;
        if (files[i].d_type == DT_DIR) {
            name += '/';
        }

        u32 col =
            (i == selectedFile) ? C2D_Color32f(1, 1, 1, 1) : C2D_Color32f(0.7f, 0.7f, 0.7f, 1);
        drawStr(name.c_str(), 10, y, 0.5f, 0.5f, 0.5f, col);
        ++iter;
    }

    // Lazy-load indicator: shown when there are hidden files and the bottom of
    // the current page is within the visible window.
    if (totalFiles > shownCount) {
        const size_t indicatorRow = shownCount;
        if (indicatorRow >= scrollOffset && indicatorRow < scrollOffset + (size_t) MAX_FILES) {
            float y = LINE_H * (float) (indicatorRow - scrollOffset + lineOffset);
            char buf[40];
            snprintf(buf, sizeof(buf), "... +%zu more (down)", totalFiles - shownCount);
            drawStr(buf, 10, y, 0.5f, 0.45f, 0.45f, C2D_Color32f(0.42f, 0.42f, 0.42f, 1));
        }
    }
}

void printNowPlayingList(const std::deque<std::string> &history,
                         const std::deque<std::string> &queue,
                         const std::vector<std::string> &autoplay,
                         int selectedVirtualIdx,
                         int topVirtualIdx,
                         const std::string &nowPlayingName,
                         const std::string &nowPlayingArtist,
                         const std::string &nowPlayingTrack) {
    const float START_X = 210.0f;
    const float BASE_Y = 8.0f;
    const float LINE_H = 16.0f;
    const float CARD_H = 34.0f;
    const float MAX_Y = 237.0f;

    const int histSz = (int) history.size();
    const int qSz = (int) queue.size();
    const int aSz = (int) autoplay.size();

    C2D_TextBufClear(g_dynamicBuf);

    // Fixed section label header
    {
        const char *hdr;
        u32 hdrCol;
        if (topVirtualIdx < 0) {
            hdr = "History";
            hdrCol = C2D_Color32f(0.45f, 0.45f, 0.72f, 1.0f);
        } else if (topVirtualIdx == 0) {
            hdr = "Now Playing";
            hdrCol = C2D_Color32f(0.38f, 0.72f, 0.45f, 1.0f);
        } else if (qSz > 0 && topVirtualIdx <= qSz) {
            hdr = "Queue";
            hdrCol = C2D_Color32f(0.50f, 0.50f, 0.50f, 1.0f);
        } else {
            hdr = "Autoplay";
            hdrCol = C2D_Color32f(0.58f, 0.52f, 0.26f, 1.0f);
        }
        drawStr(hdr, START_X + 4.0f, BASE_Y, 0.5f, 0.44f, 0.44f, hdrCol);
    }

    float y = BASE_Y + LINE_H;
    int virtIdx = topVirtualIdx;

    bool drawnHistNowSep = false;
    bool drawnNowQueueSep = false;
    bool drawnAutoplaySep = false;

    while (y < MAX_Y) {
        if (!drawnHistNowSep && virtIdx == 0 && topVirtualIdx < 0) {
            float ly = y + LINE_H * 0.35f;
            C2D_DrawRectSolid(START_X + 4.0f,
                              ly,
                              0.52f,
                              400.0f - START_X - 8.0f,
                              1.0f,
                              C2D_Color32(0x33, 0x55, 0x33, 0xFF));
            drawnHistNowSep = true;
            y += LINE_H * 0.65f;
            continue;
        }
        if (!drawnNowQueueSep && virtIdx == 1 && topVirtualIdx <= 0 && qSz > 0) {
            float ly = y + LINE_H * 0.20f;
            C2D_DrawRectSolid(START_X + 4.0f,
                              ly,
                              0.52f,
                              400.0f - START_X - 8.0f,
                              1.0f,
                              C2D_Color32(0x44, 0x44, 0x44, 0xFF));
            drawnNowQueueSep = true;
            y += LINE_H * 0.45f;
            continue;
        }
        if (!drawnAutoplaySep && virtIdx == qSz + 1 && qSz > 0 && aSz > 0) {
            float ly = y + LINE_H * 0.20f;
            C2D_DrawRectSolid(START_X + 4.0f,
                              ly,
                              0.52f,
                              400.0f - START_X - 8.0f,
                              1.0f,
                              C2D_Color32(0x55, 0x4A, 0x20, 0xFF));
            drawnAutoplaySep = true;
            y += LINE_H * 0.45f;
            continue;
        }

        if (virtIdx < -histSz || virtIdx > qSz + aSz) {
            break;
        }

        const bool isSelected = (virtIdx == selectedVirtualIdx);

        if (virtIdx == 0) {
            if (y + CARD_H > MAX_Y) {
                break;
            }

            if (isSelected) {
                C2D_DrawRectSolid(START_X,
                                  y - 1.0f,
                                  0.4f,
                                  400.0f - START_X,
                                  CARD_H + 1.0f,
                                  C2D_Color32(0x1A, 0x2E, 0x1A, 0xFF));
            } else {
                C2D_DrawRectSolid(START_X,
                                  y - 1.0f,
                                  0.38f,
                                  400.0f - START_X,
                                  CARD_H + 1.0f,
                                  C2D_Color32(0x16, 0x22, 0x16, 0xFF));
            }

            C2D_DrawRectSolid(
                START_X, y - 1.0f, 0.45f, 2.0f, CARD_H + 1.0f, C2D_Color32(0x33, 0xCC, 0x55, 0xFF));

            const float cardTextX = START_X + 7.0f;
            {
                std::string name = nowPlayingName;
                size_t sl = name.find_last_of('/');
                if (sl != std::string::npos) {
                    name = name.substr(sl + 1);
                }
                size_t dot = name.rfind('.');
                if (dot != std::string::npos) {
                    name = name.substr(0, dot);
                }
                if (utf8Len(name) > 19) {
                    name = utf8Truncate(name, 16) + "...";
                }
                u32 col = isSelected ? C2D_Color32f(0.75f, 1.00f, 0.80f, 1.0f)
                                     : C2D_Color32f(0.55f, 0.85f, 0.62f, 1.0f);
                drawStr(name.c_str(), cardTextX, y + 2.0f, 0.5f, 0.46f, 0.46f, col);
            }
            {
                std::string sub;
                if (!nowPlayingTrack.empty()) {
                    sub = nowPlayingTrack + "  ";
                }
                if (!nowPlayingArtist.empty()) {
                    sub += nowPlayingArtist;
                }
                if (utf8Len(sub) > 22) {
                    sub = utf8Truncate(sub, 19) + "...";
                }
                if (!sub.empty()) {
                    u32 col = isSelected ? C2D_Color32f(0.55f, 0.80f, 0.62f, 1.0f)
                                         : C2D_Color32f(0.38f, 0.58f, 0.44f, 1.0f);
                    drawStr(sub.c_str(), cardTextX, y + 17.0f, 0.5f, 0.40f, 0.40f, col);
                }
            }
            ++virtIdx;
            y += CARD_H;
            continue;
        }

        if (y + LINE_H > MAX_Y) {
            break;
        }

        std::string path;
        const bool inHistory = (virtIdx < 0);
        const bool inAutoplay = (virtIdx > qSz);

        if (inHistory) {
            int hi = -(virtIdx + 1);
            if (hi >= histSz) {
                break;
            }
            path = history[(size_t) hi];
        } else if (inAutoplay) {
            int ai = virtIdx - qSz - 1;
            if (ai >= aSz) {
                break;
            }
            path = autoplay[(size_t) ai];
        } else {
            int qi = virtIdx - 1;
            if (qi >= qSz) {
                break;
            }
            path = queue[(size_t) qi];
        }

        std::string name = path;
        size_t sl = name.find_last_of('/');
        if (sl != std::string::npos) {
            name = name.substr(sl + 1);
        }
        if (utf8Len(name) > 20) {
            name = utf8Truncate(name, 17) + "...";
        }

        if (isSelected) {
            C2D_DrawRectSolid(START_X,
                              y - 1.0f,
                              0.4f,
                              400.0f - START_X,
                              LINE_H,
                              C2D_Color32(0x2D, 0x2D, 0x2D, 0xFF));
        }

        u32 col;
        if (inHistory) {
            col = isSelected ? C2D_Color32f(0.80f, 0.80f, 1.00f, 1.0f)
                             : C2D_Color32f(0.40f, 0.40f, 0.60f, 1.0f);
        } else if (inAutoplay) {
            col = isSelected ? C2D_Color32f(1.00f, 0.95f, 0.60f, 1.0f)
                             : C2D_Color32f(0.52f, 0.47f, 0.24f, 1.0f);
        } else {
            col = isSelected ? C2D_Color32f(1.00f, 1.00f, 1.00f, 1.0f)
                             : C2D_Color32f(0.70f, 0.70f, 0.70f, 1.0f);
        }

        drawStr(name.c_str(), START_X + 4.0f, y, 0.5f, 0.45f, 0.45f, col);
        ++virtIdx;
        y += LINE_H;
    }

    if (virtIdx <= qSz + aSz && y < MAX_Y) {
        drawStr(
            "...", START_X + 4.0f, y, 0.5f, 0.45f, 0.45f, C2D_Color32f(0.35f, 0.35f, 0.35f, 1.0f));
    }
}

void printStringList(const std::vector<std::string> &items,
                     size_t selectedIdx,
                     size_t scrollOffset,
                     size_t lineOffset) {
    if (items.empty()) {
        printC2DText("(empty)", lineOffset);
        return;
    }

    const float LINE_H = 16.0f;
    C2D_TextBufClear(g_dynamicBuf);

    size_t iter = 0;
    for (size_t i = scrollOffset; i < std::min(items.size(), scrollOffset + (size_t) MAX_FILES);
         ++i) {

        float y = LINE_H * (iter + lineOffset);
        if (i == selectedIdx) {
            C2D_DrawRectSolid(0, y - 1, 0.4f, 400, LINE_H, C2D_Color32(0x2D, 0x2D, 0x2D, 0xFF));
        }

        u32 col = (i == selectedIdx) ? C2D_Color32f(1, 1, 1, 1) : C2D_Color32f(0.7f, 0.7f, 0.7f, 1);
        drawStr(items[i].c_str(), 10, y, 0.5f, 0.5f, 0.5f, col);
        ++iter;
    }
}

void printContextMenu(const std::vector<std::string> &options,
                      size_t selectedIdx,
                      size_t scrollOffset,
                      float anchorX,
                      float anchorY) {
    if (options.empty()) {
        return;
    }

    const size_t n = options.size();
    const size_t visible = std::min(n - scrollOffset, (size_t) MAX_CTX_VISIBLE);

    const float PAD = 6.0f;
    const float LINE_H = 16.0f;
    const float BOX_W = 190.0f;
    float BOX_H = LINE_H * (float) visible + PAD * 2.0f;
    float BOX_X = std::min(anchorX, 400.0f - BOX_W - 2.0f);
    float BOX_Y = std::min(anchorY, 240.0f - BOX_H - 2.0f);

    // Shadow + background
    C2D_DrawRectSolid(BOX_X + 3, BOX_Y + 3, 0.55f, BOX_W, BOX_H, C2D_Color32(0, 0, 0, 0xB0));
    C2D_DrawRectSolid(BOX_X, BOX_Y, 0.60f, BOX_W, BOX_H, C2D_Color32(0x1E, 0x1E, 0x1E, 0xF8));

    // Border
    u32 border = C2D_Color32(0x30, 0x7A, 0xB8, 0xFF);
    C2D_DrawRectSolid(BOX_X, BOX_Y, 0.65f, BOX_W, 1, border);
    C2D_DrawRectSolid(BOX_X, BOX_Y + BOX_H - 1, 0.65f, BOX_W, 1, border);
    C2D_DrawRectSolid(BOX_X, BOX_Y, 0.65f, 1, BOX_H, border);
    C2D_DrawRectSolid(BOX_X + BOX_W - 1, BOX_Y, 0.65f, 1, BOX_H, border);

    // Scroll nub on the right edge (only when content overflows)
    if (n > (size_t) MAX_CTX_VISIBLE) {
        const float trackH = BOX_H - PAD * 2.0f;
        const float nubH = std::max(4.0f, trackH * (float) MAX_CTX_VISIBLE / (float) n);
        const float nubFrac = (float) scrollOffset / (float) (n - MAX_CTX_VISIBLE);
        const float nubY = BOX_Y + PAD + (trackH - nubH) * nubFrac;
        C2D_DrawRectSolid(BOX_X + BOX_W - 4.0f,
                          BOX_Y + PAD,
                          0.66f,
                          3.0f,
                          trackH,
                          C2D_Color32(0x22, 0x22, 0x22, 0xFF));
        C2D_DrawRectSolid(
            BOX_X + BOX_W - 4.0f, nubY, 0.67f, 3.0f, nubH, C2D_Color32(0x55, 0x55, 0x55, 0xFF));
    }

    C2D_TextBufClear(g_dynamicBuf);
    for (size_t vi = 0; vi < visible; ++vi) {
        const size_t i = scrollOffset + vi;
        const bool sel = (i == selectedIdx);
        const float itemY = BOX_Y + PAD + LINE_H * (float) vi;

        if (sel) {
            C2D_DrawRectSolid(BOX_X + 1,
                              itemY - 1,
                              0.62f,
                              BOX_W - 2,
                              LINE_H,
                              C2D_Color32(0x2D, 0x2D, 0x2D, 0xFF));
        }

        u32 col = sel ? C2D_Color32f(1, 1, 1, 1) : C2D_Color32f(0.6f, 0.6f, 0.6f, 1);
        drawStr(options[i].c_str(), BOX_X + PAD, itemY, 0.7f, 0.46f, 0.46f, col);

        // Button hint on the right: A tracks the cursor; X/Y are fixed to items 1/2.
        const char *hint = nullptr;
        if (sel) {
            hint = "A";
        } else if (i == 1 && selectedIdx != 1) {
            hint = "X";
        } else if (i == 2 && selectedIdx != 2) {
            hint = "Y";
        }

        if (hint) {
            drawStr(hint,
                    BOX_X + BOX_W - 13.0f,
                    itemY,
                    0.70f,
                    0.38f,
                    0.38f,
                    C2D_Color32f(0.38f, 0.38f, 0.38f, 1));
        }
    }
}

void drawProgressBar(float x, float y, float w, float h, float progress) {
    progress = std::max(0.0f, std::min(1.0f, progress));

    C2D_DrawRectSolid(x, y, 0.5f, w, h, C2D_Color32(0x33, 0x33, 0x33, 0xFF));
    if (progress > 0.0f) {
        C2D_DrawRectSolid(x, y, 0.55f, w * progress, h, C2D_Color32(0x30, 0x7A, 0xB8, 0xFF));
    }
    u32 border = C2D_Color32(0x55, 0x55, 0x55, 0xFF);
    C2D_DrawRectSolid(x, y, 0.6f, w, 1, border);
    C2D_DrawRectSolid(x, y + h - 1, 0.6f, w, 1, border);
    C2D_DrawRectSolid(x, y, 0.6f, 1, h, border);
    C2D_DrawRectSolid(x + w - 1, y, 0.6f, 1, h, border);
    float thumbX = x + w * progress - 3.0f;
    C2D_DrawRectSolid(thumbX, y - 2, 0.65f, 6, h + 4, C2D_Color32(0xFF, 0xFF, 0xFF, 0xCC));
}

static std::string fmtTime(double secs) {
    if (secs < 0) {
        secs = 0;
    }
    int s = (int) secs;
    int m = s / 60;
    s %= 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

void drawTimeText(
    double positionSeconds, double durationSeconds, float x, float y, float scaleX, float scaleY) {
    std::string text = (durationSeconds > 0)
                           ? fmtTime(positionSeconds) + " / " + fmtTime(durationSeconds)
                           : fmtTime(positionSeconds);
    C2D_TextBufClear(g_dynamicBuf);
    drawStr(text.c_str(), x, y, 0.5f, scaleX, scaleY, C2D_Color32f(0.85f, 0.85f, 0.85f, 1));
}

// Log overlay
void renderLogOverlay() {
    LightLock_Lock(&s_logLock);
    std::vector<std::string> lines = s_logLines;
    LightLock_Unlock(&s_logLock);

    // Background rect — leaves a small margin on all sides
    const float PAD = 12.0f;
    const float W = 400.0f - PAD * 2.0f;
    const float H = 240.0f - PAD * 2.0f;
    C2D_DrawRectSolid(PAD, PAD, 0.88f, W, H, C2D_Color32(0x06, 0x06, 0x06, 0xDC));

    u32 border = C2D_Color32(0x2A, 0x2A, 0x2A, 0xFF);
    C2D_DrawRectSolid(PAD, PAD, 0.90f, W, 1, border);
    C2D_DrawRectSolid(PAD, PAD + H - 1, 0.90f, W, 1, border);
    C2D_DrawRectSolid(PAD, PAD, 0.90f, 1, H, border);
    C2D_DrawRectSolid(PAD + W - 1, PAD, 0.90f, 1, H, border);

    C2D_TextBufClear(g_dynamicBuf);

    // Header
    drawStr("Log  (SELECT to hide)",
            PAD + 6.0f,
            PAD + 4.0f,
            0.92f,
            0.40f,
            0.40f,
            C2D_Color32(0x44, 0x44, 0x44, 0xFF));

    if (lines.empty()) {
        drawStr("(empty)",
                PAD + 6.0f,
                PAD + 18.0f,
                0.92f,
                0.40f,
                0.40f,
                C2D_Color32f(0.3f, 0.3f, 0.3f, 1));
        return;
    }

    const float LINE_H = 13.0f;
    const float TEXT_X = PAD + 6.0f;
    const float TEXT_Y = PAD + 18.0f;  // below header
    const float MAX_Y = PAD + H - 4.0f;

    const size_t CHARS_PER_LINE = 50;
    float y = TEXT_Y;
    for (size_t i = 0; i < lines.size() && y < MAX_Y; ++i) {
        for (const auto &seg : wrapLine(lines[i], CHARS_PER_LINE)) {
            if (y + LINE_H > MAX_Y) {
                break;
            }
            drawStr(
                seg.c_str(), TEXT_X, y, 0.92f, 0.38f, 0.38f, C2D_Color32f(0.78f, 0.78f, 0.78f, 1));
            y += LINE_H;
        }
    }
}

// Playlist view: name + Play/Shuffle buttons + song list
void printPlaylistView(const std::string &playlistName,
                       const std::vector<std::string> &songNames,
                       size_t selSong,
                       size_t viewScroll,
                       bool inHeader,
                       int headerBtnSel) {
    const float LINE_H = 16.0f;
    const float BTN_Y = LINE_H;
    const float BTN_H = 14.0f;
    const float BTN1_X = 8.0f;
    const float BTN1_W = 68.0f;
    const float BTN2_X = 84.0f;
    const float BTN2_W = 80.0f;
    // songs start at line 2
    const size_t SONG_ROWS = (size_t) (MAX_FILES - 1);

    C2D_TextBufClear(g_dynamicBuf);

    // Playlist name
    drawStr(playlistName.c_str(), 10.0f, 0.0f, 0.5f, 0.5f, 0.5f, C2D_Color32f(1, 1, 1, 1));

    // Helper to draw one button
    auto drawBtn = [&](float bx, float bw, const char *label, bool sel) {
        u32 bgCol = sel ? C2D_Color32(0x1A, 0x3A, 0x55, 0xFF) : C2D_Color32(0x18, 0x18, 0x18, 0xFF);
        u32 bdCol = sel ? C2D_Color32(0x30, 0x7A, 0xB8, 0xFF) : C2D_Color32(0x33, 0x33, 0x33, 0xFF);
        C2D_DrawRectSolid(bx, BTN_Y, 0.4f, bw, BTN_H, bgCol);
        C2D_DrawRectSolid(bx, BTN_Y, 0.45f, bw, 1, bdCol);
        C2D_DrawRectSolid(bx, BTN_Y + BTN_H - 1, 0.45f, bw, 1, bdCol);
        C2D_DrawRectSolid(bx, BTN_Y, 0.45f, 1, BTN_H, bdCol);
        C2D_DrawRectSolid(bx + bw - 1, BTN_Y, 0.45f, 1, BTN_H, bdCol);
        u32 txtCol =
            sel ? C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f) : C2D_Color32f(0.55f, 0.55f, 0.55f, 1.0f);
        drawStr(label,
                bx + bw * 0.5f,
                BTN_Y + 2.0f,
                0.5f,
                0.44f,
                0.44f,
                txtCol,
                C2D_AlignCenter | C2D_WithColor);
    };

    drawBtn(BTN1_X, BTN1_W, "Play", inHeader && headerBtnSel == 0);
    drawBtn(BTN2_X, BTN2_W, "Shuffle", inHeader && headerBtnSel == 1);

    // hint text to the right of buttons
    drawStr("X=Menu",
            BTN2_X + BTN2_W + 8.0f,
            BTN_Y + 2.0f,
            0.5f,
            0.38f,
            0.38f,
            C2D_Color32f(0.35f, 0.35f, 0.35f, 1.0f));

    // Song list
    if (songNames.empty()) {
        drawStr(
            "(empty)", 10.0f, LINE_H * 2.0f, 0.5f, 0.5f, 0.5f, C2D_Color32f(0.5f, 0.5f, 0.5f, 1));
        return;
    }

    size_t iter = 0;
    for (size_t i = viewScroll; i < std::min(songNames.size(), viewScroll + SONG_ROWS); ++i) {
        float y = LINE_H * (float) (iter + 2);
        bool sel = !inHeader && i == selSong;
        if (sel) {
            C2D_DrawRectSolid(0, y - 1, 0.4f, 400, LINE_H, C2D_Color32(0x2D, 0x2D, 0x2D, 0xFF));
        }
        u32 col = sel ? C2D_Color32f(1, 1, 1, 1) : C2D_Color32f(0.7f, 0.7f, 0.7f, 1);
        drawStr(songNames[i].c_str(), 10.0f, y, 0.5f, 0.5f, 0.5f, col);
        ++iter;
    }
}

// Settings screen
void printSettingsMenu(const std::vector<std::string> &items, size_t selectedIdx) {
    C2D_TextBufClear(g_dynamicBuf);

    const float LINE_H = 20.0f;
    const float X_LABEL = 14.0f;
    const float Y_START = 16.0f;

    // Header
    drawStr("Settings  </>=Change  A=Edit  B=Back",
            X_LABEL,
            2.0f,
            0.5f,
            0.40f,
            0.40f,
            C2D_Color32(0x88, 0x88, 0x88, 0xFF));

    // Separator line
    C2D_DrawRectSolid(X_LABEL, 14.0f, 0.5f, 372.0f, 1.0f, C2D_Color32(0x2A, 0x2A, 0x2A, 0xFF));

    for (size_t i = 0; i < items.size(); ++i) {
        float y = Y_START + LINE_H * (float) i;
        bool sel = (i == selectedIdx);

        if (sel) {
            // Highlight bar
            C2D_DrawRectSolid(
                4.0f, y - 1.0f, 0.45f, 392.0f, LINE_H, C2D_Color32(0x1E, 0x3A, 0x55, 0xFF));
            // Left accent
            C2D_DrawRectSolid(
                4.0f, y - 1.0f, 0.50f, 3.0f, LINE_H, C2D_Color32(0x30, 0x7A, 0xB8, 0xFF));
        }

        u32 col =
            sel ? C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f) : C2D_Color32f(0.65f, 0.65f, 0.65f, 1.0f);
        drawStr(items[i].c_str(), X_LABEL + 4.0f, y + 2.0f, 0.55f, 0.44f, 0.44f, col);
    }
}

// Bottom screen
void renderBottomScreen(bool songPlaying,
                        double positionSeconds,
                        double durationSeconds,
                        const std::string &songName,
                        const std::string &songArtist,
                        float seekBarX,
                        float seekBarY,
                        float seekBarW,
                        float seekBarH,
                        float seekProgressOverride,
                        int activeTab) {
    C2D_TextBufClear(g_dynamicBuf);

    // Nav buttons - always drawn regardless of playback state
    static const char *const TAB_LABELS[4] = {"Fl", "NP", "Pl", "St"};
    for (int i = 0; i < NAV_BTN_COUNT; ++i) {
        const float bx = NAV_BTN_X[i];
        const bool sel = (i == activeTab);
        C2D_DrawRectSolid(bx,
                          NAV_BTN_Y,
                          0.40f,
                          NAV_BTN_W,
                          NAV_BTN_H,
                          sel ? C2D_Color32(0x28, 0x28, 0x28, 0xFF)
                              : C2D_Color32(0x18, 0x18, 0x18, 0xFF));
        // active indicator bar along the bottom edge
        if (sel) {
            C2D_DrawRectSolid(bx,
                              NAV_BTN_Y + NAV_BTN_H - 2.0f,
                              0.45f,
                              NAV_BTN_W,
                              2.0f,
                              C2D_Color32(0x30, 0x7A, 0xB8, 0xFF));
        }
        drawStr(TAB_LABELS[i],
                bx + NAV_BTN_W * 0.5f,
                NAV_BTN_Y + 7.0f,
                0.5f,
                0.42f,
                0.42f,
                sel ? C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f)
                    : C2D_Color32f(0.50f, 0.50f, 0.50f, 1.0f),
                C2D_AlignCenter | C2D_WithColor);
    }

    // Content area begins below the nav bar
    const float TITLE_Y = seekBarY - 28.0f;
    const float META_Y = seekBarY - 14.0f;

    if (!songPlaying) {
        drawStr("No song playing",
                4,
                TITLE_Y,
                0.5f,
                0.42f,
                0.42f,
                C2D_Color32f(0.35f, 0.35f, 0.35f, 1));
        C2D_DrawRectSolid(
            seekBarX, seekBarY, 0.5f, seekBarW, seekBarH, C2D_Color32(0x22, 0x22, 0x22, 0xFF));
        return;
    }

    // Song title
    {
        std::string name = songName;
        size_t sl = name.find_last_of('/');
        if (sl != std::string::npos) {
            name = name.substr(sl + 1);
        }
        size_t dot = name.rfind('.');
        if (dot != std::string::npos) {
            name = name.substr(0, dot);
        }
        if (utf8Len(name) > 38) {
            name = utf8Truncate(name, 35) + "...";
        }
        drawStr(name.c_str(), 4, TITLE_Y, 0.5f, 0.44f, 0.44f, C2D_Color32f(0.90f, 0.90f, 0.90f, 1));
    }

    // Artist + timestamp combined on one line
    {
        double displayPos = (seekProgressOverride >= 0.0f && durationSeconds > 0)
                                ? (double) seekProgressOverride * durationSeconds
                                : positionSeconds;
        std::string t = (durationSeconds > 0)
                            ? fmtTime(displayPos) + " / " + fmtTime(durationSeconds)
                            : fmtTime(displayPos);
        std::string meta;
        if (!songArtist.empty()) {
            std::string artist = songArtist;
            if (utf8Len(artist) > 22) {
                artist = utf8Truncate(artist, 19) + "...";
            }
            meta = artist + "  " + t;
        } else {
            meta = t;
        }
        drawStr(meta.c_str(), 4, META_Y, 0.5f, 0.40f, 0.40f, C2D_Color32f(0.60f, 0.60f, 0.60f, 1));
    }

    float progress =
        (seekProgressOverride >= 0.0f)
            ? seekProgressOverride
            : ((durationSeconds > 0) ? (float) (positionSeconds / durationSeconds) : 0.0f);
    drawProgressBar(seekBarX, seekBarY, seekBarW, seekBarH, progress);
}

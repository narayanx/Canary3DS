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

static C2D_SpriteSheet s_noteSheetNowPlaying = nullptr;
static C2D_SpriteSheet s_noteSheetPlaylist = nullptr;
static C2D_SpriteSheet s_filebrowserIcon = nullptr;
static C2D_SpriteSheet s_playerIcon = nullptr;
static C2D_SpriteSheet s_playlistIcon = nullptr;
static C2D_SpriteSheet s_settingsIcon = nullptr;

u32 g_accentColor = C2D_Color32(0x30, 0x7A, 0xB8, 0xFF);
u32 g_secondaryColor = C2D_Color32(0x33, 0xCC, 0x55, 0xFF);

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
    s_noteSheetNowPlaying = C2D_SpriteSheetLoad("romfs:/musical-note-square-button.t3x");
    s_noteSheetPlaylist = C2D_SpriteSheetLoad("romfs:/music-note-symbol-in-a-rounded-square.t3x");

    s_filebrowserIcon = C2D_SpriteSheetLoad("romfs:/icons/folder-white-24size-1.5weight.t3x");
    s_playerIcon = C2D_SpriteSheetLoad("romfs:/icons/music-note-solid-white-24size-1.5weight.t3x");
    s_playlistIcon = C2D_SpriteSheetLoad("romfs:/icons/playlist-white-24size-1.5weight.t3x");
    s_settingsIcon = C2D_SpriteSheetLoad("romfs:/icons/settings-white-24size-1.5weight.t3x");
}

void sceneExit() {
    C2D_TextBufDelete(g_dynamicBuf);
    if (g_font) {
        C2D_FontFree(g_font);
        g_font = nullptr;
    }
    if (s_noteSheetNowPlaying) {
        C2D_SpriteSheetFree(s_noteSheetNowPlaying);
        s_noteSheetNowPlaying = nullptr;
    }
    if (s_noteSheetPlaylist) {
        C2D_SpriteSheetFree(s_noteSheetPlaylist);
        s_noteSheetPlaylist = nullptr;
    }

    if (s_filebrowserIcon) {
        C2D_SpriteSheetFree(s_filebrowserIcon);
        s_filebrowserIcon = nullptr;
    }
    if (s_playerIcon) {
        C2D_SpriteSheetFree(s_playerIcon);
        s_playerIcon = nullptr;
    }
    if (s_playlistIcon) {
        C2D_SpriteSheetFree(s_playlistIcon);
        s_playlistIcon = nullptr;
    }
    if (s_settingsIcon) {
        C2D_SpriteSheetFree(s_settingsIcon);
        s_settingsIcon = nullptr;
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

static float textWidth(const std::string &s, float sx) {
    C2D_Text t;
    if (g_font) {
        C2D_TextFontParse(&t, g_font, g_dynamicBuf, s.c_str());
    } else {
        C2D_TextParse(&t, g_dynamicBuf, s.c_str());
    }
    C2D_TextOptimize(&t);

    float w, h;
    C2D_TextGetDimensions(&t, sx, sx, &w, &h);
    return w;
}

// Return a string which fits within a certain width
static std::string fitTextWidth(const std::string &s, float maxWidth, float sx) {
    if (textWidth(s, sx) <= maxWidth) {
        return s;
    }

    float ellipsisWidth = textWidth("...", sx);

    // If even "..." is too wide, just return it
    if (ellipsisWidth > maxWidth) {
        return "...";
    }

    float targetWidth = maxWidth - ellipsisWidth;

    // Binary search on code point count (linear scan causes lag)
    size_t totalChars = utf8Len(s);
    size_t lo = 0, hi = totalChars;

    while (lo < hi) {
        size_t mid = lo + (hi - lo + 1) / 2;  // Bias toward upper half
        std::string candidate = utf8Truncate(s, mid);

        if (textWidth(candidate, sx) <= targetWidth) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }

    // lo holds the maximum chars that fit
    std::string result = utf8Truncate(s, lo);
    return result.empty() ? "..." : result + "...";
}

// Split a line into wrapped segments of at most maxChars code points
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
            const float MARGIN = 10.0f;
            // show selected bar across screen (covering up right margin to avoid text overflow in
            // render_frame.cpp)
            C2D_DrawRectSolid(
                400 - MARGIN, y - 1, 0.8f, MARGIN, LINE_H, C2D_Color32(0x2D, 0x2D, 0x2D, 0xFF));
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
            hdrCol = g_secondaryColor;
        } else if (topVirtualIdx == 0) {
            hdr = "Now Playing";
            hdrCol = g_secondaryColor;
        } else if (qSz > 0 && topVirtualIdx <= qSz) {
            hdr = "Queue";
            hdrCol = g_secondaryColor;
        } else {
            hdr = "Autoplay";
            hdrCol = g_secondaryColor;
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
                              C2D_Color32(0x44, 0x44, 0x44, 0xFF));
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
                              C2D_Color32(0x44, 0x44, 0x44, 0xFF));
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

            u8 sR = (u8) (g_secondaryColor & 0xFF);
            u8 sG = (u8) ((g_secondaryColor >> 8) & 0xFF);
            u8 sB = (u8) ((g_secondaryColor >> 16) & 0xFF);
            // get lighter version of color for highlight
            auto lw = [](u8 c, unsigned t) -> u8 { return (u8) (c + ((255u - c) * t >> 8u)); };

            if (isSelected) {
                C2D_DrawRectSolid(START_X,
                                  y - 1.0f,
                                  0.4f,
                                  400.0f - START_X,
                                  CARD_H + 1.0f,
                                  C2D_Color32((u8) (sR / 4), (u8) (sG / 4), (u8) (sB / 4), 0xFF));
            } else {
                C2D_DrawRectSolid(START_X,
                                  y - 1.0f,
                                  0.38f,
                                  400.0f - START_X,
                                  CARD_H + 1.0f,
                                  C2D_Color32((u8) (sR / 5), (u8) (sG / 5), (u8) (sB / 5), 0xFF));
            }

            C2D_DrawRectSolid(START_X, y - 1.0f, 0.45f, 2.0f, CARD_H + 1.0f, g_secondaryColor);

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
                name = fitTextWidth(name, 180.0f, 0.46f);
                u32 col = isSelected ? C2D_Color32(lw(sR, 166), lw(sG, 166), lw(sB, 166), 0xFF)
                                     : C2D_Color32(lw(sR, 102), lw(sG, 102), lw(sB, 102), 0xFF);
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
                sub = fitTextWidth(sub, 180.0f, 0.40f);
                if (!sub.empty()) {
                    u32 col = isSelected ? C2D_Color32(lw(sR, 115), lw(sG, 115), lw(sB, 115), 0xFF)
                                         : C2D_Color32(lw(sR, 51), lw(sG, 51), lw(sB, 51), 0xFF);
                    drawStr(sub.c_str(), cardTextX, y + 17.0f, 0.5f, 0.40f, 0.40f, col);
                }
            }
            ++virtIdx;
            y += CARD_H;
            continue;
        }

        if (y >= MAX_Y) {
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
        name = fitTextWidth(name, 180.0f, 0.45f);

        if (isSelected) {
            C2D_DrawRectSolid(START_X,
                              y - 1.0f,
                              0.4f,
                              400.0f - START_X,
                              LINE_H,
                              C2D_Color32(0x2D, 0x2D, 0x2D, 0xFF));
        }

        u32 col = isSelected ? C2D_Color32f(1.00f, 1.00f, 1.00f, 1.0f)
                             : C2D_Color32f(0.70f, 0.70f, 0.70f, 1.0f);

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
    u32 border = g_accentColor;
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
        C2D_DrawRectSolid(x, y, 0.55f, w * progress, h, g_accentColor);
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

    // Background rect leaves a small margin on all sides
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

    const size_t CHARS_PER_LINE = 80;
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

void drawNoteCover(float x, float y, float targetW, float targetH, bool nowPlaying) {
    C2D_SpriteSheet sheet = nowPlaying ? s_noteSheetNowPlaying : s_noteSheetPlaylist;
    if (!sheet) {
        return;
    }
    C2D_Image img = C2D_SpriteSheetGetImage(sheet, 0);
    if (!img.tex) {
        return;
    }
    float sx = targetW / (float) img.tex->width;
    float sy = targetH / (float) img.tex->height;
    float s = std::min(sx, sy);
    float drawW = (float) img.tex->width * s;
    float drawH = (float) img.tex->height * s;

    float INSET = 6.0f;  // for the rounded corners
    // rect shows through the transparent note area
    C2D_DrawRectSolid(
        x + INSET, y + INSET, 0.29f, drawW - (2 * INSET), drawH - (2 * INSET), g_secondaryColor);
    C2D_DrawImageAt(img, x, y, 0.3f, nullptr, s, s);
}

// Playlist view: name + Play/Shuffle buttons + song list
void printPlaylistView(const std::string &playlistName,
                       const std::vector<std::string> &songNames,
                       size_t selSong,
                       size_t viewScroll,
                       bool inHeader,
                       int headerBtnSel,
                       C2D_Image *coverImage) {
    const float LINE_H = 16.0f;
    const float BTN_H = 14.0f;
    const float BTN1_X = 120.0f;
    const float BTN1_W = 68.0f;
    const float BTN2_X = 196.0f;
    const float BTN2_W = 80.0f;
    const float COVER_TARGET = 132.0f;
    const float COVER_X = (400.0f - COVER_TARGET) * 0.5f;
    const float COVER_Y = 8.0f;
    const float TITLE_Y = COVER_Y + COVER_TARGET + 10.0f;
    const float BTN_Y = TITLE_Y + 18.0f;
    const float SONGS_Y = BTN_Y + BTN_H + 10.0f;
    // Header scrolls off for the first (SONGS_Y/LINE_H) steps; song list offset starts after that.
    const size_t MAX_HEADER_STEPS = (size_t) (SONGS_Y / LINE_H);
    const size_t headerSteps = std::min(viewScroll, MAX_HEADER_STEPS);
    const float HEADER_SCROLL = (float) headerSteps * LINE_H;
    const size_t songOffset = viewScroll - headerSteps;
    const size_t SONG_ROWS =
        std::max((size_t) 1, (size_t) ((240.0f - (SONGS_Y - HEADER_SCROLL)) / LINE_H));

    C2D_TextBufClear(g_dynamicBuf);

    if (coverImage && coverImage->tex) {
        float sx = COVER_TARGET / (float) coverImage->tex->width;
        float sy = COVER_TARGET / (float) coverImage->tex->height;
        float s = std::min(sx, sy);
        C2D_DrawImageAt(*coverImage, COVER_X, COVER_Y - HEADER_SCROLL, 0.3f, nullptr, s, s);
    } else {
        drawNoteCover(COVER_X, COVER_Y - HEADER_SCROLL, COVER_TARGET, COVER_TARGET, false);
    }

    // Playlist name
    drawStr(playlistName.c_str(),
            200.0f,
            TITLE_Y - HEADER_SCROLL,
            0.5f,
            0.5f,
            0.5f,
            C2D_Color32f(1, 1, 1, 1),
            C2D_AlignCenter | C2D_WithColor);

    // Helper to draw one button
    auto drawBtn = [&](float bx, float bw, const char *label, bool sel) {
        u32 bgCol = sel ? C2D_Color32((u8) ((g_accentColor & 0xFF) / 3),
                                      (u8) (((g_accentColor >> 8) & 0xFF) / 3),
                                      (u8) (((g_accentColor >> 16) & 0xFF) / 3),
                                      0xFF)
                        : C2D_Color32(0x18, 0x18, 0x18, 0xFF);
        u32 bdCol = sel ? g_accentColor : C2D_Color32(0x33, 0x33, 0x33, 0xFF);
        C2D_DrawRectSolid(bx, BTN_Y - HEADER_SCROLL, 0.4f, bw, BTN_H, bgCol);
        C2D_DrawRectSolid(bx, BTN_Y - HEADER_SCROLL, 0.45f, bw, 1, bdCol);
        C2D_DrawRectSolid(bx, BTN_Y - HEADER_SCROLL + BTN_H - 1, 0.45f, bw, 1, bdCol);
        C2D_DrawRectSolid(bx, BTN_Y - HEADER_SCROLL, 0.45f, 1, BTN_H, bdCol);
        C2D_DrawRectSolid(bx + bw - 1, BTN_Y - HEADER_SCROLL, 0.45f, 1, BTN_H, bdCol);
        u32 txtCol =
            sel ? C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f) : C2D_Color32f(0.55f, 0.55f, 0.55f, 1.0f);
        drawStr(label,
                bx + bw * 0.5f,
                BTN_Y - HEADER_SCROLL,
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
            BTN_Y - HEADER_SCROLL + 2.0f,
            0.5f,
            0.38f,
            0.38f,
            C2D_Color32f(0.35f, 0.35f, 0.35f, 1.0f));

    // Song list
    if (songNames.empty()) {
        drawStr("(empty)",
                10.0f,
                SONGS_Y - HEADER_SCROLL,
                0.5f,
                0.5f,
                0.5f,
                C2D_Color32f(0.5f, 0.5f, 0.5f, 1));
        return;
    }

    size_t iter = 0;
    for (size_t i = songOffset; i < std::min(songNames.size(), songOffset + SONG_ROWS); ++i) {
        float y = SONGS_Y + LINE_H * (float) iter - HEADER_SCROLL;
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
void printSettingsMenu(const std::vector<std::string> &items,
                       size_t selectedIdx,
                       size_t scrollOffset) {
    C2D_TextBufClear(g_dynamicBuf);

    const float LINE_H = 20.0f;
    const float X_LABEL = 14.0f;
    const float Y_START = 16.0f;
    const size_t VISIBLE = 11;

    // U+E000: A button, U+E001: B button, U+E07B: dpad left, U+E07C: dpad right
    drawStr("Settings  \uE07B/\uE07C=Change  \uE000=Edit  \uE001=Back",
            X_LABEL,
            2.0f,
            0.5f,
            0.40f,
            0.40f,
            C2D_Color32(0x88, 0x88, 0x88, 0xFF));

    // Separator line
    C2D_DrawRectSolid(X_LABEL, 14.0f, 0.5f, 372.0f, 1.0f, C2D_Color32(0x2A, 0x2A, 0x2A, 0xFF));

    size_t safeScroll = (scrollOffset < items.size()) ? scrollOffset : 0;
    size_t end = std::min(items.size(), safeScroll + VISIBLE);

    for (size_t i = safeScroll; i < end; ++i) {
        float y = Y_START + LINE_H * (float) (i - safeScroll);
        bool sel = (i == selectedIdx);

        bool isHeader = (items[i].size() >= 3 && items[i].substr(0, 3) == "---");
        if (isHeader) {
            drawStr(items[i].c_str() + 3,
                    X_LABEL + 4.0f,
                    y + 2.0f,
                    0.55f,
                    0.40f,
                    0.40f,
                    C2D_Color32(0x50, 0x50, 0x50, 0xFF));
            C2D_DrawRectSolid(X_LABEL,
                              y + LINE_H - 1.0f,
                              0.5f,
                              372.0f,
                              1.0f,
                              C2D_Color32(0x28, 0x28, 0x28, 0xFF));
            continue;
        }

        if (sel) {
            u32 hlBg = C2D_Color32((u8) ((g_accentColor & 0xFF) / 3),
                                   (u8) (((g_accentColor >> 8) & 0xFF) / 3),
                                   (u8) (((g_accentColor >> 16) & 0xFF) / 3),
                                   0xFF);
            C2D_DrawRectSolid(4.0f, y - 1.0f, 0.45f, 392.0f, LINE_H, hlBg);
            C2D_DrawRectSolid(4.0f, y - 1.0f, 0.50f, 3.0f, LINE_H, g_accentColor);
        }

        u32 col =
            sel ? C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f) : C2D_Color32f(0.65f, 0.65f, 0.65f, 1.0f);
        drawStr(items[i].c_str(), X_LABEL + 4.0f, y + 2.0f, 0.55f, 0.44f, 0.44f, col);
    }

    // Scroll nub on the right edge when there are more rows than fit.
    if (items.size() > VISIBLE) {
        const float TRACK_X = 396.0f;
        const float TRACK_Y = Y_START;
        const float TRACK_H = LINE_H * (float) VISIBLE;
        const float NUB_H = std::max(8.0f, TRACK_H * (float) VISIBLE / (float) items.size());
        float nubFrac =
            (items.size() > VISIBLE) ? (float) safeScroll / (float) (items.size() - VISIBLE) : 0.0f;
        float NUB_Y = TRACK_Y + (TRACK_H - NUB_H) * nubFrac;
        C2D_DrawRectSolid(
            TRACK_X, TRACK_Y, 0.5f, 3.0f, TRACK_H, C2D_Color32(0x22, 0x22, 0x22, 0xFF));
        C2D_DrawRectSolid(TRACK_X, NUB_Y, 0.5f, 3.0f, NUB_H, C2D_Color32(0x55, 0x55, 0x55, 0xFF));
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
                        int activeTab,
                        bool loopActive) {
    C2D_TextBufClear(g_dynamicBuf);

    // Nav buttons always drawn regardless of playback state
    static const C2D_Image TAB_ICONS[4] = {C2D_SpriteSheetGetImage(s_filebrowserIcon, 0),
                                           C2D_SpriteSheetGetImage(s_playerIcon, 0),
                                           C2D_SpriteSheetGetImage(s_playlistIcon, 0),
                                           C2D_SpriteSheetGetImage(s_settingsIcon, 0)};
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
            C2D_DrawRectSolid(
                bx, NAV_BTN_Y + NAV_BTN_H - 2.0f, 0.45f, NAV_BTN_W, 2.0f, g_accentColor);
        }
        const int ICON_PIXELS = 24;  // icons are 24x24px
        C2D_DrawImageAt(TAB_ICONS[i],
                        bx + NAV_BTN_W * 0.5f - (ICON_PIXELS / 2),
                        NAV_BTN_Y + 3.0f,
                        0.5f,
                        nullptr,
                        1.0f,
                        1.0f);
    }

    // Loop button
    {
        C2D_DrawRectSolid(LOOP_BTN_X,
                          LOOP_BTN_Y,
                          0.40f,
                          LOOP_BTN_W,
                          LOOP_BTN_H,
                          loopActive ? C2D_Color32(0x28, 0x28, 0x28, 0xFF)
                                     : C2D_Color32(0x18, 0x18, 0x18, 0xFF));
        if (loopActive) {
            C2D_DrawRectSolid(
                LOOP_BTN_X, LOOP_BTN_Y + LOOP_BTN_H - 2.0f, 0.45f, LOOP_BTN_W, 2.0f, g_accentColor);
        }
        drawStr("Loop",
                LOOP_BTN_X + LOOP_BTN_W * 0.5f,
                LOOP_BTN_Y + 5.0f,
                0.5f,
                0.44f,
                0.44f,
                loopActive ? C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f)
                           : C2D_Color32f(0.50f, 0.50f, 0.50f, 1.0f),
                C2D_AlignCenter | C2D_WithColor);
    }
    // Shuffle button
    {
        C2D_DrawRectSolid(SHUFFLE_BTN_X,
                          LOOP_BTN_Y,
                          0.40f,
                          LOOP_BTN_W,
                          LOOP_BTN_H,
                          C2D_Color32(0x18, 0x18, 0x18, 0xFF));
        drawStr("Shuffle",
                SHUFFLE_BTN_X + LOOP_BTN_W * 0.5f,
                LOOP_BTN_Y + 5.0f,
                0.5f,
                0.44f,
                0.44f,
                C2D_Color32f(0.50f, 0.50f, 0.50f, 1.0f),
                C2D_AlignCenter | C2D_WithColor);
    }

    // Prev / Play-Pause / Next buttons
    {
        bool isPlaying = songPlaying && !ndspChnIsPaused(0);
        const char *playPauseLabel = isPlaying ? "Pause" : "Play";
        auto drawCtrlBtn = [&](float bx, float bw, const char *label, bool active) {
            C2D_DrawRectSolid(bx,
                              PLAY_BTN_Y,
                              0.40f,
                              bw,
                              PLAY_BTN_H,
                              active ? C2D_Color32(0x28, 0x28, 0x28, 0xFF)
                                     : C2D_Color32(0x18, 0x18, 0x18, 0xFF));
            if (active) {
                C2D_DrawRectSolid(
                    bx, PLAY_BTN_Y + PLAY_BTN_H - 2.0f, 0.45f, bw, 2.0f, g_accentColor);
            }
            u32 bd = C2D_Color32(0x33, 0x33, 0x33, 0xFF);
            C2D_DrawRectSolid(bx, PLAY_BTN_Y, 0.45f, bw, 1, bd);
            C2D_DrawRectSolid(bx, PLAY_BTN_Y + PLAY_BTN_H - 1, 0.45f, bw, 1, bd);
            C2D_DrawRectSolid(bx, PLAY_BTN_Y, 0.45f, 1, PLAY_BTN_H, bd);
            C2D_DrawRectSolid(bx + bw - 1, PLAY_BTN_Y, 0.45f, 1, PLAY_BTN_H, bd);
            u32 tc = active ? C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f)
                            : C2D_Color32f(0.50f, 0.50f, 0.50f, 1.0f);
            drawStr(label,
                    bx + bw * 0.5f,
                    PLAY_BTN_Y + 5.0f,
                    0.5f,
                    0.44f,
                    0.44f,
                    tc,
                    C2D_AlignCenter | C2D_WithColor);
        };
        drawCtrlBtn(PREV_BTN_X, PREV_BTN_W, "<<", false);
        drawCtrlBtn(PLAY_PAUSE_X, PLAY_PAUSE_W, playPauseLabel, isPlaying);
        drawCtrlBtn(NEXT_BTN_X, NEXT_BTN_W, ">>", false);
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
        name = fitTextWidth(name, seekBarW, 0.44f);
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
            float timeW = textWidth("  " + t, 0.40f);
            artist = fitTextWidth(artist, seekBarW - timeW, 0.40f);
            meta = t + "  " + artist;
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

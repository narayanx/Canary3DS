#include "gfx.h"

#include <3ds.h>
#include <citro2d.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

#include "constants.h"

C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

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
    g_dynamicBuf = C2D_TextBufNew(4096);
    ensureLogLock();
}

void sceneExit() {
    C2D_TextBufDelete(g_dynamicBuf);
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

static void drawStr(const char *str,
                    float x,
                    float y,
                    float z,
                    float sx,
                    float sy,
                    u32 col,
                    int flags = C2D_AlignLeft | C2D_WithColor) {
    char tmp[192];
    C2D_Text t;
    snprintf(tmp, sizeof(tmp), "%s", str);
    C2D_TextParse(&t, g_dynamicBuf, tmp);
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
                size_t /*maxFiles*/,
                size_t lineOffset) {
    const float LINE_H = 16.0f;
    C2D_TextBufClear(g_dynamicBuf);

    size_t iter = 0;
    for (size_t i = scrollOffset; i < std::min(files.size(), scrollOffset + (size_t) MAX_FILES);
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
                if (name.length() > 19) {
                    name = name.substr(0, 16) + "...";
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
                if (sub.length() > 22) {
                    sub = sub.substr(0, 19) + "...";
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
        if (name.length() > 20) {
            name = name.substr(0, 17) + "...";
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
                      float anchorX,
                      float anchorY) {
    if (options.empty()) {
        return;
    }

    const float PAD = 6.0f;
    const float LINE_H = 16.0f;
    const float BOX_W = 190.0f;
    float BOX_H = LINE_H * (float) options.size() + PAD * 2.0f;
    float BOX_X = std::min(anchorX, 400.0f - BOX_W - 2.0f);
    float BOX_Y = std::min(anchorY, 240.0f - BOX_H - 2.0f);

    C2D_DrawRectSolid(BOX_X + 3, BOX_Y + 3, 0.55f, BOX_W, BOX_H, C2D_Color32(0, 0, 0, 0xB0));
    C2D_DrawRectSolid(BOX_X, BOX_Y, 0.60f, BOX_W, BOX_H, C2D_Color32(0x1E, 0x1E, 0x1E, 0xF8));

    u32 border = C2D_Color32(0x30, 0x7A, 0xB8, 0xFF);
    C2D_DrawRectSolid(BOX_X, BOX_Y, 0.65f, BOX_W, 1, border);
    C2D_DrawRectSolid(BOX_X, BOX_Y + BOX_H - 1, 0.65f, BOX_W, 1, border);
    C2D_DrawRectSolid(BOX_X, BOX_Y, 0.65f, 1, BOX_H, border);
    C2D_DrawRectSolid(BOX_X + BOX_W - 1, BOX_Y, 0.65f, 1, BOX_H, border);

    C2D_TextBufClear(g_dynamicBuf);
    for (size_t i = 0; i < options.size(); ++i) {
        bool sel = (i == selectedIdx);
        float itemY = BOX_Y + PAD + LINE_H * (float) i;
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

    for (size_t i = 0; i < lines.size(); ++i) {
        float y = TEXT_Y + LINE_H * (float) i;
        if (y + LINE_H > MAX_Y) {
            break;
        }
        drawStr(
            lines[i].c_str(), TEXT_X, y, 0.92f, 0.38f, 0.38f, C2D_Color32f(0.78f, 0.78f, 0.78f, 1));
    }
}

void renderBottomScreen(bool songPlaying,
                        double positionSeconds,
                        double durationSeconds,
                        const std::string &songName,
                        const std::string &songArtist,
                        float seekBarX,
                        float seekBarY,
                        float seekBarW,
                        float seekBarH,
                        float seekProgressOverride) {
    C2D_TextBufClear(g_dynamicBuf);

    if (!songPlaying) {
        drawStr("No song playing", 4, 8, 0.5f, 0.42f, 0.42f, C2D_Color32f(0.35f, 0.35f, 0.35f, 1));
        C2D_DrawRectSolid(
            seekBarX, seekBarY, 0.5f, seekBarW, seekBarH, C2D_Color32(0x22, 0x22, 0x22, 0xFF));
        return;
    }

    const bool hasArtist = !songArtist.empty();

    if (hasArtist) {
        std::string artist = songArtist;
        if (artist.length() > 40) {
            artist = artist.substr(0, 37) + "...";
        }
        drawStr(artist.c_str(), 4, 8, 0.5f, 0.40f, 0.40f, C2D_Color32f(0.55f, 0.55f, 0.55f, 1));
    }

    // Song title (strip path and extension)
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
        if (name.length() > 38) {
            name = name.substr(0, 35) + "...";
        }

        float titleY = hasArtist ? 22.0f : 8.0f;
        drawStr(name.c_str(), 4, titleY, 0.5f, 0.44f, 0.44f, C2D_Color32f(0.90f, 0.90f, 0.90f, 1));
    }

    // Timestamp: use drag position when scrubbing
    {
        double displayPos = (seekProgressOverride >= 0.0f && durationSeconds > 0)
                                ? (double) seekProgressOverride * durationSeconds
                                : positionSeconds;
        std::string t = (durationSeconds > 0)
                            ? fmtTime(displayPos) + " / " + fmtTime(durationSeconds)
                            : fmtTime(displayPos);
        float timeY = hasArtist ? 38.0f : 24.0f;
        drawStr(t.c_str(), 4, timeY, 0.5f, 0.46f, 0.46f, C2D_Color32f(0.80f, 0.80f, 0.80f, 1));
    }

    float progress =
        (seekProgressOverride >= 0.0f)
            ? seekProgressOverride
            : ((durationSeconds > 0) ? (float) (positionSeconds / durationSeconds) : 0.0f);
    drawProgressBar(seekBarX, seekBarY, seekBarW, seekBarH, progress);

    drawStr("Touch to seek",
            4,
            seekBarY + seekBarH + 5,
            0.5f,
            0.38f,
            0.38f,
            C2D_Color32f(0.35f, 0.35f, 0.35f, 1));
}

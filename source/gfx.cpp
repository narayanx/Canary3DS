#include "gfx.h"

#include <citro2d.h>
#include <dirent.h>

#include <string>
#include <vector>

#include "constants.h"

C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

C2D_TextBuf g_dynamicBuf;

void sceneInit(void) {
    g_dynamicBuf = C2D_TextBufNew(4096);
}

void sceneExit(void) {
    // Delete the text buffers
    C2D_TextBufDelete(g_dynamicBuf);
}

void printC2DText(std::string msg, size_t lineOffset = 0) {
    const float BASE_Y_OFFSET = 0.0f;
    const float LINE_Y_OFFSET = 16.0f;

    C2D_TextBufClear(g_dynamicBuf);

    float yOffset = LINE_Y_OFFSET * (lineOffset) + BASE_Y_OFFSET;

    char buf[160];
    C2D_Text dynText;
    snprintf(buf, sizeof(buf), "%s", msg.c_str());
    C2D_TextParse(&dynText, g_dynamicBuf, buf);
    C2D_TextOptimize(&dynText);
    C2D_DrawText(&dynText, C2D_AlignLeft | C2D_WithColor, 10.0f, yOffset, 0.5f, 0.5f, 0.5f,
                 C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f));
}

void logToBottomScreen(const char *message) {
    static std::vector<std::string> logLines;
    static C2D_TextBuf logBuf = nullptr;

    if (!logBuf) {
        logBuf = C2D_TextBufNew(4096);
    }

    logLines.emplace_back(message);
    while ((int)logLines.size() > MAX_BOTTOM_SCREEN_LINES) {
        logLines.erase(logLines.begin());
    }

    const float LINE_Y_OFFSET = 16.0f;

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(bottom, C2D_Color32(0x00, 0x00, 0x00, 0xFF)); // always clear
    C2D_SceneBegin(bottom);
    C2D_TextBufClear(logBuf);  // own buffer, never touches g_dynamicBuf

    for (size_t i = 0; i < logLines.size(); i++) {
        char buf[160];
        C2D_Text dynText;
        snprintf(buf, sizeof(buf), "%s", logLines[i].c_str());
        C2D_TextParse(&dynText, logBuf, buf);
        C2D_TextOptimize(&dynText);
        C2D_DrawText(&dynText, C2D_AlignLeft | C2D_WithColor,
                     10.0f, LINE_Y_OFFSET * (float)i, 0.5f,
                     0.5f, 0.5f, C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f));
    }

    C3D_FrameEnd(0);
}

void logToBottomScreen(const std::string& message) {
    // forward to c string version
    logToBottomScreen(message.c_str());
}

void printFiles(std::vector<dirent> files, size_t selectedFile, size_t scrollOffset,
                size_t maxFiles, size_t lineOffset) {
    const float BASE_Y_OFFSET = 0.0f;
    const float LINE_Y_OFFSET = 16.0f;
    const float ITEM_X = 10.0f;

    size_t iter = 0;
    for (size_t i = scrollOffset; i < std::min(files.size(), (size_t)MAX_FILES + scrollOffset); i++) {
        float yOffset = LINE_Y_OFFSET * (iter + lineOffset) + BASE_Y_OFFSET;

        if (i == selectedFile) {
            C2D_DrawRectSolid(0.0f, yOffset - 1.0f, 0.4f,
                              400.0f, LINE_Y_OFFSET,
                              C2D_Color32(0x2D, 0x2D, 0x2D, 0xFF));
        }

        char buf[160];
        C2D_Text dynText;
        std::string fileName = files[i].d_name;
        std::string postfix = (files[i].d_type == DT_DIR) ? "/" : "";
        snprintf(buf, sizeof(buf), "%s%s", fileName.c_str(), postfix.c_str());
        C2D_TextParse(&dynText, g_dynamicBuf, buf);
        C2D_TextOptimize(&dynText);

        u32 col = (i == selectedFile)
                      ? C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f)
                      : C2D_Color32f(0.7f, 0.7f, 0.7f, 1.0f);
        C2D_DrawText(&dynText, C2D_AlignLeft | C2D_WithColor, ITEM_X, yOffset, 0.5f, 0.5f, 0.5f, col);
        iter++;
    }
}

void printQueue(const std::deque<std::string>& queue, size_t selectedItem, size_t lineOffset) {
    if (queue.empty()) {
        return;
    }

    const float START_X = 210.0f;
    const float BASE_Y_OFFSET = 8.0f;

    C2D_TextBufClear(g_dynamicBuf);

    // Header
    {
        C2D_Text header;
        C2D_TextParse(&header, g_dynamicBuf, "Queue");
        C2D_TextOptimize(&header);
        C2D_DrawText(&header, C2D_AlignLeft | C2D_WithColor,
                     START_X, BASE_Y_OFFSET, 0.5f,
                     0.5f, 0.5f,
                     C2D_Color32f(0.5f, 0.5f, 0.5f, 1.0f)
                    );
    }

    size_t maxVisible = 10;
    for (size_t i = 0; i < std::min(queue.size(), maxVisible); i++) {
        std::string name = queue[i];
        bool isSelected = (i == selectedItem);

        // strip path
        size_t slash = name.find_last_of('/');
        if (slash != std::string::npos) {
            name = name.substr(slash + 1);
        }

        // truncate long names
        if (name.length() > 20) {
            name = name.substr(0, 17) + "...";
        }
        float y = BASE_Y_OFFSET + 16.0f * (i + lineOffset);

        if (isSelected) {
            C2D_DrawRectSolid(START_X, y - 1.0f, 0.4f,
                            400.0f - START_X, 16.0f,
                            C2D_Color32(0x2D, 0x2D, 0x2D, 0xFF));
        }
        std::string display = name;

        C2D_Text text;
        C2D_TextParse(&text, g_dynamicBuf, display.c_str());
        C2D_TextOptimize(&text);

        u32 col = isSelected ? C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f)
                            : C2D_Color32f(0.7f, 0.7f, 0.7f, 1.0f);
        const float BUFFER_X = 4.0f;
        C2D_DrawText(&text, C2D_AlignLeft | C2D_WithColor,
                    START_X+BUFFER_X, y, 0.5f,
                    0.45f, 0.45f, col);
    }

    if (queue.size() > maxVisible) {
        C2D_Text more;
        C2D_TextParse(&more, g_dynamicBuf, "...");
        C2D_TextOptimize(&more);
        C2D_DrawText(&more, C2D_AlignLeft | C2D_WithColor,
                     START_X, BASE_Y_OFFSET + 16.0f * (maxVisible + 1),
                     0.5f, 0.45f, 0.45f,
                     C2D_Color32f(0.4f, 0.4f, 0.4f, 0.8f));
    }
}

// Renders items[scrollOffset .. scrollOffset+MAX_FILES), highlighting selectedIdx.
// Mirrors the layout of printFiles exactly.
void printStringList(const std::vector<std::string>& items, size_t selectedIdx,
                     size_t scrollOffset, size_t lineOffset) {
    if (items.empty()) {
        printC2DText("(empty)", lineOffset);
        return;
    }

    const float BASE_Y_OFFSET = 0.0f;
    const float LINE_Y_OFFSET = 16.0f;
    const float ITEM_X = 10.0f;

    size_t iter = 0;
    for (size_t i = scrollOffset;
         i < std::min(items.size(), scrollOffset + (size_t)MAX_FILES); i++) {
        float yOffset = LINE_Y_OFFSET * (iter + lineOffset) + BASE_Y_OFFSET;

        if (i == selectedIdx) {
            C2D_DrawRectSolid(0.0f, yOffset - 1.0f, 0.4f,
                              400.0f, LINE_Y_OFFSET,
                              C2D_Color32(0x2D, 0x2D, 0x2D, 0xFF));
        }

        char buf[160];
        C2D_Text dynText;
        snprintf(buf, sizeof(buf), "%s", items[i].c_str());
        C2D_TextParse(&dynText, g_dynamicBuf, buf);
        C2D_TextOptimize(&dynText);

        u32 col = (i == selectedIdx)
                      ? C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f)
                      : C2D_Color32f(0.7f, 0.7f, 0.7f, 1.0f);
        C2D_DrawText(&dynText, C2D_AlignLeft | C2D_WithColor, ITEM_X, yOffset, 0.5f, 0.5f, 0.5f, col);
        iter++;
    }
}

void printContextMenu(const std::vector<std::string>& options, size_t selectedIdx,
                      float anchorX, float anchorY) {
    if (options.empty()) {
        return;
    }

    const float PAD    = 6.0f;
    const float LINE_H = 16.0f;
    const float BOX_W  = 190.0f;
    float BOX_H = LINE_H * (float)options.size() + PAD * 2.0f;

    // Clamp so the box never leaves the 400x240 top screen
    float BOX_X = std::min(anchorX, 400.0f - BOX_W - 2.0f);
    float BOX_Y = std::min(anchorY, 240.0f - BOX_H - 2.0f);

    // Drop-shadow
    C2D_DrawRectSolid(BOX_X + 3.0f, BOX_Y + 3.0f, 0.55f, BOX_W, BOX_H,
                      C2D_Color32(0x00, 0x00, 0x00, 0xB0));
    // Background
    C2D_DrawRectSolid(BOX_X, BOX_Y, 0.6f, BOX_W, BOX_H,
                      C2D_Color32(0x1E, 0x1E, 0x1E, 0xF8));
    // Border (four 1-px edges)
    u32 borderCol = C2D_Color32(0x30, 0x7A, 0xB8, 0xFF);
    C2D_DrawRectSolid(BOX_X,              BOX_Y,             0.65f, BOX_W,  1.0f, borderCol);
    C2D_DrawRectSolid(BOX_X,              BOX_Y + BOX_H - 1, 0.65f, BOX_W,  1.0f, borderCol);
    C2D_DrawRectSolid(BOX_X,              BOX_Y,             0.65f, 1.0f,  BOX_H, borderCol);
    C2D_DrawRectSolid(BOX_X + BOX_W - 1, BOX_Y,             0.65f, 1.0f,  BOX_H, borderCol);

    // Clear buffer to use for context menu text
    C2D_TextBufClear(g_dynamicBuf);

    // Option rows
    for (size_t i = 0; i < options.size(); i++) {
        bool sel   = (i == selectedIdx);
        float itemY = BOX_Y + PAD + LINE_H * (float)(i);

        // Highlight bar behind selected item
        if (sel) {
            C2D_DrawRectSolid(BOX_X + 1.0f, itemY - 1.0f, 0.62f,
                              BOX_W - 2.0f, LINE_H,
                              C2D_Color32(0x2D, 0x2D, 0x2D, 0xFF));
        }

        std::string label = options[i];
        C2D_Text t;
        C2D_TextParse(&t, g_dynamicBuf, label.c_str());
        C2D_TextOptimize(&t);
        u32 col = sel ? C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f)
                      : C2D_Color32f(0.6f, 0.6f, 0.6f, 1.0f);
        C2D_DrawText(&t, C2D_AlignLeft | C2D_WithColor,
                     BOX_X + PAD, itemY, 0.7f,
                     0.46f, 0.46f, col);
    }
}

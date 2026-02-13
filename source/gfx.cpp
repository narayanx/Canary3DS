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
    const float LINE_Y_OFFSET = 14.0f;

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
    static int line = 0;
    if (line == 0) {
        C2D_TargetClear(bottom, CLEAR_COLOR);
    }
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_SceneBegin(bottom);
    printC2DText(message, line);
    C3D_FrameEnd(0);
    line++;
    // arbitrary cutoff
    if (line >= MAX_BOTTOM_SCREEN_LINES) {
        line = 0;
    }
}

void logToBottomScreen(const std::string& message) {
    // forward to c string version
    logToBottomScreen(message.c_str());
}

void printFiles(std::vector<dirent> files, size_t selectedFile, size_t maxFiles = MAX_FILES,
                size_t lineOffset = 0) {
    const float BASE_Y_OFFSET = 0.0f;
    const float LINE_Y_OFFSET = 14.0f;

    size_t iter = 0;
    for (size_t i = selectedFile; i < std::min(files.size(), (size_t)MAX_FILES + selectedFile);
         i++) {
        char buf[160];
        C2D_Text dynText;

        std::string fileName = "";
        std::string prefix = "";
        std::string postfix = "";

        if (i == selectedFile) {
            prefix = "-> ";
        } else {
            prefix = "   ";
        }
        fileName = files[i].d_name;
        if (files[i].d_type == DT_DIR) {
            postfix = "/";
        }
        snprintf(buf, sizeof(buf), "%s%s%s", prefix.c_str(), fileName.c_str(), postfix.c_str());
        C2D_TextParse(&dynText, g_dynamicBuf, buf);
        C2D_TextOptimize(&dynText);
        float yOffset = LINE_Y_OFFSET * (iter + lineOffset) + BASE_Y_OFFSET;
        C2D_DrawText(&dynText, C2D_AlignLeft | C2D_WithColor, 10.0f, yOffset, 0.5f, 0.5f, 0.5f,
                     C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f));
        iter++;
    }
}

void printQueue(const std::deque<std::string>& queue, size_t lineOffset) {
    if (queue.empty()) {
        return;
    }

    const float START_X = 210.0f;   // right side of top screen
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
                     C2D_Color32f(0.7f, 0.9f, 1.0f, 1.0f));
    }

    size_t maxVisible = 10;
    for (size_t i = 0; i < std::min(queue.size(), maxVisible); i++) {
        std::string name = queue[i];

        // strip path
        size_t slash = name.find_last_of('/');
        if (slash != std::string::npos) {
            name = name.substr(slash + 1);
        }

        // truncate long names
        if (name.length() > 20) {
            name = name.substr(0, 17) + "...";
        }

        C2D_Text text;
        C2D_TextParse(&text, g_dynamicBuf, name.c_str());
        C2D_TextOptimize(&text);

        float y = BASE_Y_OFFSET + 16.0f * (i + 1 + lineOffset);
        C2D_DrawText(&text, C2D_AlignLeft | C2D_WithColor,
                     START_X, y, 0.5f,
                     0.45f, 0.45f,
                     C2D_Color32f(1, 1, 1, 1));
    }

    if (queue.size() > maxVisible) {
        C2D_Text more;
        C2D_TextParse(&more, g_dynamicBuf, "...");
        C2D_TextOptimize(&more);
        C2D_DrawText(&more, C2D_AlignLeft | C2D_WithColor,
                     START_X, BASE_Y_OFFSET + 16.0f * (maxVisible + 1),
                     0.5f, 0.45f, 0.45f,
                     C2D_Color32f(1, 1, 1, 0.6f));
    }
}

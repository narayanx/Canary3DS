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
    C2D_TextBufClear(g_dynamicBuf);

    const float BASE_Y_OFFSET = 8.0f;
    float y_offset = 16.0f * (lineOffset) + BASE_Y_OFFSET;

    char buf[160];
    C2D_Text dynText;
    snprintf(buf, sizeof(buf), "%s", msg.c_str());
    C2D_TextParse(&dynText, g_dynamicBuf, buf);
    C2D_TextOptimize(&dynText);
    C2D_DrawText(&dynText, C2D_AlignLeft | C2D_WithColor, 10.0f, y_offset, 0.5f, 0.5f, 0.5f,
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
    if (line >= 14) {
        line = 0;
    }
}

void printFiles(std::vector<dirent> files, size_t selectedFile, size_t maxFiles = MAX_FILES,
                size_t lineOffset = 0) {
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
        const float BASE_Y_OFFSET = 8.0f;
        float y_offset = 16.0f * (iter + lineOffset) + BASE_Y_OFFSET;
        C2D_DrawText(&dynText, C2D_AlignLeft | C2D_WithColor, 10.0f, y_offset, 0.5f, 0.5f, 0.5f,
                     C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f));
        iter++;
    }
}

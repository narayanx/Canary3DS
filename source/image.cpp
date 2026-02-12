#include "image.h"

#include <3ds.h>
#include <RIP/C3D.h>
#include <citro2d.h>
#include <citro3d.h>

#include <stb_image.h>

#include <cstdlib>
#include <cstring>

#include "gfx.h"

// returns true on success, false on failure
bool loadC2DImage(const char *filepath, C2D_Image &image, C3D_Tex& imageTex, Tex3DS_SubTexture &subtex) {
    int width, height;
    stbi_uc *data = stbi_load(filepath, &width, &height, nullptr, 4);
    if (!data) {
        logToBottomScreen("Failed to load image\n");
        return false;
    }

    C3D_TexInit(&imageTex, (u16)width, (u16)height, GPU_RGBA8);
    ripConvertAndLoadC3DTexImage(&imageTex, data, GPU_TEXFACE_2D, 0);
    stbi_image_free(data);

    subtex = {.width = imageTex.width,
                                .height = imageTex.height,
                                .left = 0.0f,
                                .top = 1.0f,
                                .right = 1.0f,
                                .bottom = 0.0f};

    image.tex = &imageTex;
    image.subtex = &subtex;

    return true;
}

// allow supporting images that are not power of 2 dimension by padding with transparent pixels
int nextPow2(int x) {
    int p = 1;
    while (p < x) {
        p <<= 1;
    }
    return p;
}

// returns true on success, false on failure
bool loadC2DImageMemory(const unsigned char *buffer, int len, C2D_Image &image, C3D_Tex& imageTex, Tex3DS_SubTexture &subtex, bool freeTexMem) {
    int width, height;
    width = height = -1;

    stbi_uc *data = stbi_load_from_memory(buffer, len, &width, &height, nullptr, 4);
    // logToBottomScreen(((std::string)"image width: "+std::to_string(width)).c_str());
    // logToBottomScreen(((std::string)"image height: "+std::to_string(height)).c_str());
    if (!data) {
        logToBottomScreen("Failed to load image\n");
        return false;
    }
    int newWidth = nextPow2(width);
    int newHeight = nextPow2(height);
    // TODO add check for if image is bigger than 1024x1024

    size_t newBufferSize = newWidth * newHeight * 4;
    unsigned char* padded = (unsigned char*)linearAlloc(newBufferSize);
    // unassigned rows should be 0 (transparent black pixels)
    if (padded) {
        memset(padded, 0, newBufferSize);
    }
    
    // copy pixels in original image to larger image
    for (int y = 0; y < height; y++) {
        // 4 channels
        memcpy(padded + y * newWidth * 4, data + y * width * 4, width * 4);
    }

    // if image Tex was being previously used, must free memory before reinitializing
    if (freeTexMem) {
        C3D_TexDelete(&imageTex);
    }

    C3D_TexInit(&imageTex, (u16)newWidth, (u16)newHeight, GPU_RGBA8);
    ripConvertAndLoadC3DTexImage(&imageTex, padded, GPU_TEXFACE_2D, 0);

    linearFree(padded);
    stbi_image_free(data);
    // set subtex to only the used area
    subtex = {.width = imageTex.width,
                                .height = imageTex.height,
                                .left = 0.0f,
                                .top = 1.0f,
                                .right = (float)width / (float)imageTex.width,
                                .bottom = 1.0f - (float)height / (float)imageTex.height};

    // C2D_Image image;
    image.tex = &imageTex;
    image.subtex = &subtex;

    return true;
}

void drawCoverScaled(
    C2D_Image& image,
    Tex3DS_SubTexture& subtex,
    float x,
    float y
) {
    float scaleX = COVER_TARGET_WIDTH / image.tex->width;
    float scaleY = COVER_TARGET_HEIGHT / image.tex->height;

    // make non square cover art fit in target width by target height rectangle
    float scale = std::min(scaleX, scaleY);

    C2D_DrawImageAt(image, x, y, 1.0f, nullptr, scale, scale);
}

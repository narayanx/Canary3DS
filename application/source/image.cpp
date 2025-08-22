#include "image.h"

#include <3ds.h>
#include <RIP/C3D.h>
#include <citro2d.h>
#include <citro3d.h>
#include <stb_image.h>

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
    // C3D_TexLoadImage(&imageTex, data, (GPU_TEXFACE)0, 0);
    stbi_image_free(data);

    subtex = {.width = imageTex.width,
                                .height = imageTex.height,
                                .left = 0.0f,
                                .top = 1.0f,
                                .right = 1.0f,
                                .bottom = 0.0f};

    // C2D_Image image;
    image.tex = &imageTex;
    image.subtex = &subtex;

    return true;
}


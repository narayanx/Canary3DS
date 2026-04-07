#include "image.h"

#include <3ds.h>
#include <RIP/C3D.h>
#include <citro2d.h>
#include <citro3d.h>
#include <stb_image.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "gfx.h"


static int nextPow2(int x) {
    int p = 1;
    while (p < x) p <<= 1;
    return p;
}

bool loadC2DImage(const char* filepath, C2D_Image& image,
                  C3D_Tex& imageTex, Tex3DS_SubTexture& subtex) {
    int w, h;
    stbi_uc* data = stbi_load(filepath, &w, &h, nullptr, 4);
    if (!data) {
        logToBottomScreen("Failed to load image");
        return false;
    }

    C3D_TexInit(&imageTex, (u16)w, (u16)h, GPU_RGBA8);
    ripConvertAndLoadC3DTexImage(&imageTex, data, GPU_TEXFACE_2D, 0);
    stbi_image_free(data);

    subtex = { .width  = imageTex.width,  .height = imageTex.height,
               .left   = 0.0f,            .top    = 1.0f,
               .right  = 1.0f,            .bottom = 0.0f };
    image.tex    = &imageTex;
    image.subtex = &subtex;
    return true;
}

bool loadC2DImageMemory(const unsigned char* buffer, int len,
                        C2D_Image& image, C3D_Tex& imageTex,
                        Tex3DS_SubTexture& subtex, bool freeTexMem) {
    int w = -1, h = -1;
    stbi_uc* data = stbi_load_from_memory(buffer, len, &w, &h, nullptr, 4);
    if (!data) {
        logToBottomScreen("Failed to load image from memory");
        return false;
    }

    int nw = nextPow2(w);
    int nh = nextPow2(h);

    unsigned char* padded = (unsigned char*)linearAlloc((size_t)nw * nh * 4);
    if (!padded) {
        stbi_image_free(data);
        logToBottomScreen("linearAlloc failed for image");
        return false;
    }
    memset(padded, 0, (size_t)nw * nh * 4);
    for (int y = 0; y < h; ++y)
        memcpy(padded + y * nw * 4, data + y * w * 4, (size_t)w * 4);

    if (freeTexMem) C3D_TexDelete(&imageTex);

    if (!C3D_TexInit(&imageTex, (u16)nw, (u16)nh, GPU_RGBA8)) {
        linearFree(padded);
        stbi_image_free(data);
        logToBottomScreen("C3D_TexInit failed");
        return false;
    }
    ripConvertAndLoadC3DTexImage(&imageTex, padded, GPU_TEXFACE_2D, 0);

    linearFree(padded);
    stbi_image_free(data);

    subtex = { .width  = imageTex.width,
               .height = imageTex.height,
               .left   = 0.0f,
               .top    = 1.0f,
               .right  = (float)w  / (float)imageTex.width,
               .bottom = 1.0f - (float)h / (float)imageTex.height };
    image.tex    = &imageTex;
    image.subtex = &subtex;
    return true;
}

void drawCoverScaled(C2D_Image& image, Tex3DS_SubTexture& /*subtex*/,
                     float x, float y) {
    float sx = COVER_TARGET_WIDTH  / (float)image.tex->width;
    float sy = COVER_TARGET_HEIGHT / (float)image.tex->height;
    float s  = std::min(sx, sy);
    C2D_DrawImageAt(image, x, y, 1.0f, nullptr, s, s);
}

// METADATA_BLOCK_PICTURE parser
// Binary layout (big-endian u32 fields):
//   [0]      picture type
//   [4]      MIME type string length  (N)
//   [8]      MIME type string         (N bytes)
//   [8+N]    description length       (D)
//   [12+N]   description string       (D bytes)
//   [12+N+D] width  (4 bytes)
//   [16+N+D] height (4 bytes)
//   [20+N+D] colour depth (4 bytes)
//   [24+N+D] indexed colour count (4 bytes)
//   [28+N+D] image data length  (L)
//   [32+N+D] image data         (L bytes)

static uint32_t readU32BE(const std::string& d, size_t i) {
    return ((uint8_t)d[i]   << 24) | ((uint8_t)d[i+1] << 16) |
           ((uint8_t)d[i+2] <<  8) |  (uint8_t)d[i+3];
}

std::string extractImageFromPictureBlock(const std::string& block) {
    if (block.size() < 8) return {};

    uint32_t mimeLen = readU32BE(block, 4);
    if (block.size() < 8 + mimeLen + 4) return {};

    uint32_t descLen = readU32BE(block, 8 + mimeLen);
    if (block.size() < 8 + mimeLen + 4 + descLen + 20) return {};

    uint32_t dataLen = readU32BE(block, 28 + mimeLen + descLen);
    size_t   offset  = 32 + mimeLen + descLen;

    if (block.size() < offset + dataLen) return {};
    return block.substr(offset, dataLen);
}

// Upload raw image bytes to a C2D_Image
bool loadCoverArtFromBytes(const unsigned char* data, int len,
                            C2D_Image& image, C3D_Tex& tex,
                            Tex3DS_SubTexture& subtex, bool freeExisting) {
    if (!data || len <= 0) return false;
    return loadC2DImageMemory(data, len, image, tex, subtex, freeExisting);
}

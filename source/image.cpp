#include "image.h"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <RIP/C3D.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stb_image.h>
#include <sys/stat.h>
#include <vector>

#include "gfx.h"

// 3DS GPU texture dimensions are capped at 1024x1024
static constexpr int MAX_COVER_TEX_SIZE = 1024;

static int nextPow2(int x) {
    int p = 1;
    while (p < x) {
        p <<= 1;
    }
    return p;
}

bool loadC2DImage(const char *filepath,
                  C2D_Image &image,
                  C3D_Tex &imageTex,
                  Tex3DS_SubTexture &subtex) {
    int w, h;
    stbi_uc *data = stbi_load(filepath, &w, &h, nullptr, 4);
    if (!data) {
        logToDebugScreen("Failed to load image");
        return false;
    }
    if (w > MAX_COVER_TEX_SIZE || h > MAX_COVER_TEX_SIZE) {
        stbi_image_free(data);
        logToDebugScreen("Image exceeds 1024x1024, skipping (width, height): " + std::to_string(w) +
                         ", " + std::to_string(h));
        return false;
    }

    int nw = nextPow2(w);
    int nh = nextPow2(h);

    unsigned char *padded = (unsigned char *) linearAlloc((size_t) nw * nh * 4);
    if (!padded) {
        stbi_image_free(data);
        logToDebugScreen("linearAlloc failed for image");
        return false;
    }
    memset(padded, 0, (size_t) nw * nh * 4);
    for (int y = 0; y < h; ++y) {
        memcpy(padded + y * nw * 4, data + y * w * 4, (size_t) w * 4);
    }
    stbi_image_free(data);

    if (!C3D_TexInit(&imageTex, (u16) nw, (u16) nh, GPU_RGBA8)) {
        linearFree(padded);
        logToDebugScreen("C3D_TexInit failed");
        return false;
    }
    ripConvertAndLoadC3DTexImage(&imageTex, padded, GPU_TEXFACE_2D, 0);
    linearFree(padded);

    subtex = {.width = imageTex.width,
              .height = imageTex.height,
              .left = 0.0f,
              .top = 1.0f,
              .right = (float) w / (float) imageTex.width,
              .bottom = 1.0f - (float) h / (float) imageTex.height};
    image.tex = &imageTex;
    image.subtex = &subtex;
    return true;
}

bool loadC2DImageMemory(const unsigned char *buffer,
                        int len,
                        C2D_Image &image,
                        C3D_Tex &imageTex,
                        Tex3DS_SubTexture &subtex,
                        bool freeTexMem) {
    int w = -1, h = -1;
    stbi_uc *data = stbi_load_from_memory(buffer, len, &w, &h, nullptr, 4);
    if (!data) {
        logToDebugScreen("Failed to load image from memory");
        return false;
    }
    if (w > MAX_COVER_TEX_SIZE || h > MAX_COVER_TEX_SIZE) {
        stbi_image_free(data);
        logToDebugScreen("Cover art exceeds 1024x1024, skipping (width, height): " +
                         std::to_string(w) + ", " + std::to_string(h));

        return false;
    }

    int nw = nextPow2(w);
    int nh = nextPow2(h);

    unsigned char *padded = (unsigned char *) linearAlloc((size_t) nw * nh * 4);
    if (!padded) {
        stbi_image_free(data);
        logToDebugScreen("linearAlloc failed for image");
        return false;
    }
    memset(padded, 0, (size_t) nw * nh * 4);
    for (int y = 0; y < h; ++y) {
        memcpy(padded + y * nw * 4, data + y * w * 4, (size_t) w * 4);
    }

    if (freeTexMem) {
        C3D_TexDelete(&imageTex);
    }

    if (!C3D_TexInit(&imageTex, (u16) nw, (u16) nh, GPU_RGBA8)) {
        linearFree(padded);
        stbi_image_free(data);
        logToDebugScreen("C3D_TexInit failed");
        return false;
    }
    ripConvertAndLoadC3DTexImage(&imageTex, padded, GPU_TEXFACE_2D, 0);

    linearFree(padded);
    stbi_image_free(data);

    subtex = {.width = imageTex.width,
              .height = imageTex.height,
              .left = 0.0f,
              .top = 1.0f,
              .right = (float) w / (float) imageTex.width,
              .bottom = 1.0f - (float) h / (float) imageTex.height};
    image.tex = &imageTex;
    image.subtex = &subtex;
    return true;
}

void drawCoverScaled(C2D_Image &image, Tex3DS_SubTexture & /*subtex*/, float x, float y) {
    float sx = COVER_TARGET_WIDTH / (float) image.tex->width;
    float sy = COVER_TARGET_HEIGHT / (float) image.tex->height;
    float s = std::min(sx, sy);
    C2D_DrawImageAt(image, x, y, 0.3f, nullptr, s, s);
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

static uint32_t readU32BE(const std::string &d, size_t i) {
    return ((uint8_t) d[i] << 24) | ((uint8_t) d[i + 1] << 16) | ((uint8_t) d[i + 2] << 8) |
           (uint8_t) d[i + 3];
}

std::string extractImageFromPictureBlock(const std::string &block) {
    if (block.size() < 8) {
        return {};
    }

    uint32_t mimeLen = readU32BE(block, 4);
    if (block.size() < 8 + mimeLen + 4) {
        return {};
    }

    uint32_t descLen = readU32BE(block, 8 + mimeLen);
    if (block.size() < 8 + mimeLen + 4 + descLen + 20) {
        return {};
    }

    uint32_t dataLen = readU32BE(block, 28 + mimeLen + descLen);
    size_t offset = 32 + mimeLen + descLen;

    if (block.size() < offset + dataLen) {
        return {};
    }
    return block.substr(offset, dataLen);
}

// Upload raw image bytes to a C2D_Image
bool loadCoverArtFromBytes(const unsigned char *data,
                           int len,
                           C2D_Image &image,
                           C3D_Tex &tex,
                           Tex3DS_SubTexture &subtex,
                           bool freeExisting) {
    if (!data || len <= 0) {
        return false;
    }
    return loadC2DImageMemory(data, len, image, tex, subtex, freeExisting);
}

bool loadFolderCoverArt(const std::string &songPath,
                        C2D_Image &image,
                        C3D_Tex &tex,
                        Tex3DS_SubTexture &subtex,
                        bool freeExisting) {
    size_t sl = songPath.find_last_of('/');
    if (sl == std::string::npos) {
        return false;
    }
    std::string dir = songPath.substr(0, sl + 1);

    static constexpr const char *CANDIDATES[] = {
        "cover.jpg",
        "cover.jpeg",
        "cover.png",
        "folder.jpg",
        "folder.jpeg",
        "folder.png",
    };

    struct stat st;
    std::string path;
    bool found = false;
    for (const char *name : CANDIDATES) {
        path = dir + name;
        if (stat(path.c_str(), &st) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }

    if (freeExisting) {
        C3D_TexDelete(&tex);
    }
    return loadC2DImage(path.c_str(), image, tex, subtex);
}

bool saveAsBmp128(const std::string &path, const unsigned char *data, int len) {
    int srcW, srcH;
    stbi_uc *rgba = stbi_load_from_memory(data, len, &srcW, &srcH, nullptr, 4);
    if (!rgba) {
        return false;
    }

    static const int OUT = 128;
    // 128*3 = 384, divisible by 4, no row padding needed, allocate on heap to avoid stack overflow
    std::vector<unsigned char> rgb((size_t) OUT * OUT * 3);

    for (int dy = 0; dy < OUT; ++dy) {
        int sy = dy * srcH / OUT;
        for (int dx = 0; dx < OUT; ++dx) {
            int sx = dx * srcW / OUT;
            const stbi_uc *px = rgba + (sy * srcW + sx) * 4;
            // BMP rows are bottom-up, write BGR
            int bmpRow = OUT - 1 - dy;
            unsigned char *dst = rgb.data() + (bmpRow * OUT + dx) * 3;
            dst[0] = px[2];
            dst[1] = px[1];
            dst[2] = px[0];
        }
    }
    stbi_image_free(rgba);

    const int pixelDataSize = OUT * OUT * 3;
    const int fileSize = 14 + 40 + pixelDataSize;

    unsigned char hdr[54] = {};
    hdr[0] = 'B';
    hdr[1] = 'M';
    hdr[2] = (unsigned char) (fileSize);
    hdr[3] = (unsigned char) (fileSize >> 8);
    hdr[4] = (unsigned char) (fileSize >> 16);
    hdr[5] = (unsigned char) (fileSize >> 24);
    hdr[10] = 54;   // pixel data offset
    hdr[14] = 40;   // BITMAPINFOHEADER size
    hdr[18] = OUT;  // width
    hdr[22] = OUT;  // height (positive = bottom-up)
    hdr[26] = 1;    // planes
    hdr[28] = 24;   // bits per pixel

    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr) &&
              fwrite(rgb.data(), 1, (size_t) pixelDataSize, f) == (size_t) pixelDataSize;
    fclose(f);
    return ok;
}

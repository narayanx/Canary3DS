#include "image.h"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <RIP/C3D.h>
#include <algorithm>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <stb_image.h>
#include <stb_image_resize2.h>
#include <sys/stat.h>
#include <vector>

extern "C" {
#include <jpeglib.h>
}

#include "gfx.h"
#include "settings.h"

namespace {
    // 3DS GPU texture dimensions are capped at 1024x1024
    constexpr int MAX_COVER_TEX_SIZE = 1024;

    int nextPow2(int x) {
        int p = 1;
        while (p < x) {
            p <<= 1;
        }
        return p;
    }

    // FNV-1a 64-bit hash of the raw (still encoded) source bytes. Used as cache key: allows
    // deduplication and automatic cache invalidation.
    uint64_t fnv1a64(const unsigned char *data, size_t len) {
        uint64_t h = 1469598103934665603ULL;  // FNV offset basis
        for (size_t i = 0; i < len; ++i) {
            h ^= data[i];
            h *= 1099511628211ULL;  // FNV prime
        }
        return h;
    }

    std::string coverCachePath(const unsigned char *data, int len) {
        uint64_t hash = fnv1a64(data, (size_t) len);
        char hex[17];
        snprintf(hex, sizeof(hex), "%016llx", (unsigned long long) hash);
        return std::string(COVER_CACHE_DIR) + hex + ".bmp";
    }

    void ensureCoverCacheDirExists() {
        std::string path(COVER_CACHE_DIR);
        size_t pos = 0;
        // mkdir fails if the parent directory doesn't exist yet, so create every
        // intermediate directory first.
        while ((pos = path.find('/', pos + 1)) != std::string::npos) {
            std::string subdir = path.substr(0, pos);
            mkdir(subdir.c_str(), 0777);
        }
        mkdir(path.c_str(), 0777);
    }

    bool s_cacheStatsInitialized = false;
    CoverCacheStats s_cacheStats;

    void ensureCacheStatsInitialized() {
        if (s_cacheStatsInitialized) {
            return;
        }
        s_cacheStats = {};
        DIR *d = opendir(std::string(COVER_CACHE_DIR).c_str());
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != nullptr) {
                std::string name = ent->d_name;
                if (name == "." || name == "..") {
                    continue;
                }
                struct stat st;
                if (stat((std::string(COVER_CACHE_DIR) + name).c_str(), &st) == 0 &&
                    S_ISREG(st.st_mode)) {
                    ++s_cacheStats.fileCount;
                    s_cacheStats.totalBytes += (uint64_t) st.st_size;
                }
            }
            closedir(d);
        }
        s_cacheStatsInitialized = true;
    }

    std::vector<unsigned char> readWholeFile(const std::string &path) {
        std::vector<unsigned char> buf;
        FILE *f = fopen(path.c_str(), "rb");
        if (!f) {
            return buf;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0) {
            buf.resize((size_t) sz);
            if (fread(buf.data(), 1, (size_t) sz, f) != (size_t) sz) {
                buf.clear();
            }
        }
        fclose(f);
        return buf;
    }

    // Downscale an RGBA8888 buffer using stb_image_resize2.
    std::vector<unsigned char>
    downscaleRGBA(const unsigned char *src, int srcW, int srcH, int maxDim, int &outW, int &outH) {
        if (srcW >= srcH) {
            outW = maxDim;
            outH = std::max(1, (int) ((int64_t) srcH * maxDim / srcW));
        } else {
            outH = maxDim;
            outW = std::max(1, (int) ((int64_t) srcW * maxDim / srcH));
        }

        std::vector<unsigned char> dst((size_t) outW * outH * 4);
        unsigned char *result =
            stbir_resize_uint8_srgb(src, srcW, srcH, 0, dst.data(), outW, outH, 0, STBIR_RGBA);
        if (!result) {
            logToDebugScreen("stbir_resize_uint8_srgb failed");
            dst.clear();
        }
        return dst;
    }

    // JPEG files always start with the SOI marker.
    bool looksLikeJpeg(const unsigned char *buffer, int len) {
        return len >= 2 && buffer[0] == 0xFF && buffer[1] == 0xD8;
    }

    struct JpegErrorMgr {
        jpeg_error_mgr pub;  // must be first member, libjpeg casts cinfo->err to this
        jmp_buf setjmpBuffer;
    };

    // libjpeg's default error handler calls exit() on a fatal error. This overrides it
    // to unwind back to decodeJpegScaled via setjmp/longjmp instead, documented pattern for
    // embedding libjpeg (see IJG's own example.c).
    void jpegErrorExit(j_common_ptr cinfo) {
        char buf[JMSG_LENGTH_MAX];
        (*cinfo->err->format_message)(cinfo, buf);
        logToDebugScreen(std::string("libjpeg error: ") + buf);
        auto *err = reinterpret_cast<JpegErrorMgr *>(cinfo->err);
        longjmp(err->setjmpBuffer, 1);
    }

    // Swallow warnings (e.g. minor corruption libjpeg can recover from).
    void jpegOutputMessage(j_common_ptr) {}

    // Decodes a JPEG directly at the smallest DCT scale (1/8, 1/4, 1/2, or 1/1)
    // that still yields at least `minDim` pixels on the shorter side.
    std::vector<unsigned char>
    decodeJpegScaled(const unsigned char *buffer, int len, int minDim, int &outW, int &outH) {
        jpeg_decompress_struct cinfo;
        JpegErrorMgr jerr;

        cinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = jpegErrorExit;
        jerr.pub.output_message = jpegOutputMessage;

        if (setjmp(jerr.setjmpBuffer)) {
            jpeg_destroy_decompress(&cinfo);
            return {};
        }

        jpeg_create_decompress(&cinfo);
        jpeg_mem_src(&cinfo, buffer, (unsigned long) len);

        if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
            jpeg_destroy_decompress(&cinfo);
            return {};
        }

        cinfo.out_color_space = JCS_RGB;

        // Try the most reduced scale first; step down until the result is no
        // smaller than minDim on its shorter side. 1/1 (last entry) is always
        // accepted as a final fallback.
        static constexpr int kScales[][2] = {{1, 8}, {1, 4}, {1, 2}, {1, 1}};
        for (const auto &scale : kScales) {
            cinfo.scale_num = scale[0];
            cinfo.scale_denom = scale[1];
            jpeg_calc_output_dimensions(&cinfo);
            if ((int) std::min(cinfo.output_width, cinfo.output_height) >= minDim ||
                scale[0] == scale[1]) {
                break;
            }
        }

        if (!jpeg_start_decompress(&cinfo)) {
            jpeg_destroy_decompress(&cinfo);
            return {};
        }

        outW = (int) cinfo.output_width;
        outH = (int) cinfo.output_height;
        const int components = cinfo.output_components;  // 3 for JCS_RGB

        std::vector<unsigned char> rgba((size_t) outW * outH * 4);
        std::vector<unsigned char> rowBuf((size_t) outW * components);
        JSAMPROW rowPtr[1];

        while (cinfo.output_scanline < cinfo.output_height) {
            unsigned int row = cinfo.output_scanline;
            rowPtr[0] = rowBuf.data();
            jpeg_read_scanlines(&cinfo, rowPtr, 1);
            unsigned char *dst = rgba.data() + (size_t) row * outW * 4;
            const unsigned char *src = rowBuf.data();
            for (int x = 0; x < outW; ++x) {
                dst[x * 4 + 0] = src[x * components + 0];
                dst[x * 4 + 1] = src[x * components + 1];
                dst[x * 4 + 2] = src[x * components + 2];
                dst[x * 4 + 3] = 255;
            }
        }

        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return rgba;
    }

    // Writes an RGBA8888 buffer of arbitrary (non square) dimensions as a 24-bit
    // BMP, correctly padding each row to a 4 byte boundary as the format requires.
    bool writeBmpRGBA(const std::string &path, const unsigned char *rgba, int w, int h) {
        const int rowSize = ((w * 3 + 3) / 4) * 4;
        const int pixelDataSize = rowSize * h;
        const int fileSize = 14 + 40 + pixelDataSize;

        std::vector<unsigned char> pixels((size_t) pixelDataSize, 0);
        for (int y = 0; y < h; ++y) {
            // BMP rows are stored bottom-up, BGR order.
            int bmpRow = h - 1 - y;
            unsigned char *dstRow = pixels.data() + (size_t) bmpRow * rowSize;
            const unsigned char *srcRow = rgba + (size_t) y * w * 4;
            for (int x = 0; x < w; ++x) {
                dstRow[x * 3 + 0] = srcRow[x * 4 + 2];
                dstRow[x * 3 + 1] = srcRow[x * 4 + 1];
                dstRow[x * 3 + 2] = srcRow[x * 4 + 0];
            }
        }

        unsigned char hdr[54] = {};
        hdr[0] = 'B';
        hdr[1] = 'M';
        hdr[2] = (unsigned char) (fileSize);
        hdr[3] = (unsigned char) (fileSize >> 8);
        hdr[4] = (unsigned char) (fileSize >> 16);
        hdr[5] = (unsigned char) (fileSize >> 24);
        hdr[10] = 54;  // pixel data offset
        hdr[14] = 40;  // BITMAPINFOHEADER size
        hdr[18] = (unsigned char) (w);
        hdr[19] = (unsigned char) (w >> 8);
        hdr[20] = (unsigned char) (w >> 16);
        hdr[21] = (unsigned char) (w >> 24);
        hdr[22] = (unsigned char) (h);  // positive = bottom-up
        hdr[23] = (unsigned char) (h >> 8);
        hdr[24] = (unsigned char) (h >> 16);
        hdr[25] = (unsigned char) (h >> 24);
        hdr[26] = 1;   // planes
        hdr[28] = 24;  // bits per pixel

        FILE *f = fopen(path.c_str(), "wb");
        if (!f) {
            return false;
        }
        bool ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr) &&
                  fwrite(pixels.data(), 1, (size_t) pixelDataSize, f) == (size_t) pixelDataSize;
        fclose(f);
        return ok;
    }

    // Pads decoded RGBA pixels to power-of-two dimensions and uploads them as a C3D texture.
    bool uploadRGBAToTexture(const unsigned char *rgba,
                             int w,
                             int h,
                             C2D_Image &image,
                             C3D_Tex &imageTex,
                             Tex3DS_SubTexture &subtex,
                             bool freeTexMem) {
        int nw = nextPow2(w);
        int nh = nextPow2(h);

        unsigned char *padded = (unsigned char *) linearAlloc((size_t) nw * nh * 4);
        if (!padded) {
            logToDebugScreen("linearAlloc failed for image");
            return false;
        }
        memset(padded, 0, (size_t) nw * nh * 4);
        for (int y = 0; y < h; ++y) {
            memcpy(padded + y * nw * 4, rgba + (size_t) y * w * 4, (size_t) w * 4);
        }

        if (freeTexMem) {
            C3D_TexDelete(&imageTex);
        }
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

    // Decodes an in-memory image and uploads it. Caller guarantees the decoded
    // size is within MAX_COVER_TEX_SIZE.
    bool uploadDecodedCover(const unsigned char *buffer,
                            int len,
                            C2D_Image &image,
                            C3D_Tex &imageTex,
                            Tex3DS_SubTexture &subtex,
                            bool freeTexMem) {
        int w = -1, h = -1;
        stbi_uc *data = stbi_load_from_memory(buffer, len, &w, &h, nullptr, 4);
        if (!data) {
            logToDebugScreen("Failed to decode image");
            return false;
        }
        bool ok = uploadRGBAToTexture(data, w, h, image, imageTex, subtex, freeTexMem);
        stbi_image_free(data);
        return ok;
    }

    // Reads a big-endian u32 field (used while parsing a METADATA_BLOCK_PICTURE buffer).
    uint32_t readU32BE(const std::string &d, size_t i) {
        return ((uint8_t) d[i] << 24) | ((uint8_t) d[i + 1] << 16) | ((uint8_t) d[i + 2] << 8) |
               (uint8_t) d[i + 3];
    }
}  // namespace

CoverCacheStats getCoverCacheStats() {
    ensureCacheStatsInitialized();
    return s_cacheStats;
}

size_t clearCoverCache() {
    size_t removed = 0;
    DIR *d = opendir(std::string(COVER_CACHE_DIR).c_str());
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != nullptr) {
            std::string name = ent->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            if (remove((std::string(COVER_CACHE_DIR) + name).c_str()) == 0) {
                ++removed;
            }
        }
        closedir(d);
    }
    s_cacheStats = {};
    s_cacheStatsInitialized = true;  // directory is now known-empty
    return removed;
}

bool loadC2DImage(const char *filepath,
                  C2D_Image &image,
                  C3D_Tex &imageTex,
                  Tex3DS_SubTexture &subtex) {
    std::vector<unsigned char> buf = readWholeFile(filepath);
    if (buf.empty()) {
        logToDebugScreen("Failed to load image");
        return false;
    }
    return loadC2DImageMemory(buf.data(), (int) buf.size(), image, imageTex, subtex, false);
}

bool loadC2DImageMemory(const unsigned char *buffer,
                        int len,
                        C2D_Image &image,
                        C3D_Tex &imageTex,
                        Tex3DS_SubTexture &subtex,
                        bool freeTexMem) {
    // Peek dimensions without a full decode.
    int w = -1, h = -1, comp = -1;
    if (!stbi_info_from_memory(buffer, len, &w, &h, &comp)) {
        logToDebugScreen("Failed to read image from memory");
        return false;
    }

    if (w <= MAX_COVER_TEX_SIZE && h <= MAX_COVER_TEX_SIZE) {
        return uploadDecodedCover(buffer, len, image, imageTex, subtex, freeTexMem);
    }

    // Oversized: when caching is enabled, check the on disk cache before
    // doing any decoding work. When disabled, skip straight to decoding (the cover still displays,
    // it just isn't persisted).
    std::string cachePath;
    struct stat cst;
    bool hadExistingCacheFile = false;
    if (g_settings.cacheCoverArt) {
        cachePath = coverCachePath(buffer, len);
        hadExistingCacheFile = stat(cachePath.c_str(), &cst) == 0;
        if (hadExistingCacheFile) {
            std::vector<unsigned char> cached = readWholeFile(cachePath);
            if (!cached.empty() &&
                uploadDecodedCover(
                    cached.data(), (int) cached.size(), image, imageTex, subtex, freeTexMem)) {
                return true;
            }
            // Cache file missing or corrupt, fall through and regenerate it.
        }
    }

    logToDebugScreen("Cover art exceeds 1024x1024, downscaling and caching (width, height): " +
                     std::to_string(w) + ", " + std::to_string(h));

    int dw = 0, dh = 0;
    std::vector<unsigned char> scaled;

    if (looksLikeJpeg(buffer, len)) {
        int jw = 0, jh = 0;
        std::vector<unsigned char> jpegPixels =
            decodeJpegScaled(buffer, len, COVER_CACHE_DIM, jw, jh);
        if (!jpegPixels.empty()) {
            scaled = downscaleRGBA(jpegPixels.data(), jw, jh, COVER_CACHE_DIM, dw, dh);
        }
    }

    if (scaled.empty()) {
        // Not a JPEG, or the scaled decode above failed for some reason, fall back to a full
        // native resolution decode via stb_image.
        stbi_uc *data = stbi_load_from_memory(buffer, len, &w, &h, nullptr, 4);
        if (!data) {
            logToDebugScreen("Failed to decode oversized cover art");
            return false;
        }
        scaled = downscaleRGBA(data, w, h, COVER_CACHE_DIM, dw, dh);
        stbi_image_free(data);
    }

    if (scaled.empty()) {
        return false;
    }

    if (g_settings.cacheCoverArt) {
        // Must happen before writing to avoid double count.
        ensureCacheStatsInitialized();
        ensureCoverCacheDirExists();
        struct stat wst;
        if (writeBmpRGBA(cachePath, scaled.data(), dw, dh) && stat(cachePath.c_str(), &wst) == 0) {
            if (hadExistingCacheFile) {
                s_cacheStats.totalBytes =
                    s_cacheStats.totalBytes - (uint64_t) cst.st_size + (uint64_t) wst.st_size;
            } else {
                ++s_cacheStats.fileCount;
                s_cacheStats.totalBytes += (uint64_t) wst.st_size;
            }
        } else {
            logToDebugScreen("Failed to cache downscaled cover art (will re-downscale next time)");
            // Not fatal: still show the downscaled image this time around.
        }
    }

    return uploadRGBAToTexture(scaled.data(), dw, dh, image, imageTex, subtex, freeTexMem);
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

#pragma once

#include <citro2d.h>
#include <citro3d.h>

#include <cstdint>
#include <string>

// Load image from a file path.
bool loadC2DImage(const char *filepath,
                  C2D_Image &image,
                  C3D_Tex &imageTex,
                  Tex3DS_SubTexture &subtex);

// Load from a memory buffer (pads to power-of-two dimensions internally).
// freeTexMem: call C3D_TexDelete on imageTex before reinitializing.
bool loadC2DImageMemory(const unsigned char *buffer,
                        int len,
                        C2D_Image &image,
                        C3D_Tex &imageTex,
                        Tex3DS_SubTexture &subtex,
                        bool freeTexMem = false);

// Draw cover art scaled to fit COVER_TARGET_WIDTH × COVER_TARGET_HEIGHT.
void drawCoverScaled(C2D_Image &image, Tex3DS_SubTexture &subtex, float x, float y);

// helpers (shared by all decoders)
// Parse a binary METADATA_BLOCK_PICTURE block (used by FLAC, Opus, Vorbis)
// and return the raw embedded image bytes (JPEG or PNG).
// Returns an empty string if the block is malformed.
std::string extractImageFromPictureBlock(const std::string &blockData);

// Upload raw image bytes (JPEG / PNG) directly to a C2D_Image.
// freeExisting: call C3D_TexDelete on tex before reinitializing.
bool loadCoverArtFromBytes(const unsigned char *data,
                           int len,
                           C2D_Image &image,
                           C3D_Tex &tex,
                           Tex3DS_SubTexture &subtex,
                           bool freeExisting);

// Fallback for when a song has no embedded cover art: looks for images alongside songPath and loads
// it if found. freeExisting: call C3D_TexDelete on tex before reinitializing.
bool loadFolderCoverArt(const std::string &songPath,
                        C2D_Image &image,
                        C3D_Tex &tex,
                        Tex3DS_SubTexture &subtex,
                        bool freeExisting);

// Target dimensions (pixels) for displayed cover art
constexpr float COVER_TARGET_WIDTH = 190.0f;
constexpr float COVER_TARGET_HEIGHT = 190.0f;

// Directory where cover art downscaled and cached as BMP files.
inline constexpr std::string_view COVER_CACHE_DIR = "sdmc:/3ds/Canary/.cache/covers/";

// Cached covers are downscaled to fit within this many pixels per side, preserving aspect ratio.
constexpr int COVER_CACHE_DIM = (COVER_TARGET_WIDTH > COVER_TARGET_HEIGHT)
                                    ? (int) COVER_TARGET_WIDTH
                                    : (int) COVER_TARGET_HEIGHT;

struct CoverCacheStats {
    size_t fileCount = 0;
    uint64_t totalBytes = 0;
};

// Current size of the on disk cover cache.
CoverCacheStats getCoverCacheStats();

// Deletes every file under COVER_CACHE_DIR. Returns the number removed.
size_t clearCoverCache();

// Scale image bytes (JPEG/PNG) to 128x128 and write as a 24-bit BMP file.
bool saveAsBmp128(const std::string &path, const unsigned char *data, int len);

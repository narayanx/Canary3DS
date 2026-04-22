#pragma once

#include <citro2d.h>
#include <citro3d.h>

#include <string>

// Load image from a file path
bool loadC2DImage(const char *filepath,
                  C2D_Image &image,
                  C3D_Tex &imageTex,
                  Tex3DS_SubTexture &subtex);

// Load from a memory buffer (pads to power-of-two dimensions internally)
// freeTexMem: call C3D_TexDelete on imageTex before reinitialising
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
// freeExisting: call C3D_TexDelete on tex before reinitialising.
bool loadCoverArtFromBytes(const unsigned char *data,
                           int len,
                           C2D_Image &image,
                           C3D_Tex &tex,
                           Tex3DS_SubTexture &subtex,
                           bool freeExisting);

// Target dimensions (pixels) for displayed cover art
constexpr float COVER_TARGET_WIDTH = 190.0f;
constexpr float COVER_TARGET_HEIGHT = 190.0f;

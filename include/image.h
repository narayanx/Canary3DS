#ifndef CANARY_IMAGE_H
#define CANARY_IMAGE_H

#include <citro3d.h>
#include <citro2d.h>

bool loadC2DImage(const char *filepath, C2D_Image &image, C3D_Tex& imageTex, Tex3DS_SubTexture &subtex);
bool loadC2DImageMemory(const unsigned char *buffer, int len, C2D_Image &image, C3D_Tex& imageTex, Tex3DS_SubTexture &subtex, bool freeTexMem=false);


#endif
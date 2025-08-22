#include <3ds.h>
#include <string.h> // memcpy

#define STBI_MALLOC(sz) linearAlloc((sz))
#define STBI_FREE(p) linearFree((p))
#define STBI_REALLOC(p,newsz) stbiLinearRealloc((p), (newsz))

void* stbiLinearRealloc(void* p, size_t size) {
    if (!p)
        return linearAlloc(size);

    void* q = linearAlloc(size);
    if (q) {
        const size_t oldSize = linearGetSize(p);
        memcpy(q, p, size < oldSize ? size : oldSize);
        linearFree(p);
    }

    return q;
}

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

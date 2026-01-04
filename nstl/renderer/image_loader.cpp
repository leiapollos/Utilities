//
// Created by André Leite on 03/01/2026.
//

#define STB_IMAGE_IMPLEMENTATION

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wcast-align"
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#include "../../third_party/stb_image.h"
#pragma clang diagnostic pop

B32 image_load_from_memory(const U8* data, U64 size, LoadedImage* out) {
    if (!data || size == 0 || !out) {
        return 0;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    U8* pixels = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 4);
    if (!pixels) {
        LOG_ERROR("image_loader", "Failed to decode image: {}", stbi_failure_reason());
        return 0;
    }

    out->pixels = pixels;
    out->width = (U32)width;
    out->height = (U32)height;
    out->channels = 4;
    return 1;
}

B32 image_load_from_file(const char* path, LoadedImage* out) {
    if (!path || !out) {
        return 0;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    U8* pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!pixels) {
        LOG_ERROR("image_loader", "Failed to load image '{}': {}", path, stbi_failure_reason());
        return 0;
    }

    out->pixels = pixels;
    out->width = (U32)width;
    out->height = (U32)height;
    out->channels = 4;
    return 1;
}

void image_free(LoadedImage* img) {
    if (img && img->pixels) {
        stbi_image_free(img->pixels);
        img->pixels = 0;
    }
}

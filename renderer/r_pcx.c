#include "r_local.h"

static uint32_t PCX_ReadLE16(uint8_t const *p) {
    return p[0] | ((uint32_t)p[1] << 8);
}

bool R_IsTexturePCX(handle_t data, uint32_t filesize) {
    uint8_t const *file = data;
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_line;

    if (!file || filesize < 128) {
        return false;
    }

    width = PCX_ReadLE16(file + 8) - PCX_ReadLE16(file + 4) + 1;
    height = PCX_ReadLE16(file + 10) - PCX_ReadLE16(file + 6) + 1;
    bytes_per_line = PCX_ReadLE16(file + 66);
    return file[0] == 0x0a &&
           file[2] == 1 &&
           file[3] == 8 &&
           file[65] == 1 &&
           width > 0 &&
           height > 0 &&
           bytes_per_line >= width;
}

texture_t *R_LoadTexturePCX(handle_t data, uint32_t filesize) {
    uint8_t const *file = data;
    uint8_t const *src;
    uint8_t const *src_end;
    uint8_t palette[256][3];
    uint8_t *rows = NULL;
    color32_t *pixels = NULL;
    texture_t *texture = NULL;
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_line;
    uint32_t row_size;
    uint32_t palette_pos;
    uint32_t out = 0;

    if (!file || filesize < 128) {
        return NULL;
    }

    width = PCX_ReadLE16(file + 8) - PCX_ReadLE16(file + 4) + 1;
    height = PCX_ReadLE16(file + 10) - PCX_ReadLE16(file + 6) + 1;
    bytes_per_line = PCX_ReadLE16(file + 66);

    if (!R_IsTexturePCX(data, filesize)) {
        return NULL;
    }

    for (uint32_t i = 0; i < 256; i++) {
        palette[i][0] = (uint8_t)i;
        palette[i][1] = (uint8_t)i;
        palette[i][2] = (uint8_t)i;
    }
    palette_pos = filesize;
    if (filesize >= 897 && file[filesize - 769] == 0x0c) {
        palette_pos = filesize - 769;
        memcpy(palette, file + palette_pos + 1, sizeof(palette));
    }

    row_size = bytes_per_line * height;
    rows = ri.MemAlloc(row_size);
    pixels = ri.MemAlloc(sizeof(color32_t) * width * height);
    if (!rows || !pixels) {
        goto done;
    }

    src = file + 128;
    src_end = file + palette_pos;
    while (out < row_size && src < src_end) {
        uint32_t run;
        uint8_t value;
        uint8_t b = *src++;

        if ((b & 0xc0) == 0xc0) {
            run = b & 0x3f;
            if (src >= src_end) {
                goto done;
            }
            value = *src++;
        } else {
            run = 1;
            value = b;
        }
        while (run-- && out < row_size) {
            rows[out++] = value;
        }
    }
    if (out < row_size) {
        goto done;
    }

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint8_t index = rows[y * bytes_per_line + x];
            color32_t *pixel = pixels + y * width + x;

            /* PCX palettes are RGB; the old swap compensated for the desktop BGRA uploader. */
            pixel->r = palette[index][0];
            pixel->g = palette[index][1];
            pixel->b = palette[index][2];
            pixel->a = index == 255 ? 0 : 255;
        }
    }

    texture = R_AllocateTexture(width, height);
    R_LoadTextureMipLevel(texture, &(texMip_t){ pixels, width, height, 0, PIXEL_RGBA });

done:
    SAFE_DELETE(rows, ri.MemFree);
    SAFE_DELETE(pixels, ri.MemFree);
    return texture;
}

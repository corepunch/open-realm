#include "r_local.h"

static DWORD PCX_ReadLE16(BYTE const *p) {
    return p[0] | ((DWORD)p[1] << 8);
}

BOOL R_IsTexturePCX(HANDLE data, DWORD filesize) {
    BYTE const *file = data;
    DWORD width;
    DWORD height;
    DWORD bytes_per_line;

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

LPTEXTURE R_LoadTexturePCX(HANDLE data, DWORD filesize) {
    BYTE const *file = data;
    BYTE const *src;
    BYTE const *src_end;
    BYTE palette[256][3];
    LPBYTE rows = NULL;
    LPCOLOR32 pixels = NULL;
    LPTEXTURE texture = NULL;
    DWORD width;
    DWORD height;
    DWORD bytes_per_line;
    DWORD row_size;
    DWORD palette_pos;
    DWORD out = 0;

    if (!file || filesize < 128) {
        return NULL;
    }

    width = PCX_ReadLE16(file + 8) - PCX_ReadLE16(file + 4) + 1;
    height = PCX_ReadLE16(file + 10) - PCX_ReadLE16(file + 6) + 1;
    bytes_per_line = PCX_ReadLE16(file + 66);

    if (!R_IsTexturePCX(data, filesize)) {
        return NULL;
    }

    for (DWORD i = 0; i < 256; i++) {
        palette[i][0] = (BYTE)i;
        palette[i][1] = (BYTE)i;
        palette[i][2] = (BYTE)i;
    }
    palette_pos = filesize;
    if (filesize >= 897 && file[filesize - 769] == 0x0c) {
        palette_pos = filesize - 769;
        memcpy(palette, file + palette_pos + 1, sizeof(palette));
    }

    row_size = bytes_per_line * height;
    rows = ri.MemAlloc(row_size);
    pixels = ri.MemAlloc(sizeof(COLOR32) * width * height);
    if (!rows || !pixels) {
        goto done;
    }

    src = file + 128;
    src_end = file + palette_pos;
    while (out < row_size && src < src_end) {
        DWORD run;
        BYTE value;
        BYTE b = *src++;

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

    for (DWORD y = 0; y < height; y++) {
        for (DWORD x = 0; x < width; x++) {
            BYTE index = rows[y * bytes_per_line + x];
            LPCOLOR32 pixel = pixels + y * width + x;

            /* PCX palettes are RGB; the old swap compensated for the desktop BGRA uploader. */
            pixel->r = palette[index][0];
            pixel->g = palette[index][1];
            pixel->b = palette[index][2];
            pixel->a = index == 255 ? 0 : 255;
        }
    }

    texture = R_AllocateTexture(width, height);
    R_LoadTextureMipLevel(texture, &(TEXMIP){ pixels, width, height, 0, PIXEL_RGBA });

done:
    SAFE_DELETE(rows, ri.MemFree);
    SAFE_DELETE(pixels, ri.MemFree);
    return texture;
}

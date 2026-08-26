#include "r_local.h"
#include "r_blp.h"

// A description of the BLP2 format can be found on Wikipedia: http://en.wikipedia.org/wiki/.BLP
struct tBLP2Header {
    DWORD    magic;
    DWORD    type;           // 0: JPEG, 1: see encoding
    BYTE     encoding;       // 1: Uncompressed, 2: DXT compression, 3: Uncompressed BGRA
    BYTE     alphaDepth;     // 0, 1, 4 or 8 bits
    BYTE     alphaEncoding;  // 0: DXT1, 1: DXT3, 7: DXT5

    union {
        BYTE     hasMipLevels;   // In BLP file: 0 or 1
        BYTE     nbMipLevels;    // For convenience, replaced with the number of mip levels
    };

    DWORD    width;          // In pixels, power-of-two
    DWORD    height;
    DWORD    offsets[16];
    DWORD    lengths[16];
    COLOR32 palette[256];   // 256 BGRA colors
};

LPCOLOR32 blp2_convert_paletted_no_alpha(BYTE* pSrc, struct tBLP2Header* pHeader, DWORD width, DWORD height);
LPCOLOR32 blp2_convert_paletted_alpha1(BYTE* pSrc, struct tBLP2Header* pHeader, DWORD width, DWORD height);
LPCOLOR32 blp2_convert_paletted_alpha4(BYTE* pSrc, struct tBLP2Header* pHeader, DWORD width, DWORD height);
LPCOLOR32 blp2_convert_paletted_alpha8(BYTE* pSrc, struct tBLP2Header* pHeader, DWORD width, DWORD height);
LPCOLOR32 blp2_convert_raw_bgra(BYTE* pSrc, struct tBLP2Header* pHeader, DWORD width, DWORD height);
LPCOLOR32 blp2_convert_dxt(BYTE* pSrc, struct tBLP2Header* pHeader, DWORD width, DWORD height, int flags);

DWORD blp2_width(struct tBLP2Header* pBLPInfos, DWORD mipLevel) {
    // Check the mip level
    if (mipLevel >= pBLPInfos->nbMipLevels)
        mipLevel = pBLPInfos->nbMipLevels - 1;
    return (pBLPInfos->width >> mipLevel);
}


DWORD blp2_height(struct tBLP2Header* pBLPInfos, DWORD mipLevel) {
    // Check the mip level
    if (mipLevel >= pBLPInfos->nbMipLevels)
        mipLevel = pBLPInfos->nbMipLevels - 1;
    return (pBLPInfos->height >> mipLevel);
}

DWORD blp2_nbMipLevels(struct tBLP2Header* pBLPInfos) {
    return pBLPInfos->nbMipLevels;
}

enum tBLPFormat blp2_format(struct tBLP2Header* pBLPInfos) {
    if (pBLPInfos->type == 0)
        return BLP_FORMAT_JPEG;

    if (pBLPInfos->encoding == BLP_ENCODING_UNCOMPRESSED)
        return (pBLPInfos->encoding << 16) | (pBLPInfos->alphaDepth << 8);

    if (pBLPInfos->encoding == BLP_ENCODING_UNCOMPRESSED_RAW_BGRA)
        return (pBLPInfos->encoding << 16);

    return (pBLPInfos->encoding << 16) | (pBLPInfos->alphaDepth << 8) | pBLPInfos->alphaEncoding;
}

LPCOLOR32 blp2_convert(HANDLE buffer, DWORD filesize, struct tBLP2Header* pBLPInfos, DWORD mipLevel) {
    // Check the mip level
    if (mipLevel >= pBLPInfos->nbMipLevels)
        mipLevel = pBLPInfos->nbMipLevels - 1;

    // Declarations
    DWORD width  = blp2_width(pBLPInfos, mipLevel);
    DWORD height = blp2_height(pBLPInfos, mipLevel);
    LPCOLOR32 pDst = 0;
    DWORD offset = pBLPInfos->offsets[mipLevel];
    DWORD size   = pBLPInfos->lengths[mipLevel];
    BYTE *pSrc = ri.MemAlloc(size);

    memcpy(pSrc, buffer + offset, size);

    switch (blp2_format(pBLPInfos)) {
        case BLP_FORMAT_JPEG:
            pDst = blp2_convert_paletted_no_alpha(pSrc, pBLPInfos, width, height);
            break;
        case BLP_FORMAT_PALETTED_NO_ALPHA:
            pDst = blp2_convert_paletted_no_alpha(pSrc, pBLPInfos, width, height);
            break;
        case BLP_FORMAT_PALETTED_ALPHA_1:
            pDst = blp2_convert_paletted_alpha1(pSrc, pBLPInfos, width, height);
            break;
        case BLP_FORMAT_PALETTED_ALPHA_4:
            pDst = blp2_convert_paletted_alpha4(pSrc, pBLPInfos, width, height);
            break;
        case BLP_FORMAT_PALETTED_ALPHA_8:
            pDst = blp2_convert_paletted_alpha8(pSrc, pBLPInfos, width, height);
            break;
        case BLP_FORMAT_RAW_BGRA:
            pDst = blp2_convert_raw_bgra(pSrc, pBLPInfos, width, height);
            break;
        case BLP_FORMAT_DXT1_NO_ALPHA:
            pDst = blp2_convert_dxt(pSrc, pBLPInfos, width, height, 0);
            break;
        case BLP_FORMAT_DXT1_ALPHA_1:
        case BLP_FORMAT_DXT3_NO_ALPHA:
            pDst = blp2_convert_dxt(pSrc, pBLPInfos, width, height, 1);
            break;
        case BLP_FORMAT_DXT3_ALPHA_4:
        case BLP_FORMAT_DXT3_ALPHA_8:
            pDst = blp2_convert_dxt(pSrc, pBLPInfos, width, height, 3);
            break;
        case BLP_FORMAT_DXT5_ALPHA_8:
            pDst = blp2_convert_dxt(pSrc, pBLPInfos, width, height, 5);
            break;
        default:
            break;
    }

    ri.MemFree(pSrc);

    return pDst;
}


LPTEXTURE R_LoadTextureBLP2(HANDLE data, DWORD filesize) {
    struct tBLP2Header* pBLPInfos = ri.MemAlloc(sizeof(struct tBLP2Header));
    BYTE hasMipLevels;
    memcpy(pBLPInfos, data, sizeof(struct tBLP2Header));
    hasMipLevels = pBLPInfos->hasMipLevels;
    if (!hasMipLevels) {
        pBLPInfos->nbMipLevels = 1;
    } else {
        pBLPInfos->nbMipLevels = 0;
        while ((pBLPInfos->offsets[pBLPInfos->nbMipLevels] != 0) &&
               (pBLPInfos->nbMipLevels < 16))
        {
            ++pBLPInfos->nbMipLevels;
        }
    }
    LPTEXTURE pTexture = R_AllocateTexture(blp2_width(pBLPInfos, 0), blp2_height(pBLPInfos, 0));
    FOR_LOOP(level, blp2_nbMipLevels(pBLPInfos)) {
        DWORD const width = blp2_width(pBLPInfos, level);
        DWORD const height = blp2_height(pBLPInfos, level);
        LPCOLOR32 pPixels = blp2_convert(data, filesize, pBLPInfos, level);
        if (pPixels) {
            R_LoadTextureMipLevel(pTexture, &(TEXMIP){ pPixels, width, height, level, PIXEL_BGRA });
            ri.MemFree(pPixels);
        }
    }
    return pTexture;
}

LPCOLOR32 blp2_convert_paletted_alpha8(BYTE* pSrc, struct tBLP2Header* pHeader, DWORD width, DWORD height) {
    LPCOLOR32 pBuffer = ri.MemAlloc(sizeof(COLOR32) * width * height);
    LPCOLOR32 pDst = pBuffer;
    BYTE* pIndices = pSrc;
    BYTE* pAlpha = pSrc + width * height;
    FOR_LOOP(y, height) {
        FOR_LOOP(x, width) {
            *pDst = pHeader->palette[*pIndices];
            pDst->a = *pAlpha;
            ++pIndices;
            ++pAlpha;
            ++pDst;
        }
    }
    return pBuffer;
}

LPCOLOR32 blp2_convert_paletted_no_alpha(BYTE* pSrc, struct tBLP2Header* pHeader, DWORD width, DWORD height) {
    LPCOLOR32 pBuffer = ri.MemAlloc(sizeof(COLOR32) * width * height);
    LPCOLOR32 pDst = pBuffer;
    FOR_LOOP(y, height) {
        FOR_LOOP(x, width) {
            *pDst = pHeader->palette[*pSrc];
            pDst->a = 0xFF;
            ++pSrc;
            ++pDst;
        }
    }
    return pBuffer;
}

LPCOLOR32 blp2_convert_paletted_alpha1(BYTE* pSrc, struct tBLP2Header* pHeader, DWORD width, DWORD height) {
    LPCOLOR32 pBuffer = ri.MemAlloc(sizeof(COLOR32) * width * height);
    LPCOLOR32 pDst = pBuffer;
    BYTE* pIndices = pSrc;
    BYTE* pAlpha = pSrc + width * height;
    BYTE counter = 0;
    FOR_LOOP(y, height) {
        FOR_LOOP(x, width) {
            *pDst = pHeader->palette[*pIndices];
            pDst->a = (*pAlpha & (1 << counter) ? 0xFF : 0x00);
            ++pIndices;
            ++pDst;
            ++counter;
            if (counter == 8) {
                ++pAlpha;
                counter = 0;
            }
        }
    }

    return pBuffer;
}

LPCOLOR32 blp2_convert_paletted_alpha4(BYTE* pSrc, struct tBLP2Header* pHeader, DWORD width, DWORD height) {
    LPCOLOR32 pBuffer = ri.MemAlloc(sizeof(COLOR32) * width * height);
    LPCOLOR32 pDst = pBuffer;
    BYTE* pIndices = pSrc;
    BYTE* pAlpha = pSrc + width * height;
    BYTE counter = 0;
    FOR_LOOP(y, height) {
        FOR_LOOP(x, width) {
            *pDst = pHeader->palette[*pIndices];
            pDst->a = (*pAlpha >> counter) & 0xF;
            // convert 4-bit range to 8-bit range
            pDst->a = (pDst->a << 4) | pDst->a;
            ++pIndices;
            ++pDst;
            counter += 4;
            if (counter == 8) {
                ++pAlpha;
                counter = 0;
            }
        }
    }

    return pBuffer;
}

static COLOR32 blp2_dxt_color(uint16_t color, BYTE alpha) {
    BYTE r = (BYTE)(((color >> 11) & 0x1f) * 255 / 31);
    BYTE g = (BYTE)(((color >> 5) & 0x3f) * 255 / 63);
    BYTE b = (BYTE)((color & 0x1f) * 255 / 31);
    return (COLOR32){ b, g, r, alpha };
}

static void blp2_dxt_write_pixel(LPCOLOR32 pixels, DWORD width, DWORD height, DWORD x, DWORD y, COLOR32 color) {
    if (x >= width || y >= height) {
        return;
    }
    pixels[y * width + x] = color;
}

static void blp2_dxt_decode_color_block(BYTE const *block, COLOR32 colors[4], BOOL four_color) {
    uint16_t c0 = (uint16_t)(block[0] | (block[1] << 8));
    uint16_t c1 = (uint16_t)(block[2] | (block[3] << 8));
    colors[0] = blp2_dxt_color(c0, 255);
    colors[1] = blp2_dxt_color(c1, 255);

    if (four_color || c0 > c1) {
        colors[2] = (COLOR32){
            (BYTE)((2 * colors[0].r + colors[1].r) / 3),
            (BYTE)((2 * colors[0].g + colors[1].g) / 3),
            (BYTE)((2 * colors[0].b + colors[1].b) / 3),
            255
        };
        colors[3] = (COLOR32){
            (BYTE)((colors[0].r + 2 * colors[1].r) / 3),
            (BYTE)((colors[0].g + 2 * colors[1].g) / 3),
            (BYTE)((colors[0].b + 2 * colors[1].b) / 3),
            255
        };
    } else {
        colors[2] = (COLOR32){
            (BYTE)((colors[0].r + colors[1].r) / 2),
            (BYTE)((colors[0].g + colors[1].g) / 2),
            (BYTE)((colors[0].b + colors[1].b) / 2),
            255
        };
        colors[3] = (COLOR32){ 0, 0, 0, 0 };
    }
}

static void blp2_dxt_decode_alpha_dxt3(BYTE const *block, BYTE alpha[16]) {
    FOR_LOOP(i, 16) {
        BYTE value = (BYTE)((block[i / 2] >> ((i & 1) * 4)) & 0x0f);
        alpha[i] = (BYTE)((value << 4) | value);
    }
}

static void blp2_dxt_decode_alpha_dxt5(BYTE const *block, BYTE alpha[16]) {
    BYTE table[8];
    uint64_t bits = 0;
    table[0] = block[0];
    table[1] = block[1];
    if (table[0] > table[1]) {
        table[2] = (BYTE)((6 * table[0] + 1 * table[1]) / 7);
        table[3] = (BYTE)((5 * table[0] + 2 * table[1]) / 7);
        table[4] = (BYTE)((4 * table[0] + 3 * table[1]) / 7);
        table[5] = (BYTE)((3 * table[0] + 4 * table[1]) / 7);
        table[6] = (BYTE)((2 * table[0] + 5 * table[1]) / 7);
        table[7] = (BYTE)((1 * table[0] + 6 * table[1]) / 7);
    } else {
        table[2] = (BYTE)((4 * table[0] + 1 * table[1]) / 5);
        table[3] = (BYTE)((3 * table[0] + 2 * table[1]) / 5);
        table[4] = (BYTE)((2 * table[0] + 3 * table[1]) / 5);
        table[5] = (BYTE)((1 * table[0] + 4 * table[1]) / 5);
        table[6] = 0;
        table[7] = 255;
    }
    FOR_LOOP(i, 6) {
        bits |= ((uint64_t)block[2 + i]) << (8 * i);
    }
    FOR_LOOP(i, 16) {
        alpha[i] = table[(bits >> (3 * i)) & 7];
    }
}

LPCOLOR32 blp2_convert_raw_bgra(BYTE* pSrc, struct tBLP2Header* pHeader, DWORD width, DWORD height) {
    (void)pHeader;
    LPCOLOR32 pBuffer = ri.MemAlloc(sizeof(COLOR32) * width * height);
    memcpy(pBuffer, pSrc, sizeof(COLOR32) * width * height);
    return pBuffer;
}

LPCOLOR32 blp2_convert_dxt(BYTE* pSrc, struct tBLP2Header* pHeader, DWORD width, DWORD height, int format) {
    LPCOLOR32 pixels = ri.MemAlloc(sizeof(COLOR32) * width * height);
    DWORD blocks_x = (width + 3) / 4;
    DWORD blocks_y = (height + 3) / 4;
    BYTE const *src = pSrc;
    (void)pHeader;

    FOR_LOOP(by, blocks_y) {
        FOR_LOOP(bx, blocks_x) {
            BYTE alpha[16];
            COLOR32 colors[4];
            BYTE const *color_block;
            uint32_t indices;

            memset(alpha, 255, sizeof(alpha));
            if (format == 3) {
                blp2_dxt_decode_alpha_dxt3(src, alpha);
                color_block = src + 8;
                src += 16;
            } else if (format == 5) {
                blp2_dxt_decode_alpha_dxt5(src, alpha);
                color_block = src + 8;
                src += 16;
            } else {
                color_block = src;
                src += 8;
            }

            blp2_dxt_decode_color_block(color_block, colors, format != 1);
            indices = (uint32_t)(color_block[4] |
                                 (color_block[5] << 8) |
                                 (color_block[6] << 16) |
                                 (color_block[7] << 24));

            FOR_LOOP(py, 4) {
                FOR_LOOP(px, 4) {
                    DWORD i = py * 4 + px;
                    COLOR32 color = colors[(indices >> (2 * i)) & 3];
                    if (format != 1) {
                        color.a = alpha[i];
                    }
                    blp2_dxt_write_pixel(pixels, width, height, bx * 4 + px, by * 4 + py, color);
                }
            }
        }
    }

    return pixels;
}

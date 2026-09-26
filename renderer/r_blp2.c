#include "r_local.h"
#include "r_blp.h"

// A description of the BLP2 format can be found on Wikipedia: http://en.wikipedia.org/wiki/.BLP
struct tBLP2Header {
    uint32_t    magic;
    uint32_t    type;           // 0: JPEG, 1: see encoding
    uint8_t     encoding;       // 1: Uncompressed, 2: DXT compression, 3: Uncompressed BGRA
    uint8_t     alphaDepth;     // 0, 1, 4 or 8 bits
    uint8_t     alphaEncoding;  // 0: DXT1, 1: DXT3, 7: DXT5

    union {
        uint8_t     hasMipLevels;   // In BLP file: 0 or 1
        uint8_t     nbMipLevels;    // For convenience, replaced with the number of mip levels
    };

    uint32_t    width;          // In pixels, power-of-two
    uint32_t    height;
    uint32_t    offsets[16];
    uint32_t    lengths[16];
    COLOR32 palette[256];   // 256 BGRA colors
};

LPCOLOR32 blp2_convert_paletted_no_alpha(uint8_t* pSrc, struct tBLP2Header* pHeader, uint32_t width, uint32_t height);
LPCOLOR32 blp2_convert_paletted_alpha1(uint8_t* pSrc, struct tBLP2Header* pHeader, uint32_t width, uint32_t height);
LPCOLOR32 blp2_convert_paletted_alpha4(uint8_t* pSrc, struct tBLP2Header* pHeader, uint32_t width, uint32_t height);
LPCOLOR32 blp2_convert_paletted_alpha8(uint8_t* pSrc, struct tBLP2Header* pHeader, uint32_t width, uint32_t height);
LPCOLOR32 blp2_convert_raw_bgra(uint8_t* pSrc, struct tBLP2Header* pHeader, uint32_t width, uint32_t height);
LPCOLOR32 blp2_convert_dxt(uint8_t* pSrc, struct tBLP2Header* pHeader, uint32_t width, uint32_t height, int flags);

uint32_t blp2_width(struct tBLP2Header* pBLPInfos, uint32_t mipLevel) {
    // Check the mip level
    if (mipLevel >= pBLPInfos->nbMipLevels)
        mipLevel = pBLPInfos->nbMipLevels - 1;
    return (pBLPInfos->width >> mipLevel);
}


uint32_t blp2_height(struct tBLP2Header* pBLPInfos, uint32_t mipLevel) {
    // Check the mip level
    if (mipLevel >= pBLPInfos->nbMipLevels)
        mipLevel = pBLPInfos->nbMipLevels - 1;
    return (pBLPInfos->height >> mipLevel);
}

uint32_t blp2_nbMipLevels(struct tBLP2Header* pBLPInfos) {
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

LPCOLOR32 blp2_convert(handle_t buffer, uint32_t filesize, struct tBLP2Header* pBLPInfos, uint32_t mipLevel) {
    // Check the mip level
    if (mipLevel >= pBLPInfos->nbMipLevels)
        mipLevel = pBLPInfos->nbMipLevels - 1;

    // Declarations
    uint32_t width  = blp2_width(pBLPInfos, mipLevel);
    uint32_t height = blp2_height(pBLPInfos, mipLevel);
    LPCOLOR32 pDst = 0;
    uint32_t offset = pBLPInfos->offsets[mipLevel];
    uint32_t size   = pBLPInfos->lengths[mipLevel];
    uint8_t *pSrc = ri.MemAlloc(size);

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


LPTEXTURE R_LoadTextureBLP2(handle_t data, uint32_t filesize) {
    struct tBLP2Header* pBLPInfos = ri.MemAlloc(sizeof(struct tBLP2Header));
    uint8_t hasMipLevels;
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
        uint32_t const width = blp2_width(pBLPInfos, level);
        uint32_t const height = blp2_height(pBLPInfos, level);
        LPCOLOR32 pPixels = blp2_convert(data, filesize, pBLPInfos, level);
        if (pPixels) {
            R_LoadTextureMipLevel(pTexture, &(TEXMIP){ pPixels, width, height, level, PIXEL_BGRA });
            ri.MemFree(pPixels);
        }
    }
    return pTexture;
}

LPCOLOR32 blp2_convert_paletted_alpha8(uint8_t* pSrc, struct tBLP2Header* pHeader, uint32_t width, uint32_t height) {
    LPCOLOR32 pBuffer = ri.MemAlloc(sizeof(COLOR32) * width * height);
    LPCOLOR32 pDst = pBuffer;
    uint8_t* pIndices = pSrc;
    uint8_t* pAlpha = pSrc + width * height;
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

LPCOLOR32 blp2_convert_paletted_no_alpha(uint8_t* pSrc, struct tBLP2Header* pHeader, uint32_t width, uint32_t height) {
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

LPCOLOR32 blp2_convert_paletted_alpha1(uint8_t* pSrc, struct tBLP2Header* pHeader, uint32_t width, uint32_t height) {
    LPCOLOR32 pBuffer = ri.MemAlloc(sizeof(COLOR32) * width * height);
    LPCOLOR32 pDst = pBuffer;
    uint8_t* pIndices = pSrc;
    uint8_t* pAlpha = pSrc + width * height;
    uint8_t counter = 0;
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

LPCOLOR32 blp2_convert_paletted_alpha4(uint8_t* pSrc, struct tBLP2Header* pHeader, uint32_t width, uint32_t height) {
    LPCOLOR32 pBuffer = ri.MemAlloc(sizeof(COLOR32) * width * height);
    LPCOLOR32 pDst = pBuffer;
    uint8_t* pIndices = pSrc;
    uint8_t* pAlpha = pSrc + width * height;
    uint8_t counter = 0;
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

static COLOR32 blp2_dxt_color(uint16_t color, uint8_t alpha) {
    uint8_t r = (uint8_t)(((color >> 11) & 0x1f) * 255 / 31);
    uint8_t g = (uint8_t)(((color >> 5) & 0x3f) * 255 / 63);
    uint8_t b = (uint8_t)((color & 0x1f) * 255 / 31);
    return (COLOR32){ b, g, r, alpha };
}

static void blp2_dxt_write_pixel(LPCOLOR32 pixels, uint32_t width, uint32_t height, uint32_t x, uint32_t y, COLOR32 color) {
    if (x >= width || y >= height) {
        return;
    }
    pixels[y * width + x] = color;
}

static void blp2_dxt_decode_color_block(uint8_t const *block, COLOR32 colors[4], bool four_color) {
    uint16_t c0 = (uint16_t)(block[0] | (block[1] << 8));
    uint16_t c1 = (uint16_t)(block[2] | (block[3] << 8));
    colors[0] = blp2_dxt_color(c0, 255);
    colors[1] = blp2_dxt_color(c1, 255);

    if (four_color || c0 > c1) {
        colors[2] = (COLOR32){
            (uint8_t)((2 * colors[0].r + colors[1].r) / 3),
            (uint8_t)((2 * colors[0].g + colors[1].g) / 3),
            (uint8_t)((2 * colors[0].b + colors[1].b) / 3),
            255
        };
        colors[3] = (COLOR32){
            (uint8_t)((colors[0].r + 2 * colors[1].r) / 3),
            (uint8_t)((colors[0].g + 2 * colors[1].g) / 3),
            (uint8_t)((colors[0].b + 2 * colors[1].b) / 3),
            255
        };
    } else {
        colors[2] = (COLOR32){
            (uint8_t)((colors[0].r + colors[1].r) / 2),
            (uint8_t)((colors[0].g + colors[1].g) / 2),
            (uint8_t)((colors[0].b + colors[1].b) / 2),
            255
        };
        colors[3] = (COLOR32){ 0, 0, 0, 0 };
    }
}

static void blp2_dxt_decode_alpha_dxt3(uint8_t const *block, uint8_t alpha[16]) {
    FOR_LOOP(i, 16) {
        uint8_t value = (uint8_t)((block[i / 2] >> ((i & 1) * 4)) & 0x0f);
        alpha[i] = (uint8_t)((value << 4) | value);
    }
}

static void blp2_dxt_decode_alpha_dxt5(uint8_t const *block, uint8_t alpha[16]) {
    uint8_t table[8];
    uint64_t bits = 0;
    table[0] = block[0];
    table[1] = block[1];
    if (table[0] > table[1]) {
        table[2] = (uint8_t)((6 * table[0] + 1 * table[1]) / 7);
        table[3] = (uint8_t)((5 * table[0] + 2 * table[1]) / 7);
        table[4] = (uint8_t)((4 * table[0] + 3 * table[1]) / 7);
        table[5] = (uint8_t)((3 * table[0] + 4 * table[1]) / 7);
        table[6] = (uint8_t)((2 * table[0] + 5 * table[1]) / 7);
        table[7] = (uint8_t)((1 * table[0] + 6 * table[1]) / 7);
    } else {
        table[2] = (uint8_t)((4 * table[0] + 1 * table[1]) / 5);
        table[3] = (uint8_t)((3 * table[0] + 2 * table[1]) / 5);
        table[4] = (uint8_t)((2 * table[0] + 3 * table[1]) / 5);
        table[5] = (uint8_t)((1 * table[0] + 4 * table[1]) / 5);
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

LPCOLOR32 blp2_convert_raw_bgra(uint8_t* pSrc, struct tBLP2Header* pHeader, uint32_t width, uint32_t height) {
    (void)pHeader;
    LPCOLOR32 pBuffer = ri.MemAlloc(sizeof(COLOR32) * width * height);
    memcpy(pBuffer, pSrc, sizeof(COLOR32) * width * height);
    return pBuffer;
}

LPCOLOR32 blp2_convert_dxt(uint8_t* pSrc, struct tBLP2Header* pHeader, uint32_t width, uint32_t height, int format) {
    LPCOLOR32 pixels = ri.MemAlloc(sizeof(COLOR32) * width * height);
    uint32_t blocks_x = (width + 3) / 4;
    uint32_t blocks_y = (height + 3) / 4;
    uint8_t const *src = pSrc;
    (void)pHeader;

    FOR_LOOP(by, blocks_y) {
        FOR_LOOP(bx, blocks_x) {
            uint8_t alpha[16];
            COLOR32 colors[4];
            uint8_t const *color_block;
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
                    uint32_t i = py * 4 + px;
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

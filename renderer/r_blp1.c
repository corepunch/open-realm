#include "r_local.h"
#include "r_blp.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_WARCRAFT3_BLP_JPEG_RGBA_BANDS
#include "stb/stb_image.h"
#undef STBI_WARCRAFT3_BLP_JPEG_RGBA_BANDS
#undef STB_IMAGE_IMPLEMENTATION

// A description of the BLP1 format can be found in the file doc/MagosBformat.txt
struct tBLP1Header
{
    uint32_t    magic;
    uint32_t    type;           // 0: JPEG, 1: palette
    uint32_t    alphaBits;      // 0, 1, 4 or 8 bits
    uint32_t    width;          // In pixels, power-of-two
    uint32_t    height;
    uint32_t    extra;          // Usually 4 or 5, unknown purpose
    uint32_t    hasMipmaps;     // 0 or non-zero
    uint32_t    offsets[16];
    uint32_t    lengths[16];
};


// Additional informations about a BLP1 file
struct tBLP1Infos
{
    uint8_t nbMipLevels;    // The number of mip levels

    union {
        color32_t  palette[256];   // 256 BGRA colors

        struct {
            uint32_t headerSize;
            uint8_t* header;        // Shared between all mipmap levels
        } jpeg;
    };
};


// Internal representation of any BLP header
struct tInternalBLPInfos {
    struct tBLP1Header header;
    struct tBLP1Infos  infos;
};

color32_t *blp1_convert_jpeg(uint8_t* pSrc, struct tBLP1Infos* pInfos, uint32_t size);
color32_t *blp1_convert_paletted_no_alpha(uint8_t* pSrc, struct tBLP1Infos* pInfos, uint32_t width, uint32_t height);
color32_t *blp1_convert_paletted_separated_alpha(uint8_t* pSrc, struct tBLP1Infos* pInfos, uint32_t width, uint32_t height);
color32_t *blp1_convert_paletted_alpha1(uint8_t* pSrc, struct tBLP1Infos* pInfos, uint32_t width, uint32_t height);
color32_t *blp1_convert_paletted_alpha4(uint8_t* pSrc, struct tBLP1Infos* pInfos, uint32_t width, uint32_t height);

void blp1_release(struct tInternalBLPInfos* pBLPInfos) {
    if (pBLPInfos->header.type == 0)
        ri.MemFree(pBLPInfos->infos.jpeg.header);
    ri.MemFree(pBLPInfos);
}

enum tBLPFormat blp1_format(struct tInternalBLPInfos* pBLPInfos) {
    if (pBLPInfos->header.type == 0)
        return BLP_FORMAT_JPEG;
    switch (pBLPInfos->header.alphaBits) {
        case BLP_ALPHA_DEPTH_8:
            return BLP_FORMAT_PALETTED_ALPHA_8;
        case BLP_ALPHA_DEPTH_4:
            return BLP_FORMAT_PALETTED_ALPHA_4;
        case BLP_ALPHA_DEPTH_1:
            return BLP_FORMAT_PALETTED_ALPHA_1;
        case BLP_ALPHA_DEPTH_0:
        default:
            return BLP_FORMAT_PALETTED_NO_ALPHA;
    }
}


uint32_t blp1_width(struct tInternalBLPInfos* pBLPInfos, uint32_t mipLevel) {
    // Check the mip level
    if (mipLevel >= pBLPInfos->infos.nbMipLevels)
        mipLevel = pBLPInfos->infos.nbMipLevels - 1;
    return (pBLPInfos->header.width >> mipLevel);
}


uint32_t blp1_height(struct tInternalBLPInfos* pBLPInfos, uint32_t mipLevel) {
    // Check the mip level
    if (mipLevel >= pBLPInfos->infos.nbMipLevels)
        mipLevel = pBLPInfos->infos.nbMipLevels - 1;
    return (pBLPInfos->header.height >> mipLevel);
}


uint32_t blp1_nbMipLevels(struct tInternalBLPInfos* pBLPInfos) {
    return pBLPInfos->infos.nbMipLevels;
}

static color32_t *blp1_convert(handle_t buffer, uint32_t filesize, struct tInternalBLPInfos* pBLPInfos,
                               uint32_t mipLevel, bool force_opaque_jpeg_alpha) {
    // Check the mip level
    if (mipLevel >= pBLPInfos->infos.nbMipLevels)
        mipLevel = pBLPInfos->infos.nbMipLevels - 1;
    // Declarations
    uint32_t width  = blp1_width(pBLPInfos, mipLevel);
    uint32_t height = blp1_height(pBLPInfos, mipLevel);
    color32_t *pDst = 0;
    uint32_t offset = pBLPInfos->header.offsets[mipLevel];
    uint32_t size   = pBLPInfos->header.lengths[mipLevel];
    uint8_t* pSrc = ri.MemAlloc(size);
    memcpy(pSrc, buffer + offset, size);
    switch (blp1_format(pBLPInfos)) {
        case BLP_FORMAT_JPEG:
            pDst = blp1_convert_jpeg(pSrc, &pBLPInfos->infos, size);
            /* Base.blp declares no alpha but stores zeros in the alpha band of lower mips. */
            if (pDst && force_opaque_jpeg_alpha && !pBLPInfos->header.alphaBits)
                FOR_LOOP(p, width * height) pDst[p].a = 255;
            break;
        case BLP_FORMAT_PALETTED_NO_ALPHA:
            pDst = blp1_convert_paletted_no_alpha(pSrc, &pBLPInfos->infos, width, height);
            break;
        case BLP_FORMAT_PALETTED_ALPHA_1:
            pDst = blp1_convert_paletted_alpha1(pSrc, &pBLPInfos->infos, width, height);
            break;
        case BLP_FORMAT_PALETTED_ALPHA_4:
            pDst = blp1_convert_paletted_alpha4(pSrc, &pBLPInfos->infos, width, height);
            break;
        case BLP_FORMAT_PALETTED_ALPHA_8:
            pDst = blp1_convert_paletted_separated_alpha(pSrc, &pBLPInfos->infos, width, height);
            break;
        default:
            assert(0);
            break;
    }

    ri.MemFree(pSrc);

    return pDst;
}

static bool blp1_is_base_texture(cstring_t name) {
    static char const suffix[] = "textures\\base.blp";
    size_t const length = name ? strlen(name) : 0;
    size_t const suffix_length = sizeof(suffix) - 1;
    if (length < suffix_length ||
        (length > suffix_length && name[length - suffix_length - 1] != '\\' &&
         name[length - suffix_length - 1] != '/')) return false;
    FOR_LOOP(i, suffix_length) {
        char c = name[length - suffix_length + i] == '/' ? '\\' : name[length - suffix_length + i];
        if ((char)tolower((unsigned char)c) != suffix[i]) return false;
    }
    return true;
}

texture_t *R_LoadTextureBLP1Named(handle_t data, uint32_t filesize, cstring_t texture_name) {
    struct tInternalBLPInfos* pBLPInfos = ri.MemAlloc(sizeof(struct tInternalBLPInfos));
    bool const force_opaque_jpeg_alpha = blp1_is_base_texture(texture_name);
    memcpy(&pBLPInfos->header, data, sizeof(struct tBLP1Header));
    pBLPInfos->infos.nbMipLevels = 0;
    while ((pBLPInfos->header.offsets[pBLPInfos->infos.nbMipLevels] != 0) &&
           (pBLPInfos->infos.nbMipLevels < 16))
    {
        ++pBLPInfos->infos.nbMipLevels;
    }
    if (pBLPInfos->header.type == 0) {
        memcpy(&pBLPInfos->infos.jpeg.headerSize, data + sizeof(struct tBLP1Header), sizeof(uint32_t));
        if (pBLPInfos->infos.jpeg.headerSize > 0) {
            pBLPInfos->infos.jpeg.header = ri.MemAlloc(pBLPInfos->infos.jpeg.headerSize);
            memcpy(pBLPInfos->infos.jpeg.header, data + sizeof(struct tBLP1Header) + sizeof(uint32_t), pBLPInfos->infos.jpeg.headerSize);
        } else {
            pBLPInfos->infos.jpeg.header = 0;
        }
    } else {
        memcpy(&pBLPInfos->infos.palette, data + sizeof(struct tBLP1Header), sizeof(pBLPInfos->infos.palette));
    }

    texture_t *pTexture = R_AllocateTexture(blp1_width(pBLPInfos, 0), blp1_height(pBLPInfos, 0));

    FOR_LOOP(level, blp1_nbMipLevels(pBLPInfos)) {
        uint32_t const width = blp1_width(pBLPInfos, level);
        uint32_t const height = blp1_height(pBLPInfos, level);
        color32_t *pPixels = blp1_convert(data, filesize, pBLPInfos, level, force_opaque_jpeg_alpha);
        if (pPixels) {
            R_LoadTextureMipLevel(pTexture, &(texMip_t){ pPixels, width, height, level, PIXEL_BGRA });
            ri.MemFree(pPixels);
        }
    }

    return pTexture;
}

texture_t *R_LoadTextureBLP1(handle_t data, uint32_t filesize) {
    return R_LoadTextureBLP1Named(data, filesize, NULL);
}

color32_t *blp1_convert_jpeg(uint8_t* pSrc, struct tBLP1Infos* pInfos, uint32_t dataSize) {
    uint8_t* pSrcBuffer = ri.MemAlloc(pInfos->jpeg.headerSize + dataSize);

    if (pInfos->jpeg.headerSize > 0) {
        memcpy(pSrcBuffer, pInfos->jpeg.header, pInfos->jpeg.headerSize);
    }
    memcpy(pSrcBuffer + pInfos->jpeg.headerSize, pSrc, dataSize);

    int width;
    int height;
    uint8_t* image = stbi_load_from_memory(
        pSrcBuffer,
        (int)(pInfos->jpeg.headerSize + dataSize),
        &width,
        &height,
        NULL,
        STBI_rgb_alpha);

    if (!image) {
        ri.MemFree(pSrcBuffer);
        return NULL;
    }

    color32_t *pBuffer = ri.MemAlloc(sizeof(color32_t) * width * height);

    for (uint32_t p = 0; p < (uint32_t)(width * height); ++p) {
        pBuffer[p].r = image[p * 4 + 2];
        pBuffer[p].g = image[p * 4 + 1];
        pBuffer[p].b = image[p * 4 + 0];
        pBuffer[p].a = image[p * 4 + 3];
    }

    stbi_image_free(image);
    ri.MemFree(pSrcBuffer);

    return pBuffer;
}

color32_t *blp1_convert_paletted_separated_alpha(uint8_t* pSrc, struct tBLP1Infos* pInfos, uint32_t width, uint32_t height) {
    color32_t *pBuffer = ri.MemAlloc(sizeof(color32_t) * width * height);
    color32_t *pDst = pBuffer;
    uint8_t* pIndices = pSrc;
    uint8_t* pAlpha = pSrc + width * height;
    FOR_LOOP(y, height) {
        FOR_LOOP(x, width) {
            *pDst = pInfos->palette[*pIndices];
            pDst->a = *pAlpha;
            ++pIndices;
            ++pAlpha;
            ++pDst;
        }
    }
    return pBuffer;
}

color32_t *blp1_convert_paletted_alpha1(uint8_t* pSrc, struct tBLP1Infos* pInfos, uint32_t width, uint32_t height) {
    color32_t *pBuffer = ri.MemAlloc(sizeof(color32_t) * width * height);
    color32_t *pDst = pBuffer;
    uint8_t* pIndices = pSrc;
    uint8_t* pAlpha = pSrc + width * height;
    uint8_t counter = 0;
    FOR_LOOP(y, height) {
        FOR_LOOP(x, width) {
            *pDst = pInfos->palette[*pIndices];
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

color32_t *blp1_convert_paletted_alpha4(uint8_t* pSrc, struct tBLP1Infos* pInfos, uint32_t width, uint32_t height) {
    color32_t *pBuffer = ri.MemAlloc(sizeof(color32_t) * width * height);
    color32_t *pDst = pBuffer;
    uint8_t* pIndices = pSrc;
    uint8_t* pAlpha = pSrc + width * height;
    uint8_t counter = 0;
    FOR_LOOP(y, height) {
        FOR_LOOP(x, width) {
            uint8_t a;
            *pDst = pInfos->palette[*pIndices];
            a = (uint8_t)((*pAlpha >> counter) & 0xF);
            pDst->a = (uint8_t)((a << 4) | a);
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

color32_t *blp1_convert_paletted_no_alpha(uint8_t* pSrc, struct tBLP1Infos* pInfos, uint32_t width, uint32_t height) {
    color32_t *pBuffer = ri.MemAlloc(sizeof(color32_t) * width * height);
    color32_t *pDst = pBuffer;
    uint8_t* pIndices = pSrc;
    FOR_LOOP(y, height) {
        FOR_LOOP(x, width) {
            *pDst = pInfos->palette[*pIndices];
            pDst->a = 0xFF;
            ++pIndices;
            ++pDst;
        }
    }
    return pBuffer;
}

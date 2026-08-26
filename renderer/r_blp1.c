#include "r_local.h"
#include "r_blp.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_WARCRAFT3_BLP_JPEG_RGBA_BANDS
#include "stb/stb_image.h"
#undef STBI_WARCRAFT3_BLP_JPEG_RGBA_BANDS
#undef STB_IMAGE_IMPLEMENTATION

// Opaque type representing a BLP file
typedef void* tBLPInfos;

// A description of the BLP1 format can be found in the file doc/MagosBformat.txt
struct tBLP1Header
{
    DWORD    magic;
    DWORD    type;           // 0: JPEG, 1: palette
    DWORD    alphaBits;      // 0, 1, 4 or 8 bits
    DWORD    width;          // In pixels, power-of-two
    DWORD    height;
    DWORD    extra;          // Usually 4 or 5, unknown purpose
    DWORD    hasMipmaps;     // 0 or non-zero
    DWORD    offsets[16];
    DWORD    lengths[16];
};


// Additional informations about a BLP1 file
struct tBLP1Infos
{
    BYTE nbMipLevels;    // The number of mip levels

    union {
        COLOR32  palette[256];   // 256 BGRA colors

        struct {
            DWORD headerSize;
            BYTE* header;        // Shared between all mipmap levels
        } jpeg;
    };
};


// Internal representation of any BLP header
struct tInternalBLPInfos {
    struct tBLP1Header header;
    struct tBLP1Infos  infos;
};

LPCOLOR32 blp1_convert_jpeg(BYTE* pSrc, struct tBLP1Infos* pInfos, DWORD size);
LPCOLOR32 blp1_convert_paletted_no_alpha(BYTE* pSrc, struct tBLP1Infos* pInfos, DWORD width, DWORD height);
LPCOLOR32 blp1_convert_paletted_separated_alpha(BYTE* pSrc, struct tBLP1Infos* pInfos, DWORD width, DWORD height);
LPCOLOR32 blp1_convert_paletted_alpha1(BYTE* pSrc, struct tBLP1Infos* pInfos, DWORD width, DWORD height);
LPCOLOR32 blp1_convert_paletted_alpha4(BYTE* pSrc, struct tBLP1Infos* pInfos, DWORD width, DWORD height);

//struct tInternalBLPInfos *blp_processFile(FILE* pFile);
//void blp_release(tBLPInfos binfos);
//
//BYTE blp_version(tBLPInfos binfos);
//tBLPFormat blp_format(tBLPInfos binfos);
//
//DWORD blp_width(tBLPInfos binfos, DWORD mipLevel = 0);
//DWORD blp_height(tBLPInfos binfos, DWORD mipLevel = 0);
//DWORD blp_nbMipLevels(tBLPInfos binfos);
//
//color32* blp_convert(FILE* pFile, tBLPInfos binfos, DWORD mipLevel = 0);


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


DWORD blp1_width(struct tInternalBLPInfos* pBLPInfos, DWORD mipLevel) {
    // Check the mip level
    if (mipLevel >= pBLPInfos->infos.nbMipLevels)
        mipLevel = pBLPInfos->infos.nbMipLevels - 1;
    return (pBLPInfos->header.width >> mipLevel);
}


DWORD blp1_height(struct tInternalBLPInfos* pBLPInfos, DWORD mipLevel) {
    // Check the mip level
    if (mipLevel >= pBLPInfos->infos.nbMipLevels)
        mipLevel = pBLPInfos->infos.nbMipLevels - 1;
    return (pBLPInfos->header.height >> mipLevel);
}


DWORD blp1_nbMipLevels(struct tInternalBLPInfos* pBLPInfos) {
    return pBLPInfos->infos.nbMipLevels;
}

LPCOLOR32 blp1_convert(HANDLE buffer, DWORD filesize, struct tInternalBLPInfos* pBLPInfos, DWORD mipLevel) {
    // Check the mip level
    if (mipLevel >= pBLPInfos->infos.nbMipLevels)
        mipLevel = pBLPInfos->infos.nbMipLevels - 1;
    // Declarations
    DWORD width  = blp1_width(pBLPInfos, mipLevel);
    DWORD height = blp1_height(pBLPInfos, mipLevel);
    LPCOLOR32 pDst = 0;
    DWORD offset = pBLPInfos->header.offsets[mipLevel];
    DWORD size   = pBLPInfos->header.lengths[mipLevel];
    BYTE* pSrc = ri.MemAlloc(size);
    memcpy(pSrc, buffer + offset, size);
    switch (blp1_format(pBLPInfos)) {
        case BLP_FORMAT_JPEG:
            pDst = blp1_convert_jpeg(pSrc, &pBLPInfos->infos, size);
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

LPTEXTURE R_LoadTextureBLP1(HANDLE data, DWORD filesize) {
    struct tInternalBLPInfos* pBLPInfos = ri.MemAlloc(sizeof(struct tInternalBLPInfos));
    memcpy(&pBLPInfos->header, data, sizeof(struct tBLP1Header));
    pBLPInfos->infos.nbMipLevels = 0;
    while ((pBLPInfos->header.offsets[pBLPInfos->infos.nbMipLevels] != 0) &&
           (pBLPInfos->infos.nbMipLevels < 16))
    {
        ++pBLPInfos->infos.nbMipLevels;
    }
    if (pBLPInfos->header.type == 0) {
        memcpy(&pBLPInfos->infos.jpeg.headerSize, data + sizeof(struct tBLP1Header), sizeof(DWORD));
        if (pBLPInfos->infos.jpeg.headerSize > 0) {
            pBLPInfos->infos.jpeg.header = ri.MemAlloc(pBLPInfos->infos.jpeg.headerSize);
            memcpy(pBLPInfos->infos.jpeg.header, data + sizeof(struct tBLP1Header) + sizeof(DWORD), pBLPInfos->infos.jpeg.headerSize);
        } else {
            pBLPInfos->infos.jpeg.header = 0;
        }
    } else {
        memcpy(&pBLPInfos->infos.palette, data + sizeof(struct tBLP1Header), sizeof(pBLPInfos->infos.palette));
    }

    LPTEXTURE pTexture = R_AllocateTexture(blp1_width(pBLPInfos, 0), blp1_height(pBLPInfos, 0));

    FOR_LOOP(level, blp1_nbMipLevels(pBLPInfos)) {
        DWORD const width = blp1_width(pBLPInfos, level);
        DWORD const height = blp1_height(pBLPInfos, level);
        LPCOLOR32 pPixels = blp1_convert(data, filesize, pBLPInfos, level);
        if (pPixels) {
            R_LoadTextureMipLevel(pTexture, &(TEXMIP){ pPixels, width, height, level, PIXEL_BGRA });
            ri.MemFree(pPixels);
        }
    }

    return pTexture;
}

LPCOLOR32 blp1_convert_jpeg(BYTE* pSrc, struct tBLP1Infos* pInfos, DWORD dataSize) {
    BYTE* pSrcBuffer = ri.MemAlloc(pInfos->jpeg.headerSize + dataSize);

    if (pInfos->jpeg.headerSize > 0) {
        memcpy(pSrcBuffer, pInfos->jpeg.header, pInfos->jpeg.headerSize);
    }
    memcpy(pSrcBuffer + pInfos->jpeg.headerSize, pSrc, dataSize);

    int width;
    int height;
    BYTE* image = stbi_load_from_memory(
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

    LPCOLOR32 pBuffer = ri.MemAlloc(sizeof(COLOR32) * width * height);

    for (DWORD p = 0; p < (DWORD)(width * height); ++p) {
        pBuffer[p].r = image[p * 4 + 2];
        pBuffer[p].g = image[p * 4 + 1];
        pBuffer[p].b = image[p * 4 + 0];
        pBuffer[p].a = image[p * 4 + 3];
    }

    stbi_image_free(image);
    ri.MemFree(pSrcBuffer);

    return pBuffer;
}

LPCOLOR32 blp1_convert_paletted_separated_alpha(BYTE* pSrc, struct tBLP1Infos* pInfos, DWORD width, DWORD height) {
    LPCOLOR32 pBuffer = ri.MemAlloc(sizeof(COLOR32) * width * height);
    LPCOLOR32 pDst = pBuffer;
    BYTE* pIndices = pSrc;
    BYTE* pAlpha = pSrc + width * height;
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

LPCOLOR32 blp1_convert_paletted_alpha1(BYTE* pSrc, struct tBLP1Infos* pInfos, DWORD width, DWORD height) {
    LPCOLOR32 pBuffer = ri.MemAlloc(sizeof(COLOR32) * width * height);
    LPCOLOR32 pDst = pBuffer;
    BYTE* pIndices = pSrc;
    BYTE* pAlpha = pSrc + width * height;
    BYTE counter = 0;
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

LPCOLOR32 blp1_convert_paletted_alpha4(BYTE* pSrc, struct tBLP1Infos* pInfos, DWORD width, DWORD height) {
    LPCOLOR32 pBuffer = ri.MemAlloc(sizeof(COLOR32) * width * height);
    LPCOLOR32 pDst = pBuffer;
    BYTE* pIndices = pSrc;
    BYTE* pAlpha = pSrc + width * height;
    BYTE counter = 0;
    FOR_LOOP(y, height) {
        FOR_LOOP(x, width) {
            BYTE a;
            *pDst = pInfos->palette[*pIndices];
            a = (BYTE)((*pAlpha >> counter) & 0xF);
            pDst->a = (BYTE)((a << 4) | a);
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

LPCOLOR32 blp1_convert_paletted_no_alpha(BYTE* pSrc, struct tBLP1Infos* pInfos, DWORD width, DWORD height) {
    LPCOLOR32 pBuffer = ri.MemAlloc(sizeof(COLOR32) * width * height);
    LPCOLOR32 pDst = pBuffer;
    BYTE* pIndices = pSrc;
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

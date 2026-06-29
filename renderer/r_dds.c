#include "r_local.h"

#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT  0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3

LPCTEXTURE dds = NULL;

static void DDS_ParseHeader(BYTE const *buf, DWORD *headerSize, DWORD *width, DWORD *height, DWORD *mipMapCount) {
    *headerSize   = (buf[4])  | (buf[5]  << 8) | (buf[6]  << 16) | (buf[7]  << 24);
    *height       = (buf[12]) | (buf[13] << 8) | (buf[14] << 16) | (buf[15] << 24);
    *width        = (buf[16]) | (buf[17] << 8) | (buf[18] << 16) | (buf[19] << 24);
    *mipMapCount  = (buf[28]) | (buf[29] << 8) | (buf[30] << 16) | (buf[31] << 24);
}

static void DDS_ParsePixelFormat(BYTE const *buf, DWORD *flags, DWORD *fourcc, DWORD *rgbBitCount,
                                  DWORD *rMask, DWORD *gMask, DWORD *bMask, DWORD *aMask) {
    /* Pixel format struct begins at header offset 76. */
    BYTE const *pf = buf + 76;
    *flags        = (pf[4])  | (pf[5]  << 8) | (pf[6]  << 16) | (pf[7]  << 24);
    *fourcc       = (pf[8])  | (pf[9]  << 8) | (pf[10] << 16) | (pf[11] << 24);
    *rgbBitCount  = (pf[12]) | (pf[13] << 8) | (pf[14] << 16) | (pf[15] << 24);
    *rMask        = (pf[16]) | (pf[17] << 8) | (pf[18] << 16) | (pf[19] << 24);
    *gMask        = (pf[20]) | (pf[21] << 8) | (pf[22] << 16) | (pf[23] << 24);
    *bMask        = (pf[24]) | (pf[25] << 8) | (pf[26] << 16) | (pf[27] << 24);
    *aMask        = (pf[28]) | (pf[29] << 8) | (pf[30] << 16) | (pf[31] << 24);
}

static void DDS_UnsupportedOnce(void) {
    static BOOL warned;
    if (!warned) {
        fprintf(stderr, "BC4U/BC4S/ATI2/BC55/R8G8_B8G8/G8R8_G8B8/UYVY-packed/YUY2-packed unsupported\n");
        warned = true;
    }
}

LPTEXTURE R_LoadTextureDDS(HANDLE data, DWORD filesize) {
    BYTE const *buf = data;

    DWORD headerSize, width, height, mipMapCount;
    DDS_ParseHeader(buf, &headerSize, &width, &height, &mipMapCount);

    DWORD flags, fourcc, rgbBitCount, rMask, gMask, bMask, aMask;
    DDS_ParsePixelFormat(buf, &flags, &fourcc, &rgbBitCount, &rMask, &gMask, &bMask, &aMask);

    BOOL const hasAlpha = (flags & 0x1) != 0;
    BOOL const isFourCC = (flags & 0x4) != 0;
    BOOL const isRGB    = (flags & 0x40) != 0;

    DWORD pixelOffset = headerSize + 4;

    LPTEXTURE texture = ri.MemAlloc(sizeof(TEXTURE));
    R_Call(glGenTextures, 1, &texture->texid);
    R_Call(glBindTexture, GL_TEXTURE_2D, texture->texid);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    if (isFourCC && fourcc != 0) {
        /* Compressed DXT1/DXT3/DXT5 (DX10 fourCC 'DX10' is unsupported). */
        DWORD format, blockSize;
        if ((fourcc & 0xFF) == 'D' && ((fourcc >> 8) & 0xFF) == 'X' &&
            ((fourcc >> 16) & 0xFF) == 'T') {
            switch ((fourcc >> 24) & 0xFF) {
                case '1': format = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;  blockSize = 8;  break;
                case '3': format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; blockSize = 16; break;
                case '5': format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; blockSize = 16; break;
                default:
                    DDS_UnsupportedOnce();
                    R_Call(glDeleteTextures, 1, &texture->texid);
                    ri.MemFree(texture);
                    return NULL;
            }
        } else {
            DDS_UnsupportedOnce();
            R_Call(glDeleteTextures, 1, &texture->texid);
            ri.MemFree(texture);
            return NULL;
        }

        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

        DWORD offset = 0, w = width, h = height;
        for (DWORD i = 0; i < mipMapCount; i++) {
            if (w == 0 || h == 0) { mipMapCount--; continue; }
            DWORD size = ((w + 3) / 4) * ((h + 3) / 4) * blockSize;
            R_Call(glCompressedTexImage2D, GL_TEXTURE_2D, i, format, w, h, 0, size, buf + pixelOffset + offset);
            offset += size;
            w /= 2; h /= 2;
        }
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1);
    } else if (isRGB || (flags & 0x20000)) { /* RGB or luminance */
        GLint internalFormat;
        GLenum format;
        GLenum type = GL_UNSIGNED_BYTE;
        DWORD bpp = rgbBitCount / 8;

        if (rgbBitCount == 32 && rMask == 0x00FF0000 && gMask == 0x0000FF00 &&
            bMask == 0x000000FF && aMask == 0xFF000000) {
            internalFormat = GL_RGBA;
            format = GL_BGRA;
        } else if (rgbBitCount == 32 && rMask == 0xFF0000 && gMask == 0x00FF00 &&
                   bMask == 0x0000FF && aMask == 0xFF000000) {
            internalFormat = GL_RGBA;
            format = GL_BGRA;
        } else if (rgbBitCount == 24 && rMask == 0xFF0000 && gMask == 0x00FF00 && bMask == 0x0000FF) {
            internalFormat = GL_RGB;
            format = GL_BGR;
        } else if (rgbBitCount == 32 && rMask == 0x000000FF && gMask == 0x0000FF00 &&
                   bMask == 0x00FF0000 && aMask == 0xFF000000) {
            internalFormat = GL_RGBA;
            format = GL_RGBA;
        } else if (rgbBitCount == 24 && rMask == 0x000000FF && gMask == 0x0000FF00 && bMask == 0x00FF0000) {
            internalFormat = GL_RGB;
            format = GL_RGB;
        } else {
            DDS_UnsupportedOnce();
            R_Call(glDeleteTextures, 1, &texture->texid);
            ri.MemFree(texture);
            return NULL;
        }

        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipMapCount ? mipMapCount - 1 : 0);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipMapCount > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);

        DWORD offset = 0, w = width, h = height;
        DWORD levels = mipMapCount > 0 ? mipMapCount : 1;
        for (DWORD i = 0; i < levels; i++) {
            if (w == 0 || h == 0) { levels--; continue; }
            DWORD pitch = w * bpp;
            R_Call(glTexImage2D, GL_TEXTURE_2D, i, internalFormat, w, h, 0, format, type, buf + pixelOffset + offset);
            offset += pitch * h;
            w = MAX(w / 2, 1);
            h = MAX(h / 2, 1);
        }
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
    } else {
        DDS_UnsupportedOnce();
        R_Call(glDeleteTextures, 1, &texture->texid);
        ri.MemFree(texture);
        return NULL;
    }

    if (!dds) dds = texture;
    texture->width = width;
    texture->height = height;
    return texture;
}

#include "r_local.h"

#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT       0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT      0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT      0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT      0x83F3
#define GL_COMPRESSED_RED_RGTC1_EXT           0x8DBB
#define GL_COMPRESSED_SIGNED_RED_RGTC1_EXT    0x8DBC
#define GL_COMPRESSED_RG_RGTC2_EXT            0x8DBD
#define GL_COMPRESSED_SIGNED_RG_RGTC2_EXT     0x8DBE

LPCTEXTURE dds=NULL;

LPTEXTURE R_LoadTextureDDS(HANDLE data, DWORD filesize) {
    unsigned int blockSize;
    unsigned int format;
    BYTE const *buf = data;

    /* DDS header: magic(4) + headerSize(4) + flags(4) + height(4) + width(4) + ... + pixelFormat at offset 76 */
    unsigned int headerSize = (buf[4]) | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);
    unsigned int height = (buf[12]) | (buf[13] << 8) | (buf[14] << 16) | (buf[15] << 24);
    unsigned int width = (buf[16]) | (buf[17] << 8) | (buf[18] << 16) | (buf[19] << 24);
    unsigned int mipMapCount = (buf[28]) | (buf[29] << 8) | (buf[30] << 16) | (buf[31] << 24);
    if (mipMapCount == 0) mipMapCount = 1;

    /* FourCC is at offset 84 (pixel format starts at 76, fourCC at +8) */
    DWORD fourCC = buf[84] | (buf[85] << 8) | (buf[86] << 16) | (buf[87] << 24);

    /* Map FourCC to OpenGL compressed format */
    if (fourCC == 0x31545844) {       /* 'DXT1' */
        format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
        blockSize = 8;
    } else if (fourCC == 0x33545844) { /* 'DXT3' */
        format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        blockSize = 16;
    } else if (fourCC == 0x35545844) { /* 'DXT5' */
        format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        blockSize = 16;
    } else if (fourCC == 0x55344342) { /* 'BC4U' */
        format = GL_COMPRESSED_RED_RGTC1_EXT;
        blockSize = 8;
    } else if (fourCC == 0x53344342) { /* 'BC4S' */
        format = GL_COMPRESSED_SIGNED_RED_RGTC1_EXT;
        blockSize = 8;
    } else if (fourCC == 0x32495441) { /* 'ATI2' */
        format = GL_COMPRESSED_RG_RGTC2_EXT;
        blockSize = 16;
    } else if (fourCC == 0x00000000) {
        /* No FourCC: uncompressed DDS or DX10 */
        DWORD pfFlags = buf[80] | (buf[81] << 8) | (buf[82] << 16) | (buf[83] << 24);
        DWORD pfSize  = buf[76] | (buf[77] << 8) | (buf[78] << 16) | (buf[79] << 24);
        if (pfSize == 0x30 && (pfFlags & 0x4)) {
            DWORD dx10FourCC = buf[128] | (buf[129] << 8) | (buf[130] << 16) | (buf[131] << 24);
            fprintf(stderr, "R_LoadTextureDDS: DX10 FourCC=0x%08X not supported\n", dx10FourCC);
            return NULL;
        }
        /* Uncompressed RGB/RGBA */
        if ((pfFlags & 0x40) || (pfFlags & 0x40000)) {
            int bpp = (buf[88] | (buf[89] << 8));
            int hasAlpha = (pfFlags & 0x01) ? 1 : 0;
            int glFormat = hasAlpha ? GL_RGBA : GL_RGB;
            int glType = GL_UNSIGNED_BYTE;

            LPTEXTURE texture = ri.MemAlloc(sizeof(TEXTURE));
            R_Call(glGenTextures, 1, &texture->texid);
            R_Call(glBindTexture, GL_TEXTURE_2D, texture->texid);
            R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1);
            R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

            BYTE *pixels = (BYTE *)buf + (headerSize + 4);
            unsigned int w = width, h = height;
            for (unsigned int i = 0; i < mipMapCount; i++) {
                if (w == 0 || h == 0) { mipMapCount--; continue; }
                unsigned int rowBytes = w * (bpp / 8);
                R_Call(glTexImage2D, GL_TEXTURE_2D, i, glFormat, w, h, 0, glFormat, glType, pixels);
                pixels += rowBytes * h;
                w /= 2; h /= 2;
            }
            R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1);
            texture->width = width;
            texture->height = height;
            if (!dds) dds = texture;
            return texture;
        }
        /* Luminance or unusual flags */
        if (pfFlags == 0x00020001) {
            /* DDPF_ALPHA | DDPF_LUMINANCE — treat as single-channel alpha mask */
            int bpp = (buf[88] | (buf[89] << 8));
            LPTEXTURE texture = ri.MemAlloc(sizeof(TEXTURE));
            R_Call(glGenTextures, 1, &texture->texid);
            R_Call(glBindTexture, GL_TEXTURE_2D, texture->texid);
            R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1);
            R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

            BYTE *pixels = (BYTE *)buf + (headerSize + 4);
            unsigned int w = width, h = height;
            for (unsigned int i = 0; i < mipMapCount; i++) {
                if (w == 0 || h == 0) { mipMapCount--; continue; }
                unsigned int rowBytes = w * (bpp / 8);
                R_Call(glTexImage2D, GL_TEXTURE_2D, i, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
                pixels += rowBytes * h;
                w /= 2; h /= 2;
            }
            R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1);
            texture->width = width;
            texture->height = height;
            if (!dds) dds = texture;
            return texture;
        }
        fprintf(stderr, "R_LoadTextureDDS: no FourCC, pfSize=%u pfFlags=0x%08X\n", pfSize, pfFlags);
        return NULL;
    } else {
        /* Unknown FourCC — try to load as DXT5 fallback for common cases */
        static int once = 0;
        if (once < 3) {
            fprintf(stderr, "R_LoadTextureDDS: unknown FourCC 0x%08X ('%c%c%c%c')\n",
                    fourCC, buf[84], buf[85], buf[86], buf[87]);
            once++;
        }
        return NULL;
    }

    LPTEXTURE texture = ri.MemAlloc(sizeof(TEXTURE));

    R_Call(glGenTextures, 1, &texture->texid);
    R_Call(glBindTexture, GL_TEXTURE_2D, texture->texid);

    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    unsigned int offset = 0;
    unsigned int w = width;
    unsigned int h = height;

    for (unsigned int i = 0; i < mipMapCount; i++) {
        if (w == 0 || h == 0) { mipMapCount--; continue; }
        unsigned int size = ((w+3)/4) * ((h+3)/4) * blockSize;
        R_Call(glCompressedTexImage2D, GL_TEXTURE_2D, i, format, w, h, 0, size, buf + offset + (headerSize + 4));
        offset += size;
        w /= 2;
        h /= 2;
    }
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1);

    if (!dds) dds = texture;
    texture->width = width;
    texture->height = height;

    return texture;
}

#include "r_wowmap.h"

LPTEXTURE Wow_CreateAlphaTexture(BYTE const alpha[4][WOW_ALPHA_TEXELS]) {
    BYTE pixels[WOW_ALPHA_TEXELS * 4];
    LPTEXTURE texture = R_AllocateTexture(64, 64);

    FOR_LOOP(i, WOW_ALPHA_TEXELS) {
        pixels[i * 4 + 0] = alpha[0][i];
        pixels[i * 4 + 1] = alpha[1][i];
        pixels[i * 4 + 2] = alpha[2][i];
        pixels[i * 4 + 3] = alpha[3][i];
    }

    R_Call(glBindTexture, GL_TEXTURE_2D, texture->texid);
    R_Call(glTexImage2D, GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return texture;
}

void Wow_EnsureAlphaAtlasTexture(void) {
    if (wow_world.alpha_atlas_texture) {
        return;
    }

    wow_world.alpha_atlas_texture = R_AllocateTexture(WOW_ALPHA_ATLAS_SIZE, WOW_ALPHA_ATLAS_SIZE);
    R_Call(glBindTexture, GL_TEXTURE_2D, wow_world.alpha_atlas_texture->texid);
    R_Call(glTexImage2D, GL_TEXTURE_2D, 0, GL_RGBA, WOW_ALPHA_ATLAS_SIZE, WOW_ALPHA_ATLAS_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void Wow_UploadAlphaAtlasChunk(DWORD index_x, DWORD index_y, BYTE const alpha[4][WOW_ALPHA_TEXELS]) {
    BYTE pixels[WOW_ALPHA_TEXELS * 4];

    if (index_x >= WOW_ALPHA_ATLAS_CHUNKS || index_y >= WOW_ALPHA_ATLAS_CHUNKS) {
        return;
    }

    Wow_EnsureAlphaAtlasTexture();
    FOR_LOOP(i, WOW_ALPHA_TEXELS) {
        pixels[i * 4 + 0] = alpha[0][i];
        pixels[i * 4 + 1] = alpha[1][i];
        pixels[i * 4 + 2] = alpha[2][i];
        pixels[i * 4 + 3] = alpha[3][i];
    }

    R_Call(glBindTexture, GL_TEXTURE_2D, wow_world.alpha_atlas_texture->texid);
    R_Call(glTexSubImage2D, GL_TEXTURE_2D, 0, index_x * WOW_ALPHA_CHUNK_SIZE, index_y * WOW_ALPHA_CHUNK_SIZE, WOW_ALPHA_CHUNK_SIZE, WOW_ALPHA_CHUNK_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}


DWORD Wow_PredictedLayer(WORD const pred_tex[8], DWORD layer_count, int x, int y) {
    DWORD layer;

    if (!pred_tex || layer_count <= 1) {
        return 0;
    }
    layer = (pred_tex[y] >> (x * 2)) & 3;
    if (layer >= layer_count) {
        layer = 0;
    }
    return layer;
}

DWORD Wow_AlphaSlotForTexture(DWORD unique_texture_ids[4], DWORD *unique_count, DWORD texture_id) {
    FOR_LOOP(i, *unique_count) {
        if (unique_texture_ids[i] == texture_id) {
            return i;
        }
    }
    if (*unique_count < 4) {
        unique_texture_ids[*unique_count] = texture_id;
        return (*unique_count)++;
    }
    return 0;
}

DWORD Wow_BuildUniqueTextureSlots(wowLayer_t const *layers,
                                         DWORD layer_count,
                                         DWORD slot_texture_ids[4]) {
    DWORD unique_texture_ids[4] = { 0, 0, 0, 0 };
    DWORD unique_count = 0;

    memset(slot_texture_ids, 0, sizeof(DWORD) * 4);
    if (!layers || layer_count == 0) {
        return 0;
    }

    FOR_LOOP(layer_index, MIN(layer_count, 4)) {
        DWORD texture_id = layers[layer_index].texture_id;
        DWORD slot = Wow_AlphaSlotForTexture(unique_texture_ids, &unique_count, texture_id);
        if (slot < 4) {
            slot_texture_ids[slot] = texture_id;
        }
    }

    return unique_count;
}

void Wow_DecodeAlphaLayer(BYTE const *src,
                                 BYTE const *src_end,
                                 DWORD flags,
                                 DWORD mcnk_flags,
                                 BOOL big_alpha,
                                 BYTE out[WOW_ALPHA_TEXELS]) {
    DWORD read_count = 0;

    if (!src || src >= src_end) {
        return;
    }

    if (flags & 0x200) {
        while (read_count < WOW_ALPHA_TEXELS && src < src_end) {
            BYTE code = *src++;
            DWORD count = code & 0x7f;
            if (code & 0x80) {
                BYTE value;
                if (src >= src_end) {
                    break;
                }
                value = *src++;
                while (count-- && read_count < WOW_ALPHA_TEXELS) {
                    out[read_count++] = value;
                }
            } else {
                while (count-- && read_count < WOW_ALPHA_TEXELS && src < src_end) {
                    out[read_count++] = *src++;
                }
            }
        }
    } else if (big_alpha) {
        while (read_count < WOW_ALPHA_TEXELS && src < src_end) {
            out[read_count++] = *src++;
        }
    } else {
        BOOL do_not_fix_alpha_map = (mcnk_flags & 0x8000) != 0;
        size_t nibble_count = (size_t)(src_end - src) * 2;

        if (do_not_fix_alpha_map) {
            while (read_count < WOW_ALPHA_TEXELS && src < src_end) {
                BYTE value = *src++;
                out[read_count++] = (BYTE)((value & 0x0f) * 17);
                if (read_count < WOW_ALPHA_TEXELS) {
                    out[read_count++] = (BYTE)(((value >> 4) & 0x0f) * 17);
                }
            }
        } else {
            FOR_LOOP(y, 63) {
                FOR_LOOP(x, 63) {
                    size_t nibble_index = (size_t)y * 64 + x;
                    BYTE packed;
                    BYTE nibble;

                    if (nibble_index >= nibble_count) {
                        continue;
                    }
                    packed = src[nibble_index >> 1];
                    nibble = (nibble_index & 1) ? ((packed >> 4) & 0x0f) : (packed & 0x0f);
                    out[y * 64 + x] = (BYTE)(nibble * 17);
                }
                out[y * 64 + 63] = out[y * 64 + 62];
            }
            FOR_LOOP(x, 64) {
                out[63 * 64 + x] = out[62 * 64 + x];
            }
        }
    }
}

void Wow_DecodeAlphaMaps(BYTE const *mcal,
                                DWORD mcal_size,
                                wowLayer_t const *layers,
                                DWORD layer_count,
                                DWORD mcnk_flags,
                                BYTE alpha[4][WOW_ALPHA_TEXELS]) {
    BOOL big_alpha = (wow_world.wdt_flags & (0x4 | 0x80)) != 0;
    DWORD unique_texture_ids[4] = { 0, 0, 0, 0 };
    DWORD unique_count = 0;
    DWORD uncompressed_index = 0;
    BOOL has_uncompressed = false;
    memset(alpha, 0, sizeof(BYTE) * 4 * WOW_ALPHA_TEXELS);

    if (!mcal || !layers || layer_count <= 1) {
        return;
    }

    FOR_LOOP(layer_index, MIN(layer_count, 4)) {
        DWORD slot = Wow_AlphaSlotForTexture(unique_texture_ids, &unique_count, layers[layer_index].texture_id);
        BYTE const *src;
        DWORD offset;

        if (!(layers[layer_index].flags & 0x100)) {
            uncompressed_index = slot;
            has_uncompressed = true;
            continue;
        }

        offset = layers[layer_index].offset_in_mcal;
        if (offset >= mcal_size) {
            continue;
        }

        src = mcal + offset;
        Wow_DecodeAlphaLayer(src, mcal + mcal_size, layers[layer_index].flags, mcnk_flags, big_alpha, alpha[slot]);
    }

    if (has_uncompressed) {
        FOR_LOOP(i, WOW_ALPHA_TEXELS) {
            BYTE value = 255;
            FOR_LOOP(layer_index, 4) {
                value = (BYTE)(value - alpha[layer_index][i]);
            }
            alpha[uncompressed_index][i] = value;
        }
    }
}

/* Allocate the R32F height atlas covering the full ADT streaming window. */
void Wow_EnsureHeightAtlas(void) {
    if (wow_world.height_atlas) return;
    wow_world.height_atlas = R_AllocateTexture(WOW_HEIGHT_ATLAS_W, WOW_HEIGHT_ATLAS_H);
    R_Call(glBindTexture, GL_TEXTURE_2D, wow_world.height_atlas->texid);
    /* GL_R32F: exact float precision, no filtering bias near cell centers */
    R_Call(glTexImage2D, GL_TEXTURE_2D, 0, GL_R32F, WOW_HEIGHT_ATLAS_W, WOW_HEIGHT_ATLAS_H,
           0, GL_RED, GL_FLOAT, NULL);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    /* texelFetch is used in shaders; nearest is safest default */
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

/* Upload one 17x9 height tile for MCNK at atlas position (ix, iy).
   Stores absolute world Z (base_z + relative MCVT height) so the vertex shader
   can use the sampled value directly without a per-chunk base-Z uniform. */
void Wow_UploadHeightAtlasChunk(DWORD ix, DWORD iy, float base_z, float const heights[WOW_MCVT_COUNT]) {
    float tile[WOW_HEIGHT_ATLAS_TILE_H][WOW_HEIGHT_ATLAS_TILE_W];
    int r, c;

    if (ix >= WOW_HEIGHT_ATLAS_CHUNKS || iy >= WOW_HEIGHT_ATLAS_CHUNKS) return;

    /* Rows 0..7: 9 outer (cols 0..8) + 8 center (cols 9..16) */
    for (r = 0; r < 8; r++)
        for (c = 0; c < WOW_HEIGHT_ATLAS_TILE_W; c++)
            tile[r][c] = base_z + heights[r * 17 + c];
    /* Row 8: 9 outer points only; col 9..16 are unused — zero them */
    for (c = 0; c < 9; c++) tile[8][c] = base_z + heights[8 * 17 + c];
    for (c = 9; c < WOW_HEIGHT_ATLAS_TILE_W; c++) tile[8][c] = base_z;

    Wow_EnsureHeightAtlas();
    R_Call(glBindTexture, GL_TEXTURE_2D, wow_world.height_atlas->texid);
    R_Call(glTexSubImage2D, GL_TEXTURE_2D, 0,
           (GLint)(ix * WOW_HEIGHT_ATLAS_TILE_W), (GLint)(iy * WOW_HEIGHT_ATLAS_TILE_H),
           WOW_HEIGHT_ATLAS_TILE_W, WOW_HEIGHT_ATLAS_TILE_H,
           GL_RED, GL_FLOAT, tile);
}

/* Allocate the RGBA8 grass-control texture (one texel per 8x8 MCNK cell). */
void Wow_EnsureGrassCtrlTexture(void) {
    if (wow_world.grass_ctrl) return;
    wow_world.grass_ctrl = R_AllocateTexture(WOW_GRASS_CTRL_SIZE, WOW_GRASS_CTRL_SIZE);
    R_Call(glBindTexture, GL_TEXTURE_2D, wow_world.grass_ctrl->texid);
    R_Call(glTexImage2D, GL_TEXTURE_2D, 0, GL_RGBA, WOW_GRASS_CTRL_SIZE, WOW_GRASS_CTRL_SIZE,
           0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

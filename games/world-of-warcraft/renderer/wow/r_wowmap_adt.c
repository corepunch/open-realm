#include "r_wowmap.h"
#include "common/stb_dbc.h"

void Wow_LoadAdt(BYTE const *data, DWORD size, DWORD tile_x, DWORD tile_y) {
    DWORD offset = 0;
    char **textures = NULL;
    DWORD num_textures = 0;
    VERTEX *object_vertices = NULL;
    DWORD object_vertex_count = 0;
#if !WOW_DEBUG_DOODAD_ERROR_MESHES
    BYTE const *doodad_names = NULL;
    DWORD doodad_names_size = 0;
    DWORD const *doodad_offsets = NULL;
    DWORD doodad_offset_count = 0;
#endif
    BYTE const *wmo_names = NULL;
    DWORD wmo_names_size = 0;
    DWORD const *wmo_offsets = NULL;
    DWORD wmo_offset_count = 0;

    while (offset + 8 <= size) {
        BYTE const *tag = data + offset;
        DWORD chunk_size = Wow_Read32(data + offset + 4);
        BYTE const *chunk = data + offset + 8;

        offset += 8;
        if (offset + chunk_size > size) {
            break;
        }

        if (*(DWORD const *)tag == ID_XETM) {
            Wow_FreeStringList(textures, num_textures);
            textures = Wow_ParseStringBlock(chunk, chunk_size, &num_textures);
        } else if (*(DWORD const *)tag == ID_XDMM) {
#if !WOW_DEBUG_DOODAD_ERROR_MESHES
            doodad_names = chunk;
            doodad_names_size = chunk_size;
#endif
        } else if (*(DWORD const *)tag == ID_DIMM) {
#if !WOW_DEBUG_DOODAD_ERROR_MESHES
            doodad_offsets = (DWORD const *)chunk;
            doodad_offset_count = chunk_size / sizeof(DWORD);
#endif
        } else if (*(DWORD const *)tag == ID_OMWM) {
            wmo_names = chunk;
            wmo_names_size = chunk_size;
        } else if (*(DWORD const *)tag == ID_DIWM) {
            wmo_offsets = (DWORD const *)chunk;
            wmo_offset_count = chunk_size / sizeof(DWORD);
        } else if (*(DWORD const *)tag == ID_FDDM) {
            DWORD count = chunk_size / sizeof(wowDoodadDef_t);
            wow_world.num_doodads += count;
#if WOW_DEBUG_DOODAD_ERROR_MESHES
            object_vertices = Wow_AppendDoodadErrorMarkers(object_vertices, &object_vertex_count, chunk, chunk_size);
#else
            FOR_LOOP(i, count) {
                wowDoodadDef_t const *def = (wowDoodadDef_t const *)(chunk + i * sizeof(*def));
                LPCSTR model_path = Wow_StringRefFromOffsets(doodad_names, doodad_names_size, doodad_offsets, doodad_offset_count, def->name_id);
                Wow_AddDoodadInstance(model_path, def);
            }
#endif
        } else if (*(DWORD const *)tag == ID_FDOM) {
#if WOW_DEBUG_OBJECT_MARKERS
            object_vertices = Wow_AppendMarkers(object_vertices, &object_vertex_count, chunk, chunk_size, wmo_names, wmo_names_size, wmo_offsets, wmo_offset_count, true);
#else
            DWORD count = chunk_size / sizeof(wowMapObjDef_t);
            FOR_LOOP(i, count) {
                wowMapObjDef_t const *def = (wowMapObjDef_t const *)(chunk + i * sizeof(*def));
                LPCSTR wmo_path = Wow_StringRefFromOffsets(wmo_names, wmo_names_size, wmo_offsets, wmo_offset_count, def->name_id);
                Wow_AddWmoInstance(wmo_path, def);
            }
#endif
        } else if (*(DWORD const *)tag == ID_KNCM && chunk_size >= 0x80) {
            DWORD sub = 0x80;
            wowVec3_t pos;
            DWORD index_x;
            DWORD index_y;
            DWORD chunk_flags;
            WORD holes;
            WORD pred_tex[8];
            uint64_t no_effect_mask;
            BYTE alpha[4][WOW_ALPHA_TEXELS];
            wowLayer_t layers[4];
            DWORD layer_count = 0;
            BYTE const *mcal = NULL;
            DWORD mcal_size = 0;
            COLOR32 const *mccv = NULL;
            BYTE const *mcsh = NULL;
            float heights[WOW_MCVT_COUNT];
            BYTE normals[WOW_MCVT_COUNT * 3];
            BOOL has_heights = false;
            BOOL has_normals = false;
            memset(heights, 0, sizeof(heights));
            memset(normals, 0, sizeof(normals));
            memset(pred_tex, 0, sizeof(pred_tex));
            memset(layers, 0, sizeof(layers));
            memset(alpha, 0, sizeof(alpha));
            no_effect_mask = 0;
            chunk_flags = Wow_Read32(chunk + 0x00);
            index_x = Wow_Read32(chunk + 0x04);
            index_y = Wow_Read32(chunk + 0x08);
            memcpy(&pos, chunk + 0x68, sizeof(pos));
            memcpy(&holes, chunk + 0x3c, sizeof(holes));
            memcpy(pred_tex, chunk + 0x40, sizeof(pred_tex));
            /* No-effect-doodad mask: 64-bit field at MCNK header offset 0x50.
               One bit per 8x8 cell (row-major); 1 = suppress grass. */
            if (chunk_size >= 0x58)
                memcpy(&no_effect_mask, chunk + 0x50, sizeof(no_effect_mask));

            while (sub + 8 <= chunk_size) {
                BYTE const *subtag = chunk + sub;
                DWORD sub_size = Wow_Read32(chunk + sub + 4);
                BYTE const *subchunk = chunk + sub + 8;
                BOOL is_mcnr = *(DWORD const *)subtag == ID_RNCM;
                sub += 8;
                if (sub + sub_size > chunk_size) {
                    break;
                }
                if (*(DWORD const *)subtag == ID_TVCM && sub_size >= sizeof(heights)) {
                    memcpy(heights, subchunk, sizeof(heights));
                    has_heights = true;
                } else if (is_mcnr && sub_size >= sizeof(normals)) {
                    memcpy(normals, subchunk, sizeof(normals));
                    has_normals = true;
                } else if (*(DWORD const *)subtag == ID_YLCM && sub_size >= sizeof(wowLayer_t)) {
                    layer_count = MIN(sub_size / sizeof(wowLayer_t), 4);
                    memcpy(layers, subchunk, sizeof(*layers) * layer_count);
                } else if (*(DWORD const *)subtag == ID_LACM) {
                    mcal = subchunk;
                    mcal_size = sub_size;
                } else if (*(DWORD const *)subtag == ID_VCCM && sub_size >= WOW_MCVT_COUNT * sizeof(COLOR32)) {
                    mccv = (COLOR32 const *)subchunk;
                } else if (*(DWORD const *)subtag == ID_HSCM && sub_size >= 512) {
                    mcsh = subchunk;
                } else if (*(DWORD const *)subtag == ID_FRCM) {
                    /* MCRF: uint32[] indices into the tile's MDDF/MODF arrays for doodads/WMOs
                       that overlap this cell. Not needed while we instantiate from root-level
                       MDDF/MODF directly; required later for per-chunk visibility culling. */
                } else if (*(DWORD const *)subtag == ID_ESCM) {
                    /* MCSE: ambient sound emitter placement records. Audio system only. */
                } else if (*(DWORD const *)subtag == ID_QLCM) {
                    /* MCLQ: legacy inline liquid chunk (Vanilla / TBC). Contains water/lava/slime
                       geometry as a 9×9 vertex grid with type flags. Superseded by MH2O in WotLK;
                       needs its own liquid rendering pass to display Vanilla/TBC water. */
                } else if (!is_mcnr) {
                    fprintf(stderr, "WoW: unknown MCNK subchunk '%.4s' (%u bytes)\n", (char const *)subtag, sub_size);
                }
                sub += sub_size;
                if (is_mcnr && sub_size == sizeof(normals) && sub + 13 <= chunk_size) {
                    sub += 13;
                }
            }

            if (has_heights) {
                DWORD atlas_index_x = (tile_x - wow_world.alpha_origin_x) * 16 + index_x;
                DWORD atlas_index_y = (tile_y - wow_world.alpha_origin_y) * 16 + index_y;
                (void)pred_tex;  /* TODO Phase 1: decode 2-bit per-cell layer map */
                Wow_DecodeAlphaMaps(mcal, mcal_size, layers, layer_count, chunk_flags, alpha);
                Wow_UploadAlphaAtlasChunk(atlas_index_x, atlas_index_y, alpha);
                Wow_AddAdtChunk(pos, atlas_index_x, atlas_index_y, holes, no_effect_mask, alpha, layers, layer_count, textures, num_textures, heights, has_normals ? normals : NULL, mccv, mcsh);
            }
        } else if (*(DWORD const *)tag == ID_REVM) {
            /* MVER: file format version (always 18 for pre-Cata ADTs). Tag-driven
               scanning makes this redundant; no action needed. */
        } else if (*(DWORD const *)tag == ID_RDMM) {
            /* MHDR: byte offsets to every other named chunk in the file. Sequential
               tag scanning makes this redundant; no action needed. */
        } else if (*(DWORD const *)tag == ID_NICM) {
            /* MCIN: 256-entry {offset, size} index into the 16×16 MCNK grid. Sequential
               scanning finds MCNK tags directly; no action needed. */
        } else if (*(DWORD const *)tag == ID_O2HM) {
            /* MH2O: WotLK liquid layer data (water, lava, slime). Replaces per-chunk MCLQ.
               Complex format with per-cell depth, UV arrays and render flags; needs its
               own liquid rendering pass to display WotLK+ water. */
        } else if (*(DWORD const *)tag == ID_OBMF) {
            /* MFBO: two 3×3 height planes defining the min/max flyable altitude per tile
               quadrant. Flight-path clipping only; not relevant to terrain rendering. */
        } else if (*(DWORD const *)tag == ID_FXTM) {
            /* MTXF: uint32 flags array, one entry per MTEX texture. Flag 0x1 = environment
               cube-map reflection; flag 0x2 = no mip-map. Needed for correct specular /
               cubemap handling; wire up when implementing reflective surfaces. */
        } else {
            /* Log each unknown tag at most once across all tile loads. */
            static DWORD seen[16]; static DWORD seen_n = 0;
            DWORD t = *(DWORD const *)tag; BOOL logged = false;
            for (DWORD i = 0; i < seen_n; i++) if (seen[i] == t) { logged = true; break; }
            if (!logged) {
                fprintf(stderr, "WoW: unknown ADT chunk '%.4s' (%u bytes)\n", (char const *)tag, chunk_size);
                if (seen_n < 16) seen[seen_n++] = t;
            }
        }

        offset += chunk_size;
    }

    if (object_vertex_count) {
        wowAdtChunk_t *marker_chunk = ri.MemAlloc(sizeof(*marker_chunk));
        memset(marker_chunk, 0, sizeof(*marker_chunk));
        marker_chunk->buffer = R_MakeVertexArrayObject(object_vertices, object_vertex_count);
        marker_chunk->textures[0] = tr.texture[TEX_WHITE];
        marker_chunk->textures[1] = tr.texture[TEX_WHITE];
        marker_chunk->textures[2] = tr.texture[TEX_WHITE];
        marker_chunk->textures[3] = tr.texture[TEX_WHITE];
        marker_chunk->num_vertices = object_vertex_count;
        marker_chunk->next = wow_world.chunks;
        wow_world.chunks = marker_chunk;
        wow_world.num_object_vertices += object_vertex_count;
        SAFE_DELETE(object_vertices, ri.MemFree);
    }
    SAFE_DELETE(object_vertices, ri.MemFree);

    Wow_FreeStringList(textures, num_textures);
    wow_world.num_adts++;
}

void Wow_LoadAdtFile(DWORD tile_x, DWORD tile_y) {
    PATHSTR path;
    BYTE *data;
    DWORD size = 0;

    snprintf(path, sizeof(path), "%s/%s_%u_%u.adt", wow_world.map_dir, wow_world.map_name, (unsigned)tile_x, (unsigned)tile_y);

    /* Prefer mmap for loose ADT files — avoids a full fread copy of 200-800 KB
     * per tile and lets the OS evict pages under memory pressure. Falls back to
     * a heap read automatically when the file comes from an MPQ archive. */
    data = ri.FS_MmapFile ? ri.FS_MmapFile(path, &size) : NULL;
    if (!data || !size) {
        if (data) { if (ri.FS_MunmapFile) ri.FS_MunmapFile(data); }
        int isize = ri.FS_ReadFile(path, (void **)&data);
        if (isize <= 0 || !data) return;
        Wow_LoadAdt(data, (DWORD)isize, tile_x, tile_y);
        ri.FS_FreeFile(data);
        return;
    }
    Wow_LoadAdt(data, size, tile_x, tile_y);
    if (ri.FS_MunmapFile) ri.FS_MunmapFile(data);
}

BYTE const *Wow_FindMainChunk(BYTE const *data, DWORD size, LPDWORD main_size) {
    DWORD offset = 0;

    while (offset + 8 <= size) {
        BYTE const *tag = data + offset;
        DWORD chunk_size = Wow_Read32(data + offset + 4);
        offset += 8;
        if (offset + chunk_size > size) {
            break;
        }
        if (*(DWORD const *)tag == ID_NIAM) {
            *main_size = chunk_size;
            return data + offset;
        }
        offset += chunk_size;
    }
    return NULL;
}

void Wow_LoadWdtFlags(BYTE const *data, DWORD size) {
    DWORD offset = 0;

    wow_world.wdt_flags = 0;
    while (offset + 8 <= size) {
        BYTE const *tag = data + offset;
        DWORD chunk_size = Wow_Read32(data + offset + 4);
        BYTE const *chunk = data + offset + 8;
        offset += 8;
        if (offset + chunk_size > size) {
            break;
        }
        if (*(DWORD const *)tag == ID_DHPM && chunk_size >= sizeof(DWORD)) {
            wow_world.wdt_flags = Wow_Read32(chunk);
            return;
        }
        offset += chunk_size;
    }
}

BOOL Wow_LoadWdtTiles(BYTE const *data, DWORD size) {
    DWORD main_size = 0;
    BYTE const *main_chunk = Wow_FindMainChunk(data, size, &main_size);
    DWORD max_entries;
    /* WDT-level WMO placement (global-WMO maps: dungeons, instances) */
    LPCSTR wmo_name_blob = NULL;
    DWORD  wmo_name_blob_size = 0;
    DWORD const *wmo_offsets = NULL;
    DWORD  wmo_offset_count = 0;
    BYTE const *modf_chunk = NULL;
    DWORD  modf_size = 0;
    DWORD  offset2 = 0;

    if (!main_chunk || main_size < sizeof(wowWdtMainEntry_t)) {
        return false;
    }

    Wow_LoadWdtFlags(data, size);
    max_entries = main_size / sizeof(wowWdtMainEntry_t);
    FOR_LOOP(tile_y, WOW_WDT_TILES) {
        FOR_LOOP(tile_x, WOW_WDT_TILES) {
            DWORD i = tile_y * WOW_WDT_TILES + tile_x;
            wowWdtMainEntry_t const *entry;
            if (i >= max_entries) {
                continue;
            }
            entry = (wowWdtMainEntry_t const *)(main_chunk + i * sizeof(*entry));
            wow_world.tiles[tile_x][tile_y].present = (entry->flags & 1) != 0;
        }
    }

    /* Phase 5: for global-WMO maps (MPHD bit 0x01), parse WDT-level MWMO/MWID/MODF */
    if (!(wow_world.wdt_flags & 0x01)) return true;

    while (offset2 + 8 <= size) {
        BYTE const *tag = data + offset2;
        DWORD chunk_size = Wow_Read32(data + offset2 + 4);
        BYTE const *chunk = data + offset2 + 8;
        offset2 += 8;
        if (offset2 + chunk_size > size) break;
        if (*(DWORD const *)tag == ID_OMWM && chunk_size > 0) {
            wmo_name_blob = (LPCSTR)chunk;
            wmo_name_blob_size = chunk_size;
        } else if (*(DWORD const *)tag == ID_DIWM && chunk_size >= sizeof(DWORD)) {
            wmo_offsets = (DWORD const *)chunk;
            wmo_offset_count = chunk_size / sizeof(DWORD);
        } else if (*(DWORD const *)tag == ID_FDOM && chunk_size >= sizeof(wowMapObjDef_t)) {
            modf_chunk = chunk;
            modf_size  = chunk_size;
        }
        offset2 += chunk_size;
    }

    if (modf_chunk && modf_size >= sizeof(wowMapObjDef_t)) {
        DWORD count = modf_size / sizeof(wowMapObjDef_t);
        FOR_LOOP(i, count) {
            wowMapObjDef_t const *def = (wowMapObjDef_t const *)(modf_chunk + i * sizeof(*def));
            LPCSTR wmo_path = Wow_StringRefFromOffsets((BYTE const *)wmo_name_blob,
                                                        wmo_name_blob_size,
                                                        wmo_offsets, wmo_offset_count,
                                                        def->name_id);
            if (wmo_path && *wmo_path) {
                Wow_AddWmoInstance(wmo_path, def);
                fprintf(stderr, "WDT global WMO: %s\n", wmo_path);
            }
        }
    }
    return true;
}

int Wow_AdtIndexForWorldCoord(float coord) {
    return (int)floorf(32.0f - coord / WOW_ADT_SIZE);
}

void Wow_LoadMapDbcFlags(void) {
    stbDbc_t h;
    LPBYTE data = NULL;
    int size = ri.FS_ReadFile("DBFilesClient\\Map.dbc", (void **)&data);
    BYTE const *records_base;
    BYTE const *strings_base;

    wow_world.use_weighted_blend = false;
    if (!Stb_DbcValid(data, (DWORD)size, &h)) {
        SAFE_DELETE(data, ri.FS_FreeFile);
        return;
    }

    records_base = Stb_DbcRecords(data);
    strings_base = Stb_DbcStrings(data, &h);

    FOR_LOOP(record_index, h.records) {
        BYTE const *record = records_base + record_index * h.record_size;
        BOOL directory_matches = false;
        DWORD flags0 = Wow_Read32(record + (h.fields - 1) * sizeof(DWORD));

        FOR_LOOP(field_index, h.fields) {
            DWORD string_offset = Wow_Read32(record + field_index * sizeof(DWORD));
            LPCSTR value = Stb_DbcString(strings_base, h.string_size, string_offset);
            if (value && *value && !strcasecmp(value, wow_world.map_name)) {
                directory_matches = true;
                break;
            }
        }

        if (directory_matches) {
            wow_world.use_weighted_blend = (flags0 & 0x4) != 0;
            ri.FS_FreeFile(data);
            return;
        }
    }
    ri.FS_FreeFile(data);
}

void Wow_LoadNearbyAdts(int center_x, int center_y) {
    if (wow_world.has_adt_window) {
        if (center_x >= wow_world.adt_center_x - WOW_ADT_RADIUS &&
            center_x <= wow_world.adt_center_x + WOW_ADT_RADIUS &&
            center_y >= wow_world.adt_center_y - WOW_ADT_RADIUS &&
            center_y <= wow_world.adt_center_y + WOW_ADT_RADIUS) {
            return;
        }
        Wow_ClearLoadedAdts();
    }

    wow_world.alpha_origin_x = center_x - WOW_ADT_RADIUS;
    wow_world.alpha_origin_y = center_y - WOW_ADT_RADIUS;
    wow_world.adt_center_x = center_x;
    wow_world.adt_center_y = center_y;
    wow_world.has_adt_window = true;

    for (int y = center_y - WOW_ADT_RADIUS; y <= center_y + WOW_ADT_RADIUS; y++) {
        for (int x = center_x - WOW_ADT_RADIUS; x <= center_x + WOW_ADT_RADIUS; x++) {
            if (x < 0 || x >= WOW_WDT_TILES || y < 0 || y >= WOW_WDT_TILES) {
                continue;
            }
            if (wow_world.tiles[x][y].present) {
                Wow_LoadAdtFile((DWORD)x, (DWORD)y);
            }
        }
    }
    fprintf(stderr, "R_DrawWorld: WoW ADT window centered on camera tile %d,%d radius=%d chunks=%u doodads=%u rendered_doodads=%u wmos=%u wmo_batches=%u\n", center_x, center_y, WOW_ADT_RADIUS, (unsigned)wow_world.num_chunks, (unsigned)wow_world.num_doodads, (unsigned)wow_world.num_doodad_instances, (unsigned)wow_world.num_wmos, (unsigned)wow_world.num_wmo_batches);
}

void Wow_LoadCameraAdts(void) {
    int center_x = Wow_AdtIndexForWorldCoord(tr.viewDef.camerastate[0].origin.y);
    int center_y = Wow_AdtIndexForWorldCoord(tr.viewDef.camerastate[0].origin.x);

    if (center_x < 0 || center_x >= WOW_WDT_TILES || center_y < 0 || center_y >= WOW_WDT_TILES) {
        static BOOL logged_outside = false;
        if (!logged_outside) {
            fprintf(stderr, "R_DrawWorld: camera native position %.3f %.3f is outside WoW ADT range -> tile %d,%d\n", tr.viewDef.camerastate[0].origin.x, tr.viewDef.camerastate[0].origin.y, center_x, center_y);
            logged_outside = true;
        }
        return;
    }

    Wow_LoadNearbyAdts(center_x, center_y);
}

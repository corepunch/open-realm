#include "r_war3map.h"

LPMAPSEGMENT g_mapSegments = NULL;
LPMAPLAYER g_groundLayers = NULL;

#ifdef DEBUG_PATHFINDING
LPCTEXTURE pathTexture = NULL;
void R_SetPathTexture(LPCCOLOR32 debugTexture) {
    if (pathTexture) {
        R_LoadTextureMipLevel(pathTexture, 0, debugTexture, pathTexture->width, pathTexture->height);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
}
#endif

static void R_FileReadShadowMap(HANDLE hMpq, LPWAR3MAP  pWorld) {
    HANDLE file;
    if (!SFileOpenFileEx(hMpq, "war3map.shd", SFILE_OPEN_FROM_MPQ, &file)) {
        return;
    }
    int const w = (pWorld->width - 1) * 4;
    int const h = (pWorld->height - 1) * 4;
    LPSTR shadows = ri.MemAlloc(w * h);
    if (!SFileReadFile(file, shadows, w * h, NULL, NULL)) {
        ri.MemFree(shadows);
        SFileCloseFile(file);
        return;
    }
    LPTEXTURE pShadowmap = R_AllocateTexture(w, h);
    LPCOLOR32 pixels = ri.MemAlloc(w * h * sizeof(struct color32));
    FOR_LOOP(i, w * h) {
        BYTE shadow = (BYTE)shadows[i];
        pixels[i].r = 0;
        pixels[i].g = 0;
        pixels[i].b = 0;
        pixels[i].a = shadow / 2;
    }
    R_LoadTextureMipLevel(pShadowmap, 0, pixels, w, h);
    SFileCloseFile(file);
    ri.MemFree(shadows);
    ri.MemFree(pixels);

    tr.texture[TEX_TERRAIN_SHADOW] = pShadowmap;
}

static LPMAPSEGMENT R_BuildMapSegment(LPCWAR3MAP map, DWORD sx, DWORD sy) {
    LPMAPSEGMENT mapSegment = ri.MemAlloc(sizeof(MAPSEGMENT));
    LPMAPLAYER mapLayer = R_BuildMapSegmentWater(map, sx, sy);
    ADD_TO_LIST(mapLayer, mapSegment->layers);
    FOR_LOOP(cliff, map->num_cliffs) {
        if ((mapLayer = R_BuildMapSegmentCliffs(map, sx, sy, cliff))) {
            ADD_TO_LIST(mapLayer, mapSegment->layers);
        }
    }
    mapSegment->bbox.min = MAKE(VECTOR3, FLT_MAX, FLT_MAX, FLT_MAX);
    mapSegment->bbox.max = MAKE(VECTOR3, -FLT_MAX, -FLT_MAX, -FLT_MAX);
    return mapSegment;
}

static void R_BuildGroundLayers(LPCWAR3MAP map) {
    for (DWORD layer = map->num_grounds; layer > 0; layer--) {
        LPMAPLAYER mapLayer = R_BuildGroundLayerGlobal(map, layer - 1);
        if (mapLayer) {
            ADD_TO_LIST(mapLayer, g_groundLayers);
        }
    }
}

static VECTOR3 R_GetMapVertexPoint(LPCWAR3MAP map, DWORD x, DWORD y) {
    LPCWAR3MAPVERTEX mapVertex = GetWar3MapVertex(map, x, y);
    return (VECTOR3) {
        .x = map->center.x + x * TILE_SIZE,
        .y = map->center.y + y * TILE_SIZE,
        .z = GetWar3MapVertexHeight(mapVertex),
    };
}

static void R_LoadMapSegments(LPCWAR3MAP map) {
    FOR_LOOP(fx, (map->width - 1) / SEGMENT_SIZE) {
        FOR_LOOP(fy, (map->height - 1) / SEGMENT_SIZE) {
            LPMAPSEGMENT segment = R_BuildMapSegment(map, fx, fy);
            ADD_TO_LIST(segment, g_mapSegments);
            FOR_LOOP(sx, SEGMENT_SIZE+1) {
                FOR_LOOP(sy, SEGMENT_SIZE+1) {
                    FLOAT x = fx * SEGMENT_SIZE + sx;
                    FLOAT y = fy * SEGMENT_SIZE + sy;
                    VECTOR3 v = R_GetMapVertexPoint(map, x, y);
                    segment->bbox.min.x = MIN(segment->bbox.min.x, v.x);
                    segment->bbox.min.y = MIN(segment->bbox.min.y, v.y);
                    segment->bbox.min.z = MIN(segment->bbox.min.z, v.z);
                    segment->bbox.max.x = MAX(segment->bbox.max.x, v.x);
                    segment->bbox.max.y = MAX(segment->bbox.max.y, v.y);
                    segment->bbox.max.z = MAX(segment->bbox.max.z, v.z);
                }
            }
        }
    }
}

void R_AllocateFogOfWar(LPWAR3MAP map) {
    R_InitFogOfWar((map->width - 1) * 4, (map->height - 1) * 4);
}

static void R_LoadMapMinimap(HANDLE hMpq, LPCSTR mapFilename) {
    static LPCSTR const candidates[] = {
        "war3mapMap.blp",
        "war3mapMap.tga",
        NULL,
    };

    SAFE_DELETE(tr.minimap, R_ReleaseTexture);

    FOR_LOOP(i, sizeof(candidates) / sizeof(candidates[0])) {
        HANDLE file;
        PATHSTR path;

        if (!candidates[i]) {
            break;
        }
        if (!SFileOpenFileEx(hMpq, candidates[i], SFILE_OPEN_FROM_MPQ, &file)) {
            continue;
        }
        SFileCloseFile(file);
        snprintf(path, sizeof(path), "%s\\%s", mapFilename, candidates[i]);
        tr.minimap = R_LoadTexture(path);
        return;
    }
}

static BOOL R_ReadWar3MapVertex(HANDLE file, LPWAR3MAPVERTEX vert) {
    WORD water_and_edge;
    BYTE flags;
    BYTE variation;
    BYTE cliff_and_layer;

    if (!file || !vert) {
        return false;
    }
    memset(vert, 0, sizeof(*vert));
    if (!SFileReadFile(file, &vert->accurate_height, sizeof(vert->accurate_height), NULL, NULL)) {
        return false;
    }
    if (!SFileReadFile(file, &water_and_edge, sizeof(water_and_edge), NULL, NULL)) {
        return false;
    }
    if (!SFileReadFile(file, &flags, sizeof(flags), NULL, NULL)) {
        return false;
    }
    if (!SFileReadFile(file, &variation, sizeof(variation), NULL, NULL)) {
        return false;
    }
    if (!SFileReadFile(file, &cliff_and_layer, sizeof(cliff_and_layer), NULL, NULL)) {
        return false;
    }

    vert->waterlevel = water_and_edge & 0x3FFF;
    vert->mapedge = (water_and_edge & 0x4000) != 0;
    vert->ground = flags & 0x0F;
    vert->ramp = (flags & 0x10) != 0;
    vert->blight = (flags & 0x20) != 0;
    vert->water = (flags & 0x40) != 0;
    vert->boundary = (flags & 0x80) != 0;
    vert->cliffVariation = (variation >> 5) & 0x07;
    vert->groundVariation = variation & 0x1F;
    vert->cliff = (cliff_and_layer >> 4) & 0x0F;
    vert->level = cliff_and_layer & 0x0F;
    return true;
}

LPWAR3MAP FileReadWar3Map(HANDLE archive) {
    LPWAR3MAP map = ri.MemAlloc(sizeof(WAR3MAP));
    HANDLE file;
    SFileOpenFileEx(archive, "war3map.w3e", SFILE_OPEN_FROM_MPQ, &file);
    SFileReadFile(file, &map->header, 4, NULL, NULL);
    SFileReadFile(file, &map->version, 4, NULL, NULL);
    SFileReadFile(file, &map->tileset, 1, NULL, NULL);
    SFileReadFile(file, &map->custom, 4, NULL, NULL);
    SFileReadArray(file, map, grounds, 4, ri.MemAlloc);
    SFileReadArray(file, map, cliffs, 4, ri.MemAlloc);
    SFileReadFile(file, &map->width, 4, NULL, NULL);
    SFileReadFile(file, &map->height, 4, NULL, NULL);
    SFileReadFile(file, &map->center, 8, NULL, NULL);
    DWORD const num_vertices = map->width * map->height;
    int const vertexblocksize = sizeof(WAR3MAPVERTEX) * num_vertices;
    map->vertices = ri.MemAlloc(vertexblocksize);
    R_AllocateFogOfWar(map);
    FOR_LOOP(i, num_vertices) {
        if (!R_ReadWar3MapVertex(file, (LPWAR3MAPVERTEX)map->vertices + i)) {
            break;
        }
    }
    SFileCloseFile(file);
#ifdef DEBUG_PATHFINDING
    pathTexture = R_AllocateTexture((map->width - 1) * 4, (map->height - 1) * 4);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#endif
    FOR_LOOP(y, map->height) {
//        printf("%04x  ", y);
        FOR_LOOP(x, map->width) {
            LPWAR3MAPVERTEX vert = (LPWAR3MAPVERTEX)GetWar3MapVertex(map, x, y);
            if (!vert->ramp)
                continue;
            vert->cliffVariation = 0; // used also to mark mid-ramp
            LPCWAR3MAPVERTEX l = GetWar3MapVertex(map, x-1, y);
            LPCWAR3MAPVERTEX r = GetWar3MapVertex(map, x+1, y);
            LPCWAR3MAPVERTEX t = GetWar3MapVertex(map, x, y-1);
            LPCWAR3MAPVERTEX b = GetWar3MapVertex(map, x, y+1);
            if (l && r && l->ramp && r->ramp && l->level != r->level) {
                vert->cliffVariation = 1;
            } else if (t && b && t->ramp && b->ramp && t->level != b->level) {
                vert->cliffVariation = 1;
            }
//            printf("%x", GetWar3MapVertex(map, x, y)->level);
        }
//        printf("\n");
    }
    return map;
}

void R_RegisterMap(char const *mapFilename) {
    HANDLE hMpq;
    LPBYTE mapData;
    int mapSize;
    LPWAR3MAP map;

    /* Load .w3m file (which is itself an MPQ archive) */
    mapSize = ri.FS_ReadFile(mapFilename, (void **)&mapData);
    if (mapSize < 0 || !mapData) {
        ri.error("R_RegisterMap: failed to open map %s\n", mapFilename);
        return;
    }
    
    /* Open the .w3m as a nested MPQ archive to read internal files */
    if (!SFileOpenArchiveFromMemory(mapData, (DWORD)mapSize, 0, &hMpq)) {
        ri.FS_FreeFile(mapData);
        ri.error("R_RegisterMap: failed to open map archive %s\n", mapFilename);
        return;
    }
    map = FileReadWar3Map(hMpq);
    R_FileReadShadowMap(hMpq, map);
    R_LoadMapMinimap(hMpq, mapFilename);
    SFileCloseArchive(hMpq);
    ri.FS_FreeFile(mapData);
    tr.world = map;

    R_LoadMapSegments(map);
    R_BuildGroundLayers(map);
}

void R_DrawTerrainShadows(void) {
    if (!tr.world || !tr.texture[TEX_TERRAIN_SHADOW] || (tr.viewDef.rdflags & RDF_NOWORLDMODEL)) {
        return;
    }

    VECTOR2 size = GetWar3MapSize(tr.world);
    VECTOR2 mins = tr.world->center;
    VECTOR2 maxs = {
        .x = tr.world->center.x + size.x,
        .y = tr.world->center.y + size.y,
    };
    COLOR32 shadowColor = {0, 0, 0, 255};

    R_RenderRectSplat(&mins, &maxs, tr.texture[TEX_TERRAIN_SHADOW], tr.shader[SHADER_SHADOWSPLAT], shadowColor);
}

void R_DrawWorld(void) {
    if (tr.viewDef.rdflags & RDF_NOWORLDMODEL)
        return;

    R_Call(glUseProgram, tr.shader[SHADER_DEFAULT]->progid);
    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDepthFunc, GL_LEQUAL);
    
#ifdef DEBUG_PATHFINDING
    R_BindTexture(pathTexture, 1);
#endif

    FOR_EACH_LIST(MAPLAYER, layer, g_groundLayers) {
        if (layer == g_groundLayers) {
            R_Call(glDisable, GL_BLEND);
        } else {
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        R_BindTexture(layer->texture, 0);
        R_DrawBuffer(layer->buffer, layer->num_vertices);
    }

    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    FOR_EACH_LIST(MAPSEGMENT, segment, g_mapSegments) {
        R_DrawTerrainSegment(segment, (1 << MAPLAYERTYPE_CLIFF));
    }
}

void R_DrawAlphaSurfaces(void) {
    if (tr.viewDef.rdflags & RDF_NOWORLDMODEL)
        return;
    
    R_Call(glUseProgram, tr.shader[SHADER_DEFAULT]->progid);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDepthMask, GL_FALSE);

    FOR_EACH_LIST(MAPSEGMENT, segment, g_mapSegments) {
        R_DrawTerrainSegment(segment, (1 << MAPLAYERTYPE_WATER));
    }
    R_Call(glDepthMask, GL_TRUE);
}

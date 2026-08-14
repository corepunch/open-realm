#include "common/common.h"
#include <limits.h>
#include <math.h>

#define CM_WOW_ADT_SIZE       533.333313f
#define CM_WOW_ADT_UNIT_SIZE  (CM_WOW_ADT_SIZE / 16.0f / 8.0f)
#define CM_WOW_MCVT_COUNT     (9 * 9 + 8 * 8)

typedef struct {
    BOOL    has_heights;
    VECTOR3 position;
    float   heights[CM_WOW_MCVT_COUNT];
} cmWowChunkHeight_t;

typedef struct {
    BOOL              loaded;
    BOOL              valid;
    int               tile_x;
    int               tile_y;
    cmWowChunkHeight_t chunks[16][16];
} cmWowAdtHeightCache_t;

typedef struct {
    DWORD id;
    DWORD map_id;
    VECTOR3 position;
} cmWowWorldSafeLoc_t;

static VECTOR3              cm_wow_spawn_position = { 0.0f, 0.0f, 0.0f };
static FLOAT                cm_wow_spawn_heights[MAX_PLAYERS];
static char                 cm_wow_map_dir[PATH_MAX]  = { 0 };
static char                 cm_wow_map_name[128]      = { 0 };
static cmWowAdtHeightCache_t cm_wow_height_cache      = { 0 };

typedef struct {
    VECTOR3 pos;
    LPSTR   name;
} cmWowSpawnEntry_t;
static cmWowSpawnEntry_t *cm_wow_all_spawns = NULL;
static DWORD cm_wow_all_spawn_count = 0;

void CM_WowFreeAllSpawns(void) {
    if (cm_wow_all_spawns) {
        for (DWORD i = 0; i < cm_wow_all_spawn_count; i++)
            SAFE_DELETE(cm_wow_all_spawns[i].name, MemFree);
        SAFE_DELETE(cm_wow_all_spawns, MemFree);
        cm_wow_all_spawn_count = 0;
    }
}

DWORD CM_WowGetAllSpawnCount(void) { return cm_wow_all_spawn_count; }
LPCVECTOR3 CM_WowGetSpawnPos(DWORD index) { return index < cm_wow_all_spawn_count ? &cm_wow_all_spawns[index].pos : NULL; }
LPCSTR CM_WowGetSpawnName(DWORD index) { return index < cm_wow_all_spawn_count ? cm_wow_all_spawns[index].name : NULL; }

static DWORD CM_WowRead32(BYTE const *p) {
    return ((DWORD)p[0]) | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

static FLOAT CM_WowReadFloat(BYTE const *p) {
    FLOAT value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static BOOL CM_WowTagEquals(BYTE const *tag, LPCSTR reversed) {
    return memcmp(tag, reversed, 4) == 0;
}

static LPCSTR CM_WowDbcString(BYTE const *string_block, DWORD string_size, DWORD offset) {
    if (offset >= string_size)
        return NULL;
    return (LPCSTR)(string_block + offset);
}

static LPSTR CM_WowCopyString(LPCSTR value) {
    size_t len;
    LPSTR out;

    if (!value || !*value)
        return NULL;
    len = strlen(value);
    out = MemAlloc((long)len + 1);
    memcpy(out, value, len + 1);
    return out;
}

static BOOL CM_WowExtractMapName(LPCSTR mapFilename, LPSTR out, size_t out_size) {
    LPCSTR start, slash, backslash, dot;
    size_t len;

    if (!mapFilename || !out || out_size == 0)
        return false;

    slash     = strrchr(mapFilename, '/');
    backslash = strrchr(mapFilename, '\\');
    start = slash && backslash ? MAX(slash, backslash) + 1
          : slash              ? slash + 1
          : backslash          ? backslash + 1
          :                      mapFilename;
    dot = strrchr(start, '.');
    len = dot && dot > start ? (size_t)(dot - start) : strlen(start);
    if (len >= out_size)
        len = out_size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return len > 0;
}

static void CM_WowSetMapPath(LPCSTR mapFilename) {
    LPCSTR path  = mapFilename && *mapFilename ? mapFilename : "World/Maps/Azeroth/Azeroth.wdt";
    LPCSTR slash     = strrchr(path, '/');
    LPCSTR backslash = strrchr(path, '\\');
    LPCSTR base;
    size_t dir_len, name_len;

    memset(&cm_wow_height_cache, 0, sizeof(cm_wow_height_cache));
    cm_wow_map_dir[0]  = '\0';
    cm_wow_map_name[0] = '\0';

    base = slash && backslash ? MAX(slash, backslash)
         : slash              ? slash
         : backslash;
    base = base ? base + 1 : path;

    dir_len = (size_t)(base - path);
    if (dir_len > 0) {
        dir_len = MIN(dir_len, sizeof(cm_wow_map_dir) - 1);
        memcpy(cm_wow_map_dir, path, dir_len);
        cm_wow_map_dir[dir_len] = '\0';
        if (dir_len > 0 && (cm_wow_map_dir[dir_len-1] == '/' || cm_wow_map_dir[dir_len-1] == '\\'))
            cm_wow_map_dir[dir_len-1] = '\0';
    }

    name_len = strlen(base);
    if (name_len > 4 && !strcasecmp(base + name_len - 4, ".wdt"))
        name_len -= 4;
    name_len = MIN(name_len, sizeof(cm_wow_map_name) - 1);
    memcpy(cm_wow_map_name, base, name_len);
    cm_wow_map_name[name_len] = '\0';

    if (!cm_wow_map_dir[0] && cm_wow_map_name[0])
        snprintf(cm_wow_map_dir, sizeof(cm_wow_map_dir), "World/Maps/%s", cm_wow_map_name);
}

static int CM_WowAdtIndexForWorldCoord(float coord) {
    return (int)floorf(32.0f - coord / CM_WOW_ADT_SIZE);
}

static LPCSTR CM_WowAdtPath(int tile_x, int tile_y, LPSTR out, DWORD out_size) {
    if (!cm_wow_map_dir[0] || !cm_wow_map_name[0] || !out || !out_size)
        return NULL;
    snprintf(out, out_size, "%s/%s_%d_%d.adt", cm_wow_map_dir, cm_wow_map_name, tile_x, tile_y);
    return out;
}

static void CM_WowLoadAdtHeights(int tile_x, int tile_y) {
    PATHSTR path;
    LPBYTE data;
    DWORD size = 0, offset = 0;

    memset(&cm_wow_height_cache, 0, sizeof(cm_wow_height_cache));
    cm_wow_height_cache.loaded = true;
    cm_wow_height_cache.tile_x = tile_x;
    cm_wow_height_cache.tile_y = tile_y;

    if (!CM_WowAdtPath(tile_x, tile_y, path, sizeof(path)))
        return;

    data = FS_ReadFile(path, &size);
    if (!data || !size) {
        SAFE_DELETE(data, FS_FreeFile);
        return;
    }

    while (offset + 8 <= size) {
        BYTE const *tag        = data + offset;
        DWORD       chunk_size = CM_WowRead32(data + offset + 4);
        BYTE const *chunk      = data + offset + 8;

        offset += 8;
        if (offset + chunk_size > size)
            break;

        if (CM_WowTagEquals(tag, "KNCM") && chunk_size >= 0x80) {
            DWORD sub     = 0x80;
            DWORD index_x = CM_WowRead32(chunk + 0x04);
            DWORD index_y = CM_WowRead32(chunk + 0x08);
            cmWowChunkHeight_t *height_chunk = NULL;

            if (index_x < 16 && index_y < 16) {
                height_chunk = &cm_wow_height_cache.chunks[index_y][index_x];
                memcpy(&height_chunk->position, chunk + 0x68, sizeof(height_chunk->position));
            }

            while (height_chunk && sub + 8 <= chunk_size) {
                BYTE const *subtag  = chunk + sub;
                DWORD       sub_size = CM_WowRead32(chunk + sub + 4);
                BYTE const *subchunk = chunk + sub + 8;
                BOOL        is_mcnr  = CM_WowTagEquals(subtag, "RNCM");

                sub += 8;
                if (sub + sub_size > chunk_size)
                    break;
                if (CM_WowTagEquals(subtag, "TVCM") && sub_size >= sizeof(height_chunk->heights)) {
                    memcpy(height_chunk->heights, subchunk, sizeof(height_chunk->heights));
                    height_chunk->has_heights    = true;
                    cm_wow_height_cache.valid    = true;
                }
                sub += sub_size;
                if (is_mcnr && sub_size == 145 * 3 && sub + 13 <= chunk_size)
                    sub += 13;
            }
        }
        offset += chunk_size;
    }
    FS_FreeFile(data);
}

static BOOL CM_WowBarycentricHeight(float px, float py,
                                    float ax, float ay, float ah,
                                    float bx, float by, float bh,
                                    float cx, float cy, float ch,
                                    float *height) {
    float den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    float wa, wb, wc;

    if (fabsf(den) < 0.000001f || !height)
        return false;
    wa = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den;
    wb = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den;
    wc = 1.0f - wa - wb;
    if (wa < -0.0001f || wb < -0.0001f || wc < -0.0001f)
        return false;
    *height = wa * ah + wb * bh + wc * ch;
    return true;
}

static BOOL CM_WowHeightInCell(float const *heights, int row, int col, float fx, float fy, float *height) {
    int   base = row * 17 + col;
    float h_tl = heights[base],     h_tr = heights[base + 1];
    float h_bl = heights[base + 17], h_br = heights[base + 18];
    float h_c  = heights[base + 9];

    return CM_WowBarycentricHeight(fx, fy, 0.5f, 0.5f, h_c,  0.0f, 0.0f, h_tl, 1.0f, 0.0f, h_bl, height) ||
           CM_WowBarycentricHeight(fx, fy, 0.5f, 0.5f, h_c,  0.0f, 1.0f, h_tr, 0.0f, 0.0f, h_tl, height) ||
           CM_WowBarycentricHeight(fx, fy, 0.5f, 0.5f, h_c,  1.0f, 1.0f, h_br, 0.0f, 1.0f, h_tr, height) ||
           CM_WowBarycentricHeight(fx, fy, 0.5f, 0.5f, h_c,  1.0f, 0.0f, h_bl, 1.0f, 1.0f, h_br, height);
}

static BOOL CM_WowTerrainHeightAtPoint(FLOAT sx, FLOAT sy, FLOAT *height) {
    int tile_x = CM_WowAdtIndexForWorldCoord(sy);
    int tile_y = CM_WowAdtIndexForWorldCoord(sx);

    if (!height || tile_x < 0 || tile_x >= 64 || tile_y < 0 || tile_y >= 64)
        return false;
    if (!cm_wow_height_cache.loaded ||
        cm_wow_height_cache.tile_x != tile_x ||
        cm_wow_height_cache.tile_y != tile_y)
        CM_WowLoadAdtHeights(tile_x, tile_y);
    if (!cm_wow_height_cache.valid)
        return false;

    FOR_LOOP(row, 16) {
        FOR_LOOP(col, 16) {
            cmWowChunkHeight_t const *ch = &cm_wow_height_cache.chunks[row][col];
            float local_row, local_col, cell_height;
            int   cell_row, cell_col;

            if (!ch->has_heights ||
                sx > ch->position.x + 0.001f ||
                sx < ch->position.x - 8.0f * CM_WOW_ADT_UNIT_SIZE - 0.001f ||
                sy > ch->position.y + 0.001f ||
                sy < ch->position.y - 8.0f * CM_WOW_ADT_UNIT_SIZE - 0.001f)
                continue;

            local_row = (ch->position.x - sx) / CM_WOW_ADT_UNIT_SIZE;
            local_col = (ch->position.y - sy) / CM_WOW_ADT_UNIT_SIZE;
            cell_row  = (int)floorf(MIN(local_row, 7.9999f));
            cell_col  = (int)floorf(MIN(local_col, 7.9999f));
            if (cell_row < 0 || cell_row >= 8 || cell_col < 0 || cell_col >= 8)
                continue;
            if (CM_WowHeightInCell(ch->heights, cell_row, cell_col,
                                   local_row - cell_row, local_col - cell_col, &cell_height)) {
                *height = ch->position.z + cell_height;
                return true;
            }
        }
    }
    return false;
}

static BOOL CM_WowValidDbc(BYTE const *data, DWORD size,
                            DWORD *records, DWORD *fields,
                            DWORD *record_size, DWORD *string_size) {
    if (!data || size <= 20 || memcmp(data, "WDBC", 4) != 0)
        return false;
    *records     = CM_WowRead32(data + 4);
    *fields      = CM_WowRead32(data + 8);
    *record_size = CM_WowRead32(data + 12);
    *string_size = CM_WowRead32(data + 16);
    if (*fields == 0 || *record_size < *fields * sizeof(DWORD) ||
        20 + *records * *record_size + *string_size > size)
        return false;
    return true;
}

static BOOL CM_WowFindMapId(LPCSTR map_name, DWORD *map_id) {
    LPBYTE data;
    DWORD size = 0, records, fields, record_size, string_size;
    BYTE const *records_base, *strings_base;

    if (!map_name || !*map_name || !map_id)
        return false;

    data = FS_ReadFile("DBFilesClient\\Map.dbc", &size);
    if (!CM_WowValidDbc(data, size, &records, &fields, &record_size, &string_size)) {
        SAFE_DELETE(data, FS_FreeFile);
        return false;
    }
    records_base = data + 20;
    strings_base = records_base + records * record_size;
    FOR_LOOP(record_index, records) {
        BYTE const *record = records_base + record_index * record_size;
        FOR_LOOP(field_index, fields) {
            DWORD string_offset = CM_WowRead32(record + field_index * sizeof(DWORD));
            LPCSTR value = CM_WowDbcString(strings_base, string_size, string_offset);
            if (value && *value && !strcasecmp(value, map_name)) {
                *map_id = CM_WowRead32(record);
                FS_FreeFile(data);
                return true;
            }
        }
    }
    FS_FreeFile(data);
    return false;
}

static LPCSTR CM_WowWorldSafeLocName(BYTE const *record, DWORD fields,
                                      BYTE const *strings_base, DWORD string_size) {
    for (DWORD field_index = 5; field_index < fields; field_index++) {
        DWORD string_offset = CM_WowRead32(record + field_index * sizeof(DWORD));
        LPCSTR value = CM_WowDbcString(strings_base, string_size, string_offset);
        if (value && *value)
            return value;
    }
    return NULL;
}

static DWORD CM_WowCollectWorldSafeLocs(DWORD map_id, LPVECTOR3 first_spawn,
                                        LPSTR first_name, size_t first_name_size) {
    LPBYTE data;
    DWORD size = 0, records, fields, record_size, string_size;
    BYTE const *records_base, *strings_base;
    DWORD count = 0;

    if (!first_spawn)
        return 0;

    data = FS_ReadFile("DBFilesClient\\WorldSafeLocs.dbc", &size);
    if (!CM_WowValidDbc(data, size, &records, &fields, &record_size, &string_size) ||
        fields < 5 || record_size < 5 * sizeof(DWORD)) {
        SAFE_DELETE(data, FS_FreeFile);
        return 0;
    }
    records_base = data + 20;
    strings_base = records_base + records * record_size;
    FOR_LOOP(record_index, records) {
        BYTE const *record = records_base + record_index * record_size;
        cmWowWorldSafeLoc_t const *safe_loc = (cmWowWorldSafeLoc_t const *)record;
        LPCSTR name;
        mapPlayer_t *player;

        if (safe_loc->map_id != map_id)
            continue;

        name = CM_WowWorldSafeLocName(record, fields, strings_base, string_size);
        if (count == 0) {
            memcpy(first_spawn, &safe_loc->position, sizeof(*first_spawn));
            if (first_name && first_name_size) {
                first_name[0] = '\0';
                if (name) {
                    strncpy(first_name, name, first_name_size - 1);
                    first_name[first_name_size - 1] = '\0';
                }
            }
        }
        if (count < MAX_PLAYERS) {
            player = &world.info.players[count];
            player->used = true;
            player->playerType = count == 0 ? kPlayerTypeHuman : kPlayerTypeNone;
            player->playerName = CM_WowCopyString(name);
            player->startingPosition = (VECTOR2){ safe_loc->position.x, safe_loc->position.y };
            cm_wow_spawn_heights[count] = safe_loc->position.z;
        }
        count++;
    }
    FS_FreeFile(data);

    /* store ALL entries (not just 16) for game-module spawn selection */
    CM_WowFreeAllSpawns();
    cm_wow_all_spawn_count = count;
    if (count) {
        cm_wow_all_spawns = MemAlloc(count * sizeof(cmWowSpawnEntry_t));
        if (cm_wow_all_spawns) {
            DWORD idx = 0;
            memset(cm_wow_all_spawns, 0, count * sizeof(cmWowSpawnEntry_t));
            /* re-scan to fill the public array (avoid holding the raw DBC buffer) */
            data = FS_ReadFile("DBFilesClient\\WorldSafeLocs.dbc", &size);
            if (CM_WowValidDbc(data, size, &records, &fields, &record_size, &string_size) &&
                fields >= 5 && record_size >= 5 * sizeof(DWORD)) {
                records_base = data + 20;
                strings_base = records_base + records * record_size;
                FOR_LOOP(ri, records) {
                    BYTE const *r = records_base + ri * record_size;
                    if (CM_WowRead32(r + sizeof(DWORD)) != map_id) continue;
                    cm_wow_all_spawns[idx].pos.x = *(FLOAT *)(r + 2 * sizeof(DWORD));
                    cm_wow_all_spawns[idx].pos.y = *(FLOAT *)(r + 3 * sizeof(DWORD));
                    cm_wow_all_spawns[idx].pos.z = *(FLOAT *)(r + 4 * sizeof(DWORD));
                    LPCSTR raw = CM_WowWorldSafeLocName(r, fields, strings_base, string_size);
                    cm_wow_all_spawns[idx].name = raw ? CM_WowCopyString(raw) : NULL;
                    idx++;
                }
                FS_FreeFile(data);
            }
        }
    }
    return count;
}

static void CM_WowChooseSpawn(LPCSTR mapFilename) {
    char map_name[128]      = { 0 };
    char safe_loc_name[128] = { 0 };
    DWORD map_id = 0;
    DWORD safe_loc_count = 0;
    BOOL has_map_id, has_safe_locs;

    cm_wow_spawn_position = (VECTOR3){ 0.0f, 0.0f, 0.0f };
    memset(cm_wow_spawn_heights, 0, sizeof(cm_wow_spawn_heights));
    world.info.players[0].used       = true;
    world.info.players[0].playerType = kPlayerTypeHuman;
    CM_WowSetMapPath(mapFilename);

    if (!CM_WowExtractMapName(mapFilename, map_name, sizeof(map_name))) {
        world.info.players[0].startingPosition = (VECTOR2){ cm_wow_spawn_position.x, cm_wow_spawn_position.y };
        fprintf(stderr, "CM_LoadMap: WoW spawn fallback at %.3f %.3f %.3f (no map name)\n",
                cm_wow_spawn_position.x, cm_wow_spawn_position.y, cm_wow_spawn_position.z);
        return;
    }

    has_map_id   = CM_WowFindMapId(map_name, &map_id);
    if (has_map_id)
        safe_loc_count = CM_WowCollectWorldSafeLocs(map_id, &cm_wow_spawn_position,
                                                    safe_loc_name, sizeof(safe_loc_name));
    has_safe_locs = safe_loc_count > 0;
    if (!has_safe_locs)
        world.info.players[0].startingPosition = (VECTOR2){ cm_wow_spawn_position.x, cm_wow_spawn_position.y };

    if (has_safe_locs)
        fprintf(stderr, "CM_LoadMap: WoW map %s id=%u loaded %u WorldSafeLocs spawn candidates, first%s%s at %.3f %.3f %.3f\n",
                map_name, (unsigned)map_id,
                (unsigned)safe_loc_count,
                safe_loc_name[0] ? " " : "", safe_loc_name,
                cm_wow_spawn_position.x, cm_wow_spawn_position.y, cm_wow_spawn_position.z);
    else if (has_map_id)
        fprintf(stderr, "CM_LoadMap: WoW map %s id=%u has no WorldSafeLocs entry, spawn fallback at %.3f %.3f %.3f\n",
                map_name, (unsigned)map_id,
                cm_wow_spawn_position.x, cm_wow_spawn_position.y, cm_wow_spawn_position.z);
    else
        fprintf(stderr, "CM_LoadMap: WoW map %s has no Map.dbc entry, spawn fallback at %.3f %.3f %.3f\n",
                map_name,
                cm_wow_spawn_position.x, cm_wow_spawn_position.y, cm_wow_spawn_position.z);
}

/* ---- public API ---- */

bool CM_LoadMapFormat(LPCSTR mapFilename) {
    memset(&world, 0, sizeof(world));
    if (mapFilename) {
        size_t len = strlen(mapFilename);
        world.info.mapName = MemAlloc(len + 1);
        memcpy(world.info.mapName, mapFilename, len + 1);
    }
    CM_WowChooseSpawn(mapFilename);
    return true;
}

FLOAT CM_GetHeightAtPoint(FLOAT sx, FLOAT sy) {
    FLOAT terrain_height;
    if (CM_WowTerrainHeightAtPoint(sx, sy, &terrain_height))
        return terrain_height;
    FOR_LOOP(i, MAX_PLAYERS) {
        if (world.info.players[i].used &&
            fabsf(world.info.players[i].startingPosition.x - sx) < 0.001f &&
            fabsf(world.info.players[i].startingPosition.y - sy) < 0.001f)
            return cm_wow_spawn_heights[i];
    }
    return cm_wow_spawn_position.z;
}

VECTOR2 CM_GetNormalizedMapPosition(FLOAT x, FLOAT y) {
    return (VECTOR2){ x, y };
}

VECTOR2 CM_GetDenormalizedMapPosition(FLOAT x, FLOAT y) {
    return (VECTOR2){ x, y };
}

BOX2 CM_GetWorldBounds(void) {
    return (BOX2){
        .min = { -32768.0f, -32768.0f },
        .max = {  32768.0f,  32768.0f },
    };
}

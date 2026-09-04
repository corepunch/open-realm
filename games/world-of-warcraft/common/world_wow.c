#include "common/common.h"
#include "common/ui_constants.h"
#include "common/stb_dbc.h"
#include "wow_chunks.h"
#include <float.h>
#include <limits.h>
#include <math.h>

BOOL CL_GameDefaultCamera(gameCamera_t *camera) {
    if (!camera) return false;
    *camera = (gameCamera_t){ .distance = 8.0f, .pitch = 342.0f, .yaw = 0.0f, .fov = WOW_CAMERA_FOV,
        .znear = WOW_WORLD_NEAR_CLIP, .zfar = WOW_WORLD_FAR_CLIP };
    return true;
}

FLOAT CL_GameCameraHeightAtPoint(FLOAT x, FLOAT y) { return CM_GetHeightAtPoint(x, y); }
FLOAT CL_GameLerpDegrees(FLOAT a, FLOAT b, FLOAT fraction) { return a + (b - a) * fraction; }
FLOAT CM_GetCameraHeightOffset(void) { return 0; }

#define CM_WOW_ADT_SIZE       533.333313f
#define CM_WOW_ADT_UNIT_SIZE  (CM_WOW_ADT_SIZE / 16.0f / 8.0f)
#define CM_WOW_MCVT_COUNT     (9 * 9 + 8 * 8)
#define CM_WOW_HEIGHT_CACHE_TILES 16
#define CM_WOW_WMO_DETAIL     0x04
#define CM_WOW_WMO_COLLISION  0x08
#define CM_WOW_WMO_RENDER     0x20
#define CM_WOW_WMO_GRID 32
#define CM_WOW_PLAYER_RADIUS 0.50f // world units; horizontal player cylinder radius; used by swept WMO wall rays
#define CM_WOW_PLAYER_LOW_Z 0.35f // world units above feet; catches low walls without treating floors as walls
#define CM_WOW_PLAYER_HIGH_Z 1.50f // world units above feet; catches full-height WMO walls and doors
#define CM_WOW_WALL_NORMAL_Z 0.65f // abs world normal Z; surfaces below this up component block horizontal movement

typedef struct {
    VECTOR3 a, b, c;
    BOX3 bounds;
} cmWowWmoTri_t;

typedef struct {
    WORD flags;
    SHORT children[2];
    WORD face_count;
    DWORD first_face;
    FLOAT distance;
} cmWowWmoBspNode_t;

_Static_assert(sizeof(cmWowWmoBspNode_t) == 16, "WMO MOBN node must match the on-disk 16-byte record");

typedef struct {
    BOX3 bounds;
    cmWowWmoBspNode_t *nodes;
    DWORD node_count, *triangles, triangle_count;
} cmWowWmoGroup_t;

typedef struct cmWowWmoModel_s {
    PATHSTR path;
    cmWowWmoTri_t *triangles;
    DWORD count, capacity;
    BOX3 bounds;
    DWORD cell_offsets[CM_WOW_WMO_GRID * CM_WOW_WMO_GRID + 1];
    DWORD *cell_triangles;
    cmWowWmoGroup_t *groups;
    DWORD group_count;
    BOOL missing_bsp;
    BOOL loaded, valid;
    struct cmWowWmoModel_s *next;
} cmWowWmoModel_t;

typedef struct cmWowWmoInstance_s {
    PATHSTR path;
    MATRIX4 matrix, inverse;
    BOX3 bounds;
    cmWowWmoModel_t *model;
    struct cmWowWmoInstance_s *next;
} cmWowWmoInstance_t;

typedef struct {
    cmWowWmoModel_t const *model;
    cmWowWmoGroup_t const *group;
    cmWowWmoInstance_t const *instance;
    LPCVECTOR3 start, end;
    FLOAT best;
    BOOL walls_only;
} cmWowTrace_t;

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
    DWORD             use_stamp;
    cmWowChunkHeight_t chunks[16][16];
    cmWowWmoInstance_t *wmos;
} cmWowAdtHeightCache_t;

typedef struct {
    DWORD id;
    DWORD map_id;
    VECTOR3 position;
} cmWowWorldSafeLoc_t;

static VECTOR3              cm_wow_spawn_position = { 0.0f, 0.0f, 0.0f };
static DWORD                cm_wow_map_id = ~0u;
static FLOAT                cm_wow_spawn_heights[MAX_PLAYERS];
static char                 cm_wow_map_dir[PATH_MAX]  = { 0 };
static char                 cm_wow_map_name[128]      = { 0 };
static cmWowAdtHeightCache_t cm_wow_height_cache[CM_WOW_HEIGHT_CACHE_TILES];
static DWORD                 cm_wow_height_cache_stamp;
static cmWowWmoModel_t       *cm_wow_wmo_models;

static void CM_WowFreeWmos(void) {
    cmWowWmoModel_t *model = cm_wow_wmo_models;
    FOR_LOOP(i, CM_WOW_HEIGHT_CACHE_TILES) {
        cmWowWmoInstance_t *instance = cm_wow_height_cache[i].wmos;
        while (instance) {
            cmWowWmoInstance_t *next = instance->next;
            MemFree(instance); instance = next;
        }
    }
    while (model) {
        cmWowWmoModel_t *next = model->next;
        SAFE_DELETE(model->triangles, MemFree);
        SAFE_DELETE(model->cell_triangles, MemFree);
        FOR_LOOP(i, model->group_count) { SAFE_DELETE(model->groups[i].nodes, MemFree); SAFE_DELETE(model->groups[i].triangles, MemFree); }
        SAFE_DELETE(model->groups, MemFree);
        MemFree(model); model = next;
    }
    cm_wow_wmo_models = NULL;
}

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
DWORD CM_WowGetMapId(void) { return cm_wow_map_id; }

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

    CM_WowFreeWmos();
    memset(cm_wow_height_cache, 0, sizeof(cm_wow_height_cache));
    cm_wow_height_cache_stamp = 0;
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

LPCSTR CM_WowAdtPath(int tile_x, int tile_y, LPSTR out, DWORD out_size) {
    if (!cm_wow_map_dir[0] || !cm_wow_map_name[0] || !out || !out_size)
        return NULL;
    snprintf(out, out_size, "%s/%s_%d_%d.adt", cm_wow_map_dir, cm_wow_map_name, tile_x, tile_y);
    return out;
}

typedef struct {
    DWORD name_id, unique_id;
    VECTOR3 position, rotation;
    struct { VECTOR3 min, max; } extents;
    WORD flags, doodad_set, name_set, scale;
} cmWowWmoDef_t;

/* MODF and WMO vertices use the same transform as the renderer; collision must agree bit-for-bit with visuals. */
static void CM_WowWmoMatrix(cmWowWmoDef_t const *def, LPMATRIX4 matrix) {
    MATRIX4 basis, tmp;
    VECTOR3 origin = CM_WowObjectPoint(def->position.x, def->position.y, def->position.z);
    Matrix4_identity(matrix); Matrix4_translate(matrix, &origin);
    Matrix4_identity(&basis);
    basis.v[0] = 0.0f; basis.v[1] = 1.0f; basis.v[2] = 0.0f;
    basis.v[4] = 0.0f; basis.v[5] = 0.0f; basis.v[6] = 1.0f;
    basis.v[8] = 1.0f; basis.v[9] = 0.0f; basis.v[10] = 0.0f;
    Matrix4_multiply(matrix, &basis, &tmp); *matrix = tmp;
    Matrix4_rotate(matrix, &(VECTOR3){ 0.0f, def->rotation.y - 270.0f, 0.0f }, ROTATE_XYZ);
    Matrix4_rotate(matrix, &(VECTOR3){ 0.0f, 0.0f, -def->rotation.x }, ROTATE_XYZ);
    Matrix4_rotate(matrix, &(VECTOR3){ def->rotation.z - 90.0f, 0.0f, 0.0f }, ROTATE_XYZ);
    if (def->scale) {
        float scale = def->scale / 1024.0f;
        Matrix4_scale(matrix, &(VECTOR3){ scale, scale, scale });
    }
}

static void CM_WowWmoGroupPath(LPCSTR root, DWORD index, LPSTR out, DWORD out_size) {
    size_t len = strlen(root);
    if (len > 4 && !strcasecmp(root + len - 4, ".wmo"))
        snprintf(out, out_size, "%.*s_%03u.wmo", (int)(len - 4), root, (unsigned)index);
    else
        snprintf(out, out_size, "%s_%03u.wmo", root, (unsigned)index);
}

static BOOL CM_WowWmoAppendTriangle(cmWowWmoModel_t *model, LPCVECTOR3 a, LPCVECTOR3 b, LPCVECTOR3 c) {
    cmWowWmoTri_t *tri;
    if (model->count == model->capacity) {
        DWORD capacity = model->capacity ? model->capacity * 2 : 1024;
        cmWowWmoTri_t *triangles = MemAlloc(capacity * sizeof(*triangles));
        if (!triangles) return false;
        if (model->triangles) { memcpy(triangles, model->triangles, model->count * sizeof(*triangles)); MemFree(model->triangles); }
        model->triangles = triangles; model->capacity = capacity;
    }
    tri = model->triangles + model->count++; tri->a = *a; tri->b = *b; tri->c = *c;
    tri->bounds.min = (VECTOR3){ MIN(a->x, MIN(b->x, c->x)), MIN(a->y, MIN(b->y, c->y)), MIN(a->z, MIN(b->z, c->z)) };
    tri->bounds.max = (VECTOR3){ MAX(a->x, MAX(b->x, c->x)), MAX(a->y, MAX(b->y, c->y)), MAX(a->z, MAX(b->z, c->z)) };
    return true;
}

/* MOPY bit 0x04 marks detail/non-colliding faces; all other authored triangles are floor candidates. */
static BOOL CM_WowLoadWmoGroup(cmWowWmoModel_t *model, DWORD group_index) {
    PATHSTR path;
    LPBYTE data;
    DWORD size = 0, offset = 0, mopy_count = 0, index_count = 0, vertex_count = 0;
    BYTE const *mopy = NULL, *mobn = NULL;
    WORD const *indices = NULL;
    WORD const *mobr = NULL;
    VECTOR3 const *vertices = NULL;
    DWORD mobn_size = 0, mobr_count = 0;
    cmWowWmoGroup_t *group = model->groups + group_index;
    CM_WowWmoGroupPath(model->path, group_index, path, sizeof(path));
    data = FS_ReadFile(path, &size);
    if (!data || !size) { fprintf(stderr, "CM WoW WMO: missing group %s\n", path); SAFE_DELETE(data, FS_FreeFile); return false; }
    while (offset + 8 <= size) {
        BYTE const *tag = data + offset;
        DWORD chunk_size = Stb_DbcRead32(data + offset + 4);
        BYTE const *chunk = data + offset + 8;
        offset += 8;
        if (offset + chunk_size > size) break;
        if (*(DWORD const *)tag == ID_PGOM && chunk_size >= 0x44) {
            DWORD sub = 0x44;
            memcpy(&group->bounds.min, chunk + 0x0c, sizeof(VECTOR3));
            memcpy(&group->bounds.max, chunk + 0x18, sizeof(VECTOR3));
            while (sub + 8 <= chunk_size) {
                BYTE const *subtag = chunk + sub;
                DWORD sub_size = Stb_DbcRead32(chunk + sub + 4);
                BYTE const *subchunk = chunk + sub + 8;
                sub += 8;
                if (sub + sub_size > chunk_size) break;
                if (*(DWORD const *)subtag == ID_YPOM) { mopy = subchunk; mopy_count = sub_size / 2; }
                else if (*(DWORD const *)subtag == ID_IVOM) { indices = (WORD const *)subchunk; index_count = sub_size / 2; }
                else if (*(DWORD const *)subtag == ID_TVOM) { vertices = (VECTOR3 const *)subchunk; vertex_count = sub_size / sizeof(*vertices); }
                else if (*(DWORD const *)subtag == ID_NBOM) { mobn = subchunk; mobn_size = sub_size; }
                else if (*(DWORD const *)subtag == ID_RBOM) { mobr = (WORD const *)subchunk; mobr_count = sub_size / 2; }
                sub += sub_size;
            }
        }
        offset += chunk_size;
    }
    if (!vertices || !indices) { fprintf(stderr, "CM WoW WMO: group %s has no collision geometry\n", path); FS_FreeFile(data); return false; }
    DWORD poly_count = index_count / 3;
    DWORD *poly_map = MemAlloc(poly_count * sizeof(*poly_map));
    BYTE *poly_used = MemAlloc(poly_count);
    if (!poly_map || !poly_used) { SAFE_DELETE(poly_map, MemFree); SAFE_DELETE(poly_used, MemFree); FS_FreeFile(data); return false; }
    FOR_LOOP(i, poly_count) poly_map[i] = ~0u;
    memset(poly_used, 0, poly_count);
    /* MOBR is the authored collision face set. MOPY filtering is only a fallback when the BSP is absent. */
    if (mobn && mobr && mobn_size % sizeof(cmWowWmoBspNode_t) == 0) {
        FOR_LOOP(i, mobr_count) if (mobr[i] < poly_count) poly_used[mobr[i]] = true;
    } else {
        FOR_LOOP(i, poly_count) {
            BYTE flags = mopy && i < mopy_count ? mopy[i * 2] : CM_WOW_WMO_RENDER;
            BYTE material = mopy && i < mopy_count ? mopy[i * 2 + 1] : 0;
            poly_used[i] = (flags & CM_WOW_WMO_COLLISION) || ((flags & CM_WOW_WMO_RENDER) && !(flags & CM_WOW_WMO_DETAIL)) || material == 0xff;
        }
    }
    for (DWORD i = 0; i + 2 < index_count; i += 3) {
        DWORD poly = i / 3;
        if (!poly_used[poly] || indices[i] >= vertex_count || indices[i + 1] >= vertex_count || indices[i + 2] >= vertex_count)
            continue;
        if (!CM_WowWmoAppendTriangle(model, vertices + indices[i], vertices + indices[i + 1], vertices + indices[i + 2])) {
            MemFree(poly_used); MemFree(poly_map); FS_FreeFile(data); return false;
        }
        poly_map[poly] = model->count - 1;
    }
    if (mobn && mobr && mobn_size % sizeof(cmWowWmoBspNode_t) == 0) {
        group->node_count = mobn_size / sizeof(cmWowWmoBspNode_t);
        group->nodes = MemAlloc(mobn_size); memcpy(group->nodes, mobn, mobn_size);
        group->triangles = MemAlloc(mobr_count * sizeof(*group->triangles));
        group->triangle_count = mobr_count;
        FOR_LOOP(i, mobr_count) group->triangles[i] = mobr[i] < poly_count ? poly_map[mobr[i]] : ~0u;
    } else {
        model->missing_bsp = true;
        fprintf(stderr, "CM WoW WMO: %s has no MOBN/MOBR collision BSP; using indexed mesh grid\n", path);
    }
    MemFree(poly_used);
    MemFree(poly_map);
    FS_FreeFile(data);
    return true;
}

static int CM_WowWmoCell(FLOAT value, FLOAT min, FLOAT max) {
    if (max - min < 0.0001f) return 0;
    return MAX(0, MIN(CM_WOW_WMO_GRID - 1, (int)((value - min) * CM_WOW_WMO_GRID / (max - min))));
}

/* A fixed local-XZ grid makes the common upright WMO floor ray inspect one small triangle bucket. */
static BOOL CM_WowBuildWmoGrid(cmWowWmoModel_t *model) {
    DWORD cells = CM_WOW_WMO_GRID * CM_WOW_WMO_GRID, *cursor;
    model->bounds.min = (VECTOR3){ FLT_MAX, FLT_MAX, FLT_MAX };
    model->bounds.max = (VECTOR3){ -FLT_MAX, -FLT_MAX, -FLT_MAX };
    FOR_LOOP(i, model->count) {
        cmWowWmoTri_t const *tri = model->triangles + i;
        model->bounds.min.x = MIN(model->bounds.min.x, tri->bounds.min.x);
        model->bounds.min.y = MIN(model->bounds.min.y, tri->bounds.min.y);
        model->bounds.min.z = MIN(model->bounds.min.z, tri->bounds.min.z);
        model->bounds.max.x = MAX(model->bounds.max.x, tri->bounds.max.x);
        model->bounds.max.y = MAX(model->bounds.max.y, tri->bounds.max.y);
        model->bounds.max.z = MAX(model->bounds.max.z, tri->bounds.max.z);
    }
    FOR_LOOP(i, model->count) {
        cmWowWmoTri_t const *tri = model->triangles + i;
        int x0 = CM_WowWmoCell(tri->bounds.min.x, model->bounds.min.x, model->bounds.max.x);
        int x1 = CM_WowWmoCell(tri->bounds.max.x, model->bounds.min.x, model->bounds.max.x);
        int z0 = CM_WowWmoCell(tri->bounds.min.z, model->bounds.min.z, model->bounds.max.z);
        int z1 = CM_WowWmoCell(tri->bounds.max.z, model->bounds.min.z, model->bounds.max.z);
        for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) model->cell_offsets[z * CM_WOW_WMO_GRID + x + 1]++;
    }
    FOR_LOOP(i, cells) model->cell_offsets[i + 1] += model->cell_offsets[i];
    model->cell_triangles = MemAlloc(model->cell_offsets[cells] * sizeof(*model->cell_triangles));
    cursor = MemAlloc(cells * sizeof(*cursor));
    if (!model->cell_triangles || !cursor) { SAFE_DELETE(cursor, MemFree); return false; }
    memcpy(cursor, model->cell_offsets, cells * sizeof(*cursor));
    FOR_LOOP(i, model->count) {
        cmWowWmoTri_t const *tri = model->triangles + i;
        int x0 = CM_WowWmoCell(tri->bounds.min.x, model->bounds.min.x, model->bounds.max.x);
        int x1 = CM_WowWmoCell(tri->bounds.max.x, model->bounds.min.x, model->bounds.max.x);
        int z0 = CM_WowWmoCell(tri->bounds.min.z, model->bounds.min.z, model->bounds.max.z);
        int z1 = CM_WowWmoCell(tri->bounds.max.z, model->bounds.min.z, model->bounds.max.z);
        for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) model->cell_triangles[cursor[z * CM_WOW_WMO_GRID + x]++] = i;
    }
    MemFree(cursor);
    return true;
}

/* Models are shared by MODF instances and loaded only after the player enters an instance's authored bounds. */
static cmWowWmoModel_t *CM_WowGetWmoModel(LPCSTR path) {
    cmWowWmoModel_t *model;
    LPBYTE data;
    DWORD size = 0, offset = 0, group_count = 0;
    for (model = cm_wow_wmo_models; model; model = model->next)
        if (!strcasecmp(model->path, path)) return model->valid ? model : NULL;
    model = MemAlloc(sizeof(*model)); memset(model, 0, sizeof(*model));
    snprintf(model->path, sizeof(model->path), "%s", path); model->next = cm_wow_wmo_models; cm_wow_wmo_models = model;
    data = FS_ReadFile(path, &size);
    if (!data || !size) { fprintf(stderr, "CM WoW WMO: missing root %s\n", path); SAFE_DELETE(data, FS_FreeFile); model->loaded = true; return NULL; }
    while (offset + 8 <= size) {
        BYTE const *tag = data + offset;
        DWORD chunk_size = Stb_DbcRead32(data + offset + 4);
        BYTE const *chunk = data + offset + 8;
        offset += 8;
        if (offset + chunk_size > size) break;
        if (*(DWORD const *)tag == ID_DHOM && chunk_size >= 8) group_count = Stb_DbcRead32(chunk + 4);
        offset += chunk_size;
    }
    FS_FreeFile(data);
    model->groups = MemAlloc(group_count * sizeof(*model->groups));
    if (!model->groups) { model->loaded = true; return NULL; }
    memset(model->groups, 0, group_count * sizeof(*model->groups)); model->group_count = group_count;
    FOR_LOOP(i, group_count)
        if (!CM_WowLoadWmoGroup(model, i)) { model->loaded = true; return NULL; }
    model->loaded = true; model->valid = model->count > 0 && (!model->missing_bsp || CM_WowBuildWmoGrid(model));
    fprintf(stderr, "CM WoW WMO: loaded %u collision triangles from %s\n", (unsigned)model->count, model->path);
    return model->valid ? model : NULL;
}

static LPCSTR CM_WowStringAt(LPCSTR blob, DWORD size, DWORD offset) {
    return blob && offset < size && memchr(blob + offset, '\0', size - offset) ? blob + offset : NULL;
}

static void CM_WowFreeAdtWmos(cmWowAdtHeightCache_t *cache) {
    cmWowWmoInstance_t *instance = cache ? cache->wmos : NULL;
    while (instance) {
        cmWowWmoInstance_t *next = instance->next;
        MemFree(instance); instance = next;
    }
    if (cache) cache->wmos = NULL;
}

/* Cache only lightweight MODF instances; collision geometry remains lazy until a query enters its authored bounds. */
static void CM_WowLoadAdtWmos(cmWowAdtHeightCache_t *cache, BYTE const *data, DWORD size) {
    LPCSTR names = NULL;
    DWORD names_size = 0, offset = 0, name_count = 0, def_count = 0;
    DWORD const *name_offsets = NULL;
    cmWowWmoDef_t const *defs = NULL;
    while (offset + 8 <= size) {
        BYTE const *tag = data + offset;
        DWORD chunk_size = Stb_DbcRead32(data + offset + 4);
        BYTE const *chunk = data + offset + 8;
        offset += 8;
        if (offset + chunk_size > size) break;
        if (*(DWORD const *)tag == ID_OMWM) { names = (LPCSTR)chunk; names_size = chunk_size; }
        else if (*(DWORD const *)tag == ID_DIWM) { name_offsets = (DWORD const *)chunk; name_count = chunk_size / 4; }
        else if (*(DWORD const *)tag == ID_FDOM) { defs = (cmWowWmoDef_t const *)chunk; def_count = chunk_size / sizeof(*defs); }
        offset += chunk_size;
    }
    if (!cache || !names || !name_offsets || !defs) return;
    FOR_LOOP(i, def_count) {
        cmWowWmoDef_t const *def = defs + i;
        LPCSTR path = def->name_id < name_count ? CM_WowStringAt(names, names_size, name_offsets[def->name_id]) : NULL;
        cmWowWmoInstance_t *instance;
        VECTOR3 a, b;
        if (!path) continue;
        instance = MemAlloc(sizeof(*instance)); memset(instance, 0, sizeof(*instance));
        snprintf(instance->path, sizeof(instance->path), "%s", path);
        CM_WowWmoMatrix(def, &instance->matrix); Matrix4_inverse(&instance->matrix, &instance->inverse);
        a = CM_WowObjectPoint(def->extents.min.x, def->extents.min.y, def->extents.min.z);
        b = CM_WowObjectPoint(def->extents.max.x, def->extents.max.y, def->extents.max.z);
        instance->bounds.min = (VECTOR3){ MIN(a.x,b.x), MIN(a.y,b.y), MIN(a.z,b.z) };
        instance->bounds.max = (VECTOR3){ MAX(a.x,b.x), MAX(a.y,b.y), MAX(a.z,b.z) };
        instance->next = cache->wmos; cache->wmos = instance;
    }
}

static void CM_WowLoadAdtHeights(int tile_x, int tile_y) {
    cmWowAdtHeightCache_t *cache = NULL, *oldest = NULL;
    PATHSTR path;
    LPBYTE data;
    DWORD size = 0, offset = 0;

    FOR_LOOP(i, CM_WOW_HEIGHT_CACHE_TILES) {
        if (cm_wow_height_cache[i].loaded && cm_wow_height_cache[i].tile_x == tile_x && cm_wow_height_cache[i].tile_y == tile_y) {
            cm_wow_height_cache[i].use_stamp = ++cm_wow_height_cache_stamp;
            return;
        }
        if (!cm_wow_height_cache[i].loaded)
            cache = &cm_wow_height_cache[i];
        else if (!oldest || cm_wow_height_cache[i].use_stamp < oldest->use_stamp)
            oldest = &cm_wow_height_cache[i];
    }
    if (!cache)
        cache = oldest;
    CM_WowFreeAdtWmos(cache);
    memset(cache, 0, sizeof(*cache));
    cache->loaded = true;
    cache->tile_x = tile_x;
    cache->tile_y = tile_y;
    cache->use_stamp = ++cm_wow_height_cache_stamp;

    if (!CM_WowAdtPath(tile_x, tile_y, path, sizeof(path)))
        return;

    data = FS_ReadFile(path, &size);
    if (!data || !size) {
        SAFE_DELETE(data, FS_FreeFile);
        return;
    }

    while (offset + 8 <= size) {
        BYTE const *tag        = data + offset;
        DWORD       chunk_size = Stb_DbcRead32(data + offset + 4);
        BYTE const *chunk      = data + offset + 8;

        offset += 8;
        if (offset + chunk_size > size)
            break;

        if (*(DWORD const *)tag == ID_KNCM && chunk_size >= 0x80) {
            DWORD sub     = 0x80;
            DWORD index_x = Stb_DbcRead32(chunk + 0x04);
            DWORD index_y = Stb_DbcRead32(chunk + 0x08);
            cmWowChunkHeight_t *height_chunk = NULL;

            if (index_x < 16 && index_y < 16) {
                height_chunk = &cache->chunks[index_y][index_x];
                memcpy(&height_chunk->position, chunk + 0x68, sizeof(height_chunk->position));
            }

            while (height_chunk && sub + 8 <= chunk_size) {
                BYTE const *subtag  = chunk + sub;
                DWORD       sub_size = Stb_DbcRead32(chunk + sub + 4);
                BYTE const *subchunk = chunk + sub + 8;
                BOOL        is_mcnr  = *(DWORD const *)subtag == ID_RNCM;

                sub += 8;
                if (sub + sub_size > chunk_size)
                    break;
                if (*(DWORD const *)subtag == ID_TVCM && sub_size >= sizeof(height_chunk->heights)) {
                    memcpy(height_chunk->heights, subchunk, sizeof(height_chunk->heights));
                    height_chunk->has_heights    = true;
                    cache->valid                 = true;
                }
                sub += sub_size;
                if (is_mcnr && sub_size == 145 * 3 && sub + 13 <= chunk_size)
                    sub += 13;
            }
        }
        offset += chunk_size;
    }
    CM_WowLoadAdtWmos(cache, data, size);
    FS_FreeFile(data);
}

/* Two-sided segment/triangle test: WMO winding differs between indoor and outdoor groups. */
BOOL CM_WowRayTriangle(LPCVECTOR3 start, LPCVECTOR3 end, LPCVECTOR3 a, LPCVECTOR3 b, LPCVECTOR3 c, FLOAT *fraction) {
    VECTOR3 dir = Vector3_sub(end, start), edge1 = Vector3_sub(b, a), edge2 = Vector3_sub(c, a);
    VECTOR3 p = Vector3_cross(&dir, &edge2), t, q;
    FLOAT det = Vector3_dot(&edge1, &p), inv, u, v, hit;
    if (fabsf(det) < 0.000001f || !fraction) return false;
    inv = 1.0f / det; t = Vector3_sub(start, a); u = Vector3_dot(&t, &p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    q = Vector3_cross(&t, &edge1); v = Vector3_dot(&dir, &q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    hit = Vector3_dot(&edge2, &q) * inv;
    if (hit < 0.0f || hit > 1.0f) return false;
    *fraction = hit; return true;
}

/* A WMO triangle blocks horizontal movement only when its transformed normal is wall-like. */
static BOOL CM_WowTriangleIsWall(cmWowWmoInstance_t const *instance, cmWowWmoTri_t const *tri) {
    VECTOR3 ab = Vector3_sub(&tri->b, &tri->a), ac = Vector3_sub(&tri->c, &tri->a);
    VECTOR3 n = Vector3_cross(&ab, &ac), tip = Vector3_add(&tri->a, &n);
    VECTOR3 world_a = Matrix4_multiply_vector3(&instance->matrix, &tri->a);
    VECTOR3 world_tip = Matrix4_multiply_vector3(&instance->matrix, &tip);
    VECTOR3 world_n = Vector3_sub(&world_tip, &world_a);
    if (Vector3_len(&world_n) < 0.000001f) return false;
    Vector3_normalize(&world_n);
    return fabsf(world_n.z) < CM_WOW_WALL_NORMAL_Z;
}

/* Traverse the file-authored CAaBsp tree; MOBR leaves point back to MOVI triangles through the retained map. */
static void CM_WowTraceWmoBsp(cmWowTrace_t *trace, LONG node_index, DWORD depth) {
    cmWowWmoBspNode_t const *node;
    FLOAT a, b;
    int axis;
    if (node_index < 0 || (DWORD)node_index >= trace->group->node_count || depth > trace->group->node_count) return;
    node = trace->group->nodes + node_index;
    for (DWORD i = node->first_face; i < node->first_face + node->face_count && i < trace->group->triangle_count; i++) {
        DWORD index = trace->group->triangles[i];
        cmWowWmoTri_t const *tri;
        FLOAT hit;
        if (index >= trace->model->count) continue;
        tri = trace->model->triangles + index;
        if (trace->walls_only && !CM_WowTriangleIsWall(trace->instance, tri)) continue;
        if (CM_WowRayTriangle(trace->start, trace->end, &tri->a, &tri->b, &tri->c, &hit) && hit < trace->best)
            trace->best = hit;
    }
    if (node->flags & 0x04) return;
    axis = node->flags & 0x03;
    if (axis > 2) return;
    a = ((FLOAT const *)trace->start)[axis] - node->distance; b = ((FLOAT const *)trace->end)[axis] - node->distance;
    if (a <= 0.0f && b <= 0.0f) CM_WowTraceWmoBsp(trace, node->children[0], depth + 1);
    else if (a >= 0.0f && b >= 0.0f) CM_WowTraceWmoBsp(trace, node->children[1], depth + 1);
    else {
        int near = a <= 0.0f ? 0 : 1;
        CM_WowTraceWmoBsp(trace, node->children[near], depth + 1);
        CM_WowTraceWmoBsp(trace, node->children[1 - near], depth + 1);
    }
}

#ifdef BZ_TESTS
BOOL CM_WowTestBspRay(LPCVECTOR3 start, LPCVECTOR3 end, FLOAT *fraction) {
    cmWowWmoTri_t triangles[] = { { .a = { 0, 0, 0 }, .b = { 1, 0, 0 }, .c = { 0, 1, 0 } } };
    cmWowWmoBspNode_t nodes[] = {
        { .flags = 0, .children = { 1, 2 }, .distance = 0.5f },
        { .flags = 4 },
        { .flags = 4, .face_count = 1 },
    };
    DWORD refs[] = { 0 };
    cmWowWmoModel_t model = { .triangles = triangles, .count = 1 };
    cmWowWmoGroup_t group = { .nodes = nodes, .node_count = 3, .triangles = refs, .triangle_count = 1 };
    cmWowTrace_t trace = { .model = &model, .group = &group, .start = start, .end = end, .best = 2.0f };
    CM_WowTraceWmoBsp(&trace, 0, 0); *fraction = trace.best;
    return trace.best <= 1.0f;
}

BOOL CM_WowTestWallRay(BOOL wall) {
    cmWowWmoTri_t triangle = wall
        ? (cmWowWmoTri_t){ .a = { 0, 0, 0 }, .b = { 0, 1, 0 }, .c = { 0, 0, 1 } }
        : (cmWowWmoTri_t){ .a = { 0, 0, 0 }, .b = { 1, 0, 0 }, .c = { 0, 1, 0 } };
    cmWowWmoBspNode_t node = { .flags = 4, .face_count = 1 };
    DWORD ref = 0;
    cmWowWmoModel_t model = { .triangles = &triangle, .count = 1 };
    cmWowWmoGroup_t group = { .nodes = &node, .node_count = 1, .triangles = &ref, .triangle_count = 1 };
    cmWowWmoInstance_t instance;
    VECTOR3 start = wall ? (VECTOR3){ -1, .25f, .25f } : (VECTOR3){ .25f, .25f, 1 };
    VECTOR3 end = wall ? (VECTOR3){ 1, .25f, .25f } : (VECTOR3){ .25f, .25f, -1 };
    cmWowTrace_t trace = { .model = &model, .group = &group, .instance = &instance,
                           .start = &start, .end = &end, .best = 2.0f, .walls_only = true };
    Matrix4_identity(&instance.matrix); CM_WowTraceWmoBsp(&trace, 0, 0);
    return trace.best <= 1.0f;
}
#endif

/* Select the highest authored surface reachable by a small upward step, just like a ground trace. */
FLOAT CM_WowFloorHeight(FLOAT sx, FLOAT sy, FLOAT ref_z, FLOAT step_up) {
    cmWowAdtHeightCache_t *cache = NULL;
    FLOAT best = CM_GetHeightAtPoint(sx, sy), top = ref_z + MAX(step_up, 0.0f);
    int tile_x = CM_WowAdtIndexForWorldCoord(sy), tile_y = CM_WowAdtIndexForWorldCoord(sx);
    if (tile_x < 0 || tile_x >= 64 || tile_y < 0 || tile_y >= 64) return best;
    CM_WowLoadAdtHeights(tile_x, tile_y);
    FOR_LOOP(i, CM_WOW_HEIGHT_CACHE_TILES)
        if (cm_wow_height_cache[i].loaded && cm_wow_height_cache[i].tile_x == tile_x && cm_wow_height_cache[i].tile_y == tile_y) { cache = cm_wow_height_cache + i; break; }
    if (!cache) return best;
    for (cmWowWmoInstance_t *instance = cache->wmos; instance; instance = instance->next) {
        VECTOR3 world_start, world_end, start, finish, seg_min, seg_max;
        if (sx < instance->bounds.min.x || sx > instance->bounds.max.x || sy < instance->bounds.min.y || sy > instance->bounds.max.y ||
            top < instance->bounds.min.z || best > instance->bounds.max.z) continue;
        if (!instance->model) instance->model = CM_WowGetWmoModel(instance->path);
        if (!instance->model) continue;
        world_start = (VECTOR3){ sx, sy, top };
        world_end = (VECTOR3){ sx, sy, MIN(best, instance->bounds.min.z) - 1.0f };
        start = Matrix4_multiply_vector3(&instance->inverse, &world_start);
        finish = Matrix4_multiply_vector3(&instance->inverse, &world_end);
        seg_min = (VECTOR3){ MIN(start.x,finish.x), MIN(start.y,finish.y), MIN(start.z,finish.z) };
        seg_max = (VECTOR3){ MAX(start.x,finish.x), MAX(start.y,finish.y), MAX(start.z,finish.z) };
        if (!instance->model->missing_bsp) {
            FOR_LOOP(i, instance->model->group_count) {
                cmWowWmoGroup_t const *group = instance->model->groups + i;
                cmWowTrace_t trace = { .model = instance->model, .group = group, .instance = instance,
                                       .start = &start, .end = &finish, .best = 2.0f };
                VECTOR3 local_hit, world_hit;
                if (seg_max.x < group->bounds.min.x || seg_min.x > group->bounds.max.x || seg_max.y < group->bounds.min.y ||
                    seg_min.y > group->bounds.max.y || seg_max.z < group->bounds.min.z || seg_min.z > group->bounds.max.z) continue;
                CM_WowTraceWmoBsp(&trace, 0, 0);
                if (trace.best > 1.0f) continue;
                local_hit = Vector3_lerp(&start, &finish, trace.best);
                world_hit = Matrix4_multiply_vector3(&instance->matrix, &local_hit);
                if (world_hit.z <= top + 0.001f && world_hit.z > best) best = world_hit.z;
            }
        } else {
            int x0 = CM_WowWmoCell(seg_min.x, instance->model->bounds.min.x, instance->model->bounds.max.x);
            int x1 = CM_WowWmoCell(seg_max.x, instance->model->bounds.min.x, instance->model->bounds.max.x);
            int z0 = CM_WowWmoCell(seg_min.z, instance->model->bounds.min.z, instance->model->bounds.max.z);
            int z1 = CM_WowWmoCell(seg_max.z, instance->model->bounds.min.z, instance->model->bounds.max.z);
            for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
                DWORD cell = z * CM_WOW_WMO_GRID + x;
                for (DWORD j = instance->model->cell_offsets[cell]; j < instance->model->cell_offsets[cell + 1]; j++) {
                    cmWowWmoTri_t const *tri = instance->model->triangles + instance->model->cell_triangles[j];
                    FLOAT fraction;
                    VECTOR3 local_hit, world_hit;
                    if (seg_max.x < tri->bounds.min.x || seg_min.x > tri->bounds.max.x || seg_max.y < tri->bounds.min.y ||
                        seg_min.y > tri->bounds.max.y || seg_max.z < tri->bounds.min.z || seg_min.z > tri->bounds.max.z ||
                        !CM_WowRayTriangle(&start, &finish, &tri->a, &tri->b, &tri->c, &fraction)) continue;
                    local_hit = Vector3_lerp(&start, &finish, fraction);
                    world_hit = Matrix4_multiply_vector3(&instance->matrix, &local_hit);
                    if (world_hit.z <= top + 0.001f && world_hit.z > best) best = world_hit.z;
                }
            }
        }
    }
    return best;
}

/* Test one player-height ray against authored WMO walls, using BSP or the existing fallback grid. */
static BOOL CM_WowInstanceWallRay(cmWowWmoInstance_t *instance, LPCVECTOR3 world_start, LPCVECTOR3 world_end) {
    VECTOR3 start, finish, seg_min, seg_max;
    if (!instance->model) instance->model = CM_WowGetWmoModel(instance->path);
    if (!instance->model) return false;
    start = Matrix4_multiply_vector3(&instance->inverse, world_start);
    finish = Matrix4_multiply_vector3(&instance->inverse, world_end);
    seg_min = (VECTOR3){ MIN(start.x,finish.x), MIN(start.y,finish.y), MIN(start.z,finish.z) };
    seg_max = (VECTOR3){ MAX(start.x,finish.x), MAX(start.y,finish.y), MAX(start.z,finish.z) };
    if (!instance->model->missing_bsp) {
        FOR_LOOP(i, instance->model->group_count) {
            cmWowWmoGroup_t const *group = instance->model->groups + i;
            cmWowTrace_t trace = { .model = instance->model, .group = group, .instance = instance,
                                   .start = &start, .end = &finish, .best = 2.0f, .walls_only = true };
            if (seg_max.x < group->bounds.min.x || seg_min.x > group->bounds.max.x || seg_max.y < group->bounds.min.y ||
                seg_min.y > group->bounds.max.y || seg_max.z < group->bounds.min.z || seg_min.z > group->bounds.max.z) continue;
            CM_WowTraceWmoBsp(&trace, 0, 0);
            if (trace.best <= 1.0f) return true;
        }
    } else {
        int x0 = CM_WowWmoCell(seg_min.x, instance->model->bounds.min.x, instance->model->bounds.max.x);
        int x1 = CM_WowWmoCell(seg_max.x, instance->model->bounds.min.x, instance->model->bounds.max.x);
        int z0 = CM_WowWmoCell(seg_min.z, instance->model->bounds.min.z, instance->model->bounds.max.z);
        int z1 = CM_WowWmoCell(seg_max.z, instance->model->bounds.min.z, instance->model->bounds.max.z);
        for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
            DWORD cell = z * CM_WOW_WMO_GRID + x;
            for (DWORD j = instance->model->cell_offsets[cell]; j < instance->model->cell_offsets[cell + 1]; j++) {
                cmWowWmoTri_t const *tri = instance->model->triangles + instance->model->cell_triangles[j];
                FLOAT fraction;
                if (seg_max.x < tri->bounds.min.x || seg_min.x > tri->bounds.max.x || seg_max.y < tri->bounds.min.y ||
                    seg_min.y > tri->bounds.max.y || seg_max.z < tri->bounds.min.z || seg_min.z > tri->bounds.max.z ||
                    !CM_WowTriangleIsWall(instance, tri)) continue;
                if (CM_WowRayTriangle(&start, &finish, &tri->a, &tri->b, &tri->c, &fraction)) return true;
            }
        }
    }
    return false;
}

/* Sweep center and cylinder-edge rays at shin/chest height so walls cannot be crossed or edge-clipped. */
BOOL CM_WowMoveBlocked(LPCVECTOR3 from, LPCVECTOR3 to) {
    cmWowAdtHeightCache_t *cache[2] = { NULL, NULL };
    VECTOR2 move = { to->x - from->x, to->y - from->y };
    FLOAT len = sqrtf(move.x * move.x + move.y * move.y);
    int tile_x[2] = { CM_WowAdtIndexForWorldCoord(from->y), CM_WowAdtIndexForWorldCoord(to->y) };
    int tile_y[2] = { CM_WowAdtIndexForWorldCoord(from->x), CM_WowAdtIndexForWorldCoord(to->x) };
    if (len < 0.0001f) return false;
    FOR_LOOP(k, 2) {
        if (k && tile_x[k] == tile_x[0] && tile_y[k] == tile_y[0]) { cache[k] = cache[0]; continue; }
        if (tile_x[k] < 0 || tile_x[k] >= 64 || tile_y[k] < 0 || tile_y[k] >= 64) continue;
        CM_WowLoadAdtHeights(tile_x[k], tile_y[k]);
        FOR_LOOP(i, CM_WOW_HEIGHT_CACHE_TILES)
            if (cm_wow_height_cache[i].loaded && cm_wow_height_cache[i].tile_x == tile_x[k] &&
                cm_wow_height_cache[i].tile_y == tile_y[k]) { cache[k] = cm_wow_height_cache + i; break; }
    }
    move.x = -move.y / len * CM_WOW_PLAYER_RADIUS; move.y = (to->x - from->x) / len * CM_WOW_PLAYER_RADIUS;
    FOR_LOOP(k, 2) {
        if (!cache[k] || (k && cache[k] == cache[0])) continue;
        for (cmWowWmoInstance_t *instance = cache[k]->wmos; instance; instance = instance->next) {
            if (MAX(from->x,to->x) + CM_WOW_PLAYER_RADIUS < instance->bounds.min.x ||
                MIN(from->x,to->x) - CM_WOW_PLAYER_RADIUS > instance->bounds.max.x ||
                MAX(from->y,to->y) + CM_WOW_PLAYER_RADIUS < instance->bounds.min.y ||
                MIN(from->y,to->y) - CM_WOW_PLAYER_RADIUS > instance->bounds.max.y ||
                MAX(from->z,to->z) + CM_WOW_PLAYER_HIGH_Z < instance->bounds.min.z ||
                MIN(from->z,to->z) + CM_WOW_PLAYER_LOW_Z > instance->bounds.max.z) continue;
            FOR_LOOP(side, 3) FOR_LOOP(level, 2) {
                FLOAT off = side == 1 ? 1.0f : side == 2 ? -1.0f : 0.0f;
                FLOAT z = level ? CM_WOW_PLAYER_HIGH_Z : CM_WOW_PLAYER_LOW_Z;
                VECTOR3 a = { from->x + move.x * off, from->y + move.y * off, from->z + z };
                VECTOR3 b = { to->x + move.x * off, to->y + move.y * off, to->z + z };
                if (CM_WowInstanceWallRay(instance, &a, &b)) return true;
            }
        }
    }
    return false;
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
    cmWowAdtHeightCache_t *cache = NULL;
    int tile_x = CM_WowAdtIndexForWorldCoord(sy);
    int tile_y = CM_WowAdtIndexForWorldCoord(sx);

    if (!height || tile_x < 0 || tile_x >= 64 || tile_y < 0 || tile_y >= 64)
        return false;
    FOR_LOOP(i, CM_WOW_HEIGHT_CACHE_TILES)
        if (cm_wow_height_cache[i].loaded && cm_wow_height_cache[i].tile_x == tile_x && cm_wow_height_cache[i].tile_y == tile_y) {
            cache = &cm_wow_height_cache[i];
            break;
        }
    if (!cache)
        CM_WowLoadAdtHeights(tile_x, tile_y);
    FOR_LOOP(i, CM_WOW_HEIGHT_CACHE_TILES)
        if (cm_wow_height_cache[i].loaded && cm_wow_height_cache[i].tile_x == tile_x && cm_wow_height_cache[i].tile_y == tile_y) {
            cache = &cm_wow_height_cache[i];
            break;
        }
    if (!cache || !cache->valid)
        return false;
    cache->use_stamp = ++cm_wow_height_cache_stamp;

    FOR_LOOP(row, 16) {
        FOR_LOOP(col, 16) {
            cmWowChunkHeight_t const *ch = &cache->chunks[row][col];
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
            if (CM_WowHeightInCell(ch->heights, cell_row, cell_col, local_row - cell_row, local_col - cell_col, &cell_height)) {
                *height = ch->position.z + cell_height;
                return true;
            }
        }
    }
    return false;
}

static BOOL CM_WowFindMapId(LPCSTR map_name, DWORD *map_id) {
    stbDbc_t h;
    LPBYTE data;
    DWORD size = 0;
    BYTE const *records_base, *strings_base;

    if (!map_name || !*map_name || !map_id)
        return false;

    data = FS_ReadFile("DBFilesClient\\Map.dbc", &size);
    if (!Stb_DbcValid(data, size, &h)) {
        SAFE_DELETE(data, FS_FreeFile);
        return false;
    }
    records_base = Stb_DbcRecords(data);
    strings_base = Stb_DbcStrings(data, &h);
    FOR_LOOP(record_index, h.records) {
        BYTE const *record = records_base + record_index * h.record_size;
        FOR_LOOP(field_index, h.fields) {
            LPCSTR value = Stb_DbcString(strings_base, h.string_size, Stb_DbcRead32(record + field_index * sizeof(DWORD)));
            if (value && *value && !strcasecmp(value, map_name)) {
                *map_id = Stb_DbcRead32(record);
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
        DWORD string_offset = Stb_DbcRead32(record + field_index * sizeof(DWORD));
        LPCSTR value = Stb_DbcString(strings_base, string_size, string_offset);
        if (value && *value)
            return value;
    }
    return NULL;
}

static DWORD CM_WowCollectWorldSafeLocs(DWORD map_id, LPVECTOR3 first_spawn,
                                        LPSTR first_name, size_t first_name_size) {
    stbDbc_t h;
    LPBYTE data;
    DWORD size = 0;
    BYTE const *records_base, *strings_base;
    DWORD count = 0;

    if (!first_spawn)
        return 0;

    data = FS_ReadFile("DBFilesClient\\WorldSafeLocs.dbc", &size);
    if (!Stb_DbcValid(data, size, &h) ||
        h.fields < 5 || h.record_size < 5 * sizeof(DWORD)) {
        SAFE_DELETE(data, FS_FreeFile);
        return 0;
    }
    records_base = Stb_DbcRecords(data);
    strings_base = Stb_DbcStrings(data, &h);
    FOR_LOOP(record_index, h.records) {
        BYTE const *record = records_base + record_index * h.record_size;
        cmWowWorldSafeLoc_t const *safe_loc = (cmWowWorldSafeLoc_t const *)record;
        LPCSTR name;
        mapPlayer_t *player;

        if (safe_loc->map_id != map_id)
            continue;

        name = CM_WowWorldSafeLocName(record, h.fields, strings_base, h.string_size);
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
            if (Stb_DbcValid(data, size, &h) &&
                h.fields >= 5 && h.record_size >= 5 * sizeof(DWORD)) {
                records_base = Stb_DbcRecords(data);
                strings_base = Stb_DbcStrings(data, &h);
                FOR_LOOP(ri, h.records) {
                    BYTE const *r = records_base + ri * h.record_size;
                    if (Stb_DbcRead32(r + sizeof(DWORD)) != map_id) continue;
                    cm_wow_all_spawns[idx].pos.x = *(FLOAT *)(r + 2 * sizeof(DWORD));
                    cm_wow_all_spawns[idx].pos.y = *(FLOAT *)(r + 3 * sizeof(DWORD));
                    cm_wow_all_spawns[idx].pos.z = *(FLOAT *)(r + 4 * sizeof(DWORD));
                    LPCSTR raw = CM_WowWorldSafeLocName(r, h.fields, strings_base, h.string_size);
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
    cm_wow_map_id = ~0u;
    memset(cm_wow_spawn_heights, 0, sizeof(cm_wow_spawn_heights));
    world.info.players[0].used       = true;
    world.info.players[0].playerType = kPlayerTypeHuman;
    CM_WowSetMapPath(mapFilename);

    if (!CM_WowExtractMapName(mapFilename, map_name, sizeof(map_name))) {
        world.info.players[0].startingPosition = (VECTOR2){ cm_wow_spawn_position.x, cm_wow_spawn_position.y };
        fprintf(stderr, "CM_LoadMap: WoW spawn fallback at %.3f %.3f %.3f (no map name)\n", cm_wow_spawn_position.x, cm_wow_spawn_position.y, cm_wow_spawn_position.z);
        return;
    }

    has_map_id   = CM_WowFindMapId(map_name, &map_id);
    if (has_map_id)
        cm_wow_map_id = map_id;
    if (has_map_id)
        safe_loc_count = CM_WowCollectWorldSafeLocs(map_id, &cm_wow_spawn_position, safe_loc_name, sizeof(safe_loc_name));
    has_safe_locs = safe_loc_count > 0;
    if (!has_safe_locs)
        world.info.players[0].startingPosition = (VECTOR2){ cm_wow_spawn_position.x, cm_wow_spawn_position.y };

    if (has_safe_locs)
        fprintf(stderr, "CM_LoadMap: WoW map %s id=%u loaded %u WorldSafeLocs spawn candidates, first%s%s at %.3f %.3f %.3f\n", map_name, (unsigned)map_id, (unsigned)safe_loc_count, safe_loc_name[0] ? " " : "", safe_loc_name, cm_wow_spawn_position.x, cm_wow_spawn_position.y, cm_wow_spawn_position.z);
    else if (has_map_id)
        fprintf(stderr, "CM_LoadMap: WoW map %s id=%u has no WorldSafeLocs entry, spawn fallback at %.3f %.3f %.3f\n", map_name, (unsigned)map_id, cm_wow_spawn_position.x, cm_wow_spawn_position.y, cm_wow_spawn_position.z);
    else
        fprintf(stderr, "CM_LoadMap: WoW map %s has no Map.dbc entry, spawn fallback at %.3f %.3f %.3f\n", map_name, cm_wow_spawn_position.x, cm_wow_spawn_position.y, cm_wow_spawn_position.z);
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

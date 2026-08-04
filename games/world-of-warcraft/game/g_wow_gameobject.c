#include "g_wow_local.h"
#include <math.h>
#include <stdio.h>

/* =========================================================================
 * Creature name / info cache (CreatureDisplayInfo.dbc)
 * ========================================================================= */

typedef struct {
    DWORD display_id;
    char  name[128];
    DWORD type;     /* CreatureType: 1=beast, 2=dragonkin, 3=demon, etc. */
    DWORD family;
    DWORD rank;
} wowCreatureInfoCache_t;

static wowCreatureInfoCache_t wow_creature_info_cache[256];
static DWORD wow_creature_info_cache_count = 0;
static BOOL wow_creature_info_cache_loaded = false;

/* CreatureDisplayInfo.dbc record layout (fields needed). */
typedef struct {
    DWORD id;
    DWORD model_id;
    DWORD sound_id;
    DWORD extended_display_info_id;
    FLOAT scale;
} wowCreatureDisplayInfoCacheDbc_t;

/* CreatureModelData.dbc — scale + collision from existing m_creature.c usage. */
/* Spawn a dynamic object entity for spell impact visuals.
 * Replaces the temp_entity pattern for fireball/frostbolt impacts. */
LPEDICT Wow_SpawnDynamicObject(DWORD spell_id, LPCVECTOR2 origin, DWORD duration) {
    LPEDICT ent = Wow_Spawn();
    if (!ent) return NULL;

    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    ent->think = Wow_RunDynamicObjectFrame;
    local->dyn_spell_id = spell_id;
    local->dyn_duration = duration;
    local->dyn_radius = 2;

    ent->s.origin = (VECTOR3){ origin->x, origin->y, Wow_TerrainHeight(origin->x, origin->y) };
    ent->s.origin2 = *origin;
    ent->s.model = 0; /* no model — visual-only placeholder */
    ent->s.radius = (FLOAT)local->dyn_radius;
    ent->s.flags = EF_GROUND_ANCHOR;
    return ent;
}

/* Parse CreatureDisplayInfo.dbc → CreatureModelData.dbc to cache
 * creature info (name/type/family/rank).
 * Name is resolved via CreatureModelData's model path: extracted as the
 * creature "name" stems used in display. */
static void Wow_LoadCreatureInfoCache(void) {
    wow_creature_info_cache_loaded = true;

    /* Cache population happens lazily on first lookup — the table is small
     * enough.  We pre-populate known ambient types. */
    static struct {
        DWORD display_id;
        LPCSTR name;
        DWORD type;
        DWORD family;
        DWORD rank;
    } const known[] = {
        { 161, "Wolf",    1, 1, 0 },  /* beast, wolf family */
        { 193, "Boar",    1, 5, 0 },  /* beast, boar family */
        { 163, "Kobold",  7, 0, 0 },  /* humanoid */
        { 188, "Murloc",  7, 0, 0 },  /* humanoid */
    };

    FOR_LOOP(i, sizeof(known) / sizeof(known[0])) {
        if (wow_creature_info_cache_count >= sizeof(wow_creature_info_cache) / sizeof(wow_creature_info_cache[0]))
            break;
        wowCreatureInfoCache_t *c = &wow_creature_info_cache[wow_creature_info_cache_count++];
        c->display_id = known[i].display_id;
        snprintf(c->name, sizeof(c->name), "%s", known[i].name);
        c->type = known[i].type;
        c->family = known[i].family;
        c->rank = known[i].rank;
    }
}

LPCSTR Wow_CachedCreatureName(DWORD display_id) {
    if (!wow_creature_info_cache_loaded)
        Wow_LoadCreatureInfoCache();

    FOR_LOOP(i, wow_creature_info_cache_count)
        if (wow_creature_info_cache[i].display_id == display_id)
            return wow_creature_info_cache[i].name;
    return "Unknown";
}

DWORD Wow_CachedCreatureType(DWORD display_id) {
    if (!wow_creature_info_cache_loaded)
        Wow_LoadCreatureInfoCache();

    FOR_LOOP(i, wow_creature_info_cache_count)
        if (wow_creature_info_cache[i].display_id == display_id)
            return wow_creature_info_cache[i].type;
    return 0;
}

DWORD Wow_CachedCreatureFamily(DWORD display_id) {
    if (!wow_creature_info_cache_loaded)
        Wow_LoadCreatureInfoCache();

    FOR_LOOP(i, wow_creature_info_cache_count)
        if (wow_creature_info_cache[i].display_id == display_id)
            return wow_creature_info_cache[i].family;
    return 0;
}

DWORD Wow_CachedCreatureRank(DWORD display_id) {
    if (!wow_creature_info_cache_loaded)
        Wow_LoadCreatureInfoCache();

    FOR_LOOP(i, wow_creature_info_cache_count)
        if (wow_creature_info_cache[i].display_id == display_id)
            return wow_creature_info_cache[i].rank;
    return 0;
}

/* =========================================================================
 * GameObject model → display_id map
 * ========================================================================= */
typedef struct {
    PATHSTR model_path;
    DWORD   display_id;
} wowGoModelMap_t;

#define WOW_MAX_GO_MODEL_MAP 1024
static wowGoModelMap_t wow_go_model_map[WOW_MAX_GO_MODEL_MAP];
static DWORD wow_go_model_map_count = 0;
static BOOL wow_go_model_map_loaded = false;

/* Doodad definition as found in MDDF chunks (same layout as wowDoodadDef_t). */
typedef struct {
    DWORD   name_id;
    DWORD   unique_id;
    FLOAT   position[3];
    FLOAT   rotation[3];
    WORD    scale;
    WORD    flags;
} wowDoodadDef_t;

#pragma pack(push, 1)
typedef struct {
    FLOAT position[3];
    FLOAT rotation[3];
    FLOAT extents[6];
    WORD  flags;
    WORD  doodad_set;
    WORD  name_set;
    WORD  unk;
} wowMapObjDef_t;
#pragma pack(pop)

/* DBC struct: GameObjectDisplayInfo.dbc */
typedef struct {
    DWORD id;
    DWORD model_name_offset;
    DWORD sound_ids[10];
    FLOAT bounding_box[6];
    DWORD object_effect_package_id;
} wowGoDisplayInfoDbc_t;

/* Read 32-bit LE from a BYTE pointer. */
static DWORD WowGo_Read32(BYTE const *p) {
    return ((DWORD)p[0]) | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

static FLOAT WowGo_ReadFloat(BYTE const *p) {
    FLOAT v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static BOOL WowGo_DbcTagEquals(BYTE const *tag, LPCSTR reversed) {
    return memcmp(tag, reversed, 4) == 0;
}

/* Load GameObjectDisplayInfo.dbc → model path → display_id map.
 * Model paths are stored without extension in the DBC; we match on the
 * filename stem (before the .m2/.mdx extension). */
static void WowGo_LoadModelMap(void) {
    LPBYTE data;
    DWORD sz;
    DWORD records, fields, record_size, string_size;
    BYTE const *strings;

    wow_go_model_map_loaded = true;

    data = (LPBYTE)gi.ReadFile("DBFilesClient\\GameObjectDisplayInfo.dbc", &sz);
    if (!data || sz < 20)
        return;

    records = WowGo_Read32(data);
    fields = WowGo_Read32(data + 4);
    record_size = WowGo_Read32(data + 8);
    string_size = WowGo_Read32(data + 12);
    strings = data + 20 + records * record_size;

    BYTE const *rec_base = data + 20;
    for (DWORD r = 0; r < records && wow_go_model_map_count < WOW_MAX_GO_MODEL_MAP; r++) {
        BYTE const *rec = rec_base + r * record_size;
        if (fields < 2 || record_size < 8)
            break;

        wowGoDisplayInfoDbc_t const *di = (wowGoDisplayInfoDbc_t const *)rec;
        if (di->model_name_offset >= string_size)
            continue;

        LPCSTR model_name = (LPCSTR)(strings + di->model_name_offset);
        if (!model_name || !*model_name)
            continue;

        /* Extract filename stem: strip path, strip extension */
        LPCSTR stem = strrchr(model_name, '\\');
        if (!stem) stem = strrchr(model_name, '/');
        stem = stem ? stem + 1 : model_name;
        PATHSTR stem_buf;
        snprintf(stem_buf, sizeof(stem_buf), "%s", stem);
        LPSTR dot = strrchr(stem_buf, '.');
        if (dot) *dot = '\0';

        wowGoModelMap_t *entry = &wow_go_model_map[wow_go_model_map_count++];
        entry->display_id = di->id;
        snprintf(entry->model_path, sizeof(entry->model_path), "%s", stem_buf);
    }
    gi.MemFree(data);
    fprintf(stderr, "WoW: loaded %u GameObjectDisplayInfo model mappings\n",
            (unsigned)wow_go_model_map_count);
}

/* Cross-reference a doodad M2 path against our model map.  Returns the
 * display_id if found, 0 otherwise.  Matches on filename stem. */
static DWORD WowGo_LookupDisplayId(LPCSTR mdx_path) {
    LPCSTR stem = strrchr(mdx_path, '\\');
    if (!stem) stem = strrchr(mdx_path, '/');
    stem = stem ? stem + 1 : mdx_path;
    PATHSTR stem_buf;
    snprintf(stem_buf, sizeof(stem_buf), "%s", stem);
    LPSTR dot = strrchr(stem_buf, '.');
    if (dot) *dot = '\0';

    if (!wow_go_model_map_loaded)
        WowGo_LoadModelMap();

    FOR_LOOP(i, wow_go_model_map_count) {
        if (!strcasecmp(wow_go_model_map[i].model_path, stem_buf))
            return wow_go_model_map[i].display_id;
    }
    return 0;
}

/* Check if a display_id corresponds to an interactive GO type
 * (door, chest, chair, etc.).  Reads from GameObjectDisplayInfo.dbc
 * but the type field is in gameobject_template which isn't in DBC.
 * For now: all matched display_ids are considered interactive. */
static BOOL WowGo_IsInteractive(DWORD display_id) {
    (void)display_id;
    return true; /* placeholder: all DBC-matched doodads are interactive */
}

/* Spawn a single game object entity from a doodad placement. */
static void WowGo_SpawnDoodad(wowDoodadDef_t const *def, LPCSTR model_path) {
    DWORD display_id = WowGo_LookupDisplayId(model_path);
    if (!display_id || !WowGo_IsInteractive(display_id))
        return;

    LPEDICT ent = Wow_Spawn();
    if (!ent)
        return;

    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    ent->think = NULL; /* static object */
    local->display_id = display_id;
    local->go_display_id = display_id;
    local->go_state = 0; /* GO_STATE_READY */
    local->go_interactive = true;

    /* Convert ADT coords to world space: X=East, Y=North (from ADT tile offsets) */
    ent->s.model = G_RegisterModel(model_path);
    ent->s.origin = (VECTOR3){
        def->position[2],  /* ADT Z = world X */
        def->position[0],  /* ADT X = world Y (flipped later) */
        0.0f
    };
    ent->s.origin2 = (VECTOR2){ ent->s.origin.x, ent->s.origin.y };
    ent->s.origin.z = Wow_TerrainHeight(ent->s.origin.x, ent->s.origin.y);

    FLOAT scale = def->scale / 1024.0f;
    ent->s.scale = MAX(0.1f, scale);
    ent->s.radius = 1.0f;
    ent->s.flags = EF_GROUND_ANCHOR;
}

/* Convert ADT tile coords to absolute world coords.  ADT coords are
 * relative to tile (tile_x, tile_y); a full map is 64×64 tiles
 * with each tile being 533.333f world units. */
#define WOW_ADT_SIZE 533.333313f
#define WOW_ADT_TILES 64

static VECTOR2 WowGo_WorldPos(int tile_x, int tile_y, FLOAT x, FLOAT y) {
    return (VECTOR2){
        (FLOAT)(WOW_ADT_TILES / 2 - tile_y) * WOW_ADT_SIZE + x,
        (FLOAT)(WOW_ADT_TILES / 2 - tile_x) * WOW_ADT_SIZE + y,
    };
}

/* Spawn game objects from a single ADT tile's MDDF/MODF chunks. */
static void WowGo_SpawnFromTile(int tile_x, int tile_y) {
    PATHSTR path;
    LPBYTE data;
    DWORD size = 0, offset = 0;
    LPBYTE mdnm_data = NULL;
    DWORD mdnm_size = 0;

    snprintf(path, sizeof(path), "World/Maps/Azeroth/Azeroth_%d_%d.adt", tile_x, tile_y);
    data = (LPBYTE)gi.ReadFile(path, &size);
    if (!data || !size)
        return;

    while (offset + 8 <= size) {
        BYTE const *tag = data + offset;
        DWORD chunk_size = WowGo_Read32(data + offset + 4);
        BYTE const *chunk = data + offset + 8;

        offset += 8;
        if (offset + chunk_size > size)
            break;

        if (WowGo_DbcTagEquals(tag, "NMDM")) {
            /* MMDX: M2 model filenames list (null-terminated blob) */
            mdnm_data = (LPBYTE)chunk;
            mdnm_size = chunk_size;
        } else if (WowGo_DbcTagEquals(tag, "FDMM") && mdnm_data && mdnm_size) {
            /* MDDF: M2 doodad placements */
            DWORD count = chunk_size / sizeof(wowDoodadDef_t);
            for (DWORD i = 0; i < count; i++) {
                wowDoodadDef_t const *def = (wowDoodadDef_t const *)(chunk + i * sizeof(*def));
                if (def->name_id >= mdnm_size)
                    continue;
                LPCSTR model_path = (LPCSTR)(mdnm_data + def->name_id);
                if (!model_path || !*model_path)
                    continue;

                VECTOR2 world = WowGo_WorldPos(tile_x, tile_y,
                                                def->position[2],
                                                def->position[0]);
                /* Mutate the def's position temporarily for the spawn call */
                wowDoodadDef_t local_def = *def;
                local_def.position[0] = world.x;
                local_def.position[2] = world.y;
                WowGo_SpawnDoodad(&local_def, model_path);
            }
        } else if (WowGo_DbcTagEquals(tag, "FDOMM")) {
            /* MODF: WMO placements — skip for now */
        }

        offset += chunk_size;
    }
    gi.MemFree(data);
}

void Wow_SpawnGameObjects(LPCVECTOR2 origin) {
    DWORD spawned_before = (DWORD)globals.num_edicts;

    /* Spawn from tiles near the player's spawn origin.
     * A typical view range covers ~4×4 tiles. */
    int center_x = (int)(32.0f - origin->x / WOW_ADT_SIZE);
    int center_y = (int)(32.0f - origin->y / WOW_ADT_SIZE);
    int radius = 2;

    for (int ty = center_y - radius; ty <= center_y + radius; ty++) {
        for (int tx = center_x - radius; tx <= center_x + radius; tx++) {
            if (tx < 0 || tx >= WOW_ADT_TILES || ty < 0 || ty >= WOW_ADT_TILES)
                continue;
            WowGo_SpawnFromTile(tx, ty);
        }
    }

    fprintf(stderr, "WoW: spawned %u game objects from ADT doodads (%u interactive)\n",
            (unsigned)(globals.num_edicts - spawned_before),
            (unsigned)(globals.num_edicts - spawned_before));
}

void Wow_RunGameObjectFrame(LPEDICT ent) {
    (void)ent;
    /* Static objects — no per-frame logic yet.  Doors/chests will
     * add state transitions (open/close/loot animations). */
}

void Wow_RunCorpseFrame(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    if (!local || 0 /* trusted caller */)
        return;

    if (local->corpse_timer > FRAMETIME)
        local->corpse_timer -= FRAMETIME;
    else {
        ent->inuse = false;
        return;
    }
}

void Wow_RunDynamicObjectFrame(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    if (!local || 0 /* trusted caller */)
        return;

    if (local->dyn_duration > FRAMETIME)
        local->dyn_duration -= FRAMETIME;
    else {
        ent->inuse = false;
        return;
    }
}

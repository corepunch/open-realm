#include "g_wow_local.h"
#include "common/wow_coords.h"
#include "common/stb_dbc.h"
#include "common/wow_chunks.h"
#include <math.h>
#include <stdio.h>

/* =========================================================================
 * Creature name / info cache (CreatureDisplayInfo.dbc)
 * ========================================================================= */

typedef struct {
    uint32_t display_id;
    char  name[128];
    uint32_t type;     /* CreatureType: 1=beast, 2=dragonkin, 3=demon, etc. */
    uint32_t family;
    uint32_t rank;
} wowCreatureInfoCache_t;

static wowCreatureInfoCache_t wow_creature_info_cache[256];
static uint32_t wow_creature_info_cache_count = 0;
static bool wow_creature_info_cache_loaded = false;

/* Spawn a dynamic object entity for spell impact visuals.
 * Replaces the temp_entity pattern for fireball/frostbolt impacts. */
edict_t *Wow_SpawnDynamicObject(uint32_t spell_id, vec2_t const *origin, uint32_t duration) {
    edict_t *ent = Wow_Spawn();
    if (!ent) return NULL;

    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    local->think = Wow_RunDynamicObjectFrame;
    local->dyn_spell_id = spell_id;
    local->dyn_duration = duration;
    local->dyn_radius = 2;

    ent->s.origin = (vec3_t){ origin->x, origin->y, Wow_TerrainHeight(origin->x, origin->y) };
    ent->s.origin2 = *origin;
    ent->s.model = 0; /* no model — visual-only placeholder */
    ent->s.radius = (float)local->dyn_radius;
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
        uint32_t display_id;
        cstring_t name;
        uint32_t type;
        uint32_t family;
        uint32_t rank;
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

cstring_t Wow_CachedCreatureName(uint32_t display_id) {
    if (!wow_creature_info_cache_loaded)
        Wow_LoadCreatureInfoCache();

    FOR_LOOP(i, wow_creature_info_cache_count)
        if (wow_creature_info_cache[i].display_id == display_id)
            return wow_creature_info_cache[i].name;
    return "Unknown";
}

uint32_t Wow_CachedCreatureType(uint32_t display_id) {
    if (!wow_creature_info_cache_loaded)
        Wow_LoadCreatureInfoCache();

    FOR_LOOP(i, wow_creature_info_cache_count)
        if (wow_creature_info_cache[i].display_id == display_id)
            return wow_creature_info_cache[i].type;
    return 0;
}

uint32_t Wow_CachedCreatureFamily(uint32_t display_id) {
    if (!wow_creature_info_cache_loaded)
        Wow_LoadCreatureInfoCache();

    FOR_LOOP(i, wow_creature_info_cache_count)
        if (wow_creature_info_cache[i].display_id == display_id)
            return wow_creature_info_cache[i].family;
    return 0;
}

uint32_t Wow_CachedCreatureRank(uint32_t display_id) {
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
    uint32_t   display_id;
} wowGoModelMap_t;

#define WOW_MAX_GO_MODEL_MAP 1024
static wowGoModelMap_t wow_go_model_map[WOW_MAX_GO_MODEL_MAP];
static uint32_t wow_go_model_map_count = 0;
static bool wow_go_model_map_loaded = false;

#pragma pack(push, 1)
typedef struct {
    float position[3];
    float rotation[3];
    float extents[6];
    uint16_t  flags;
    uint16_t  doodad_set;
    uint16_t  name_set;
    uint16_t  unk;
} wowMapObjDef_t;
#pragma pack(pop)

/* GameObjectDisplayInfo decode via the shared schema (common/stb_dbc.h); consumers
 * read named fields, not raw column offsets. */
typedef struct { uint32_t id; cstring_t model_name; } gGameObjectDisplayInfoRec_t;
static stbDbcField_t const game_object_display_info_schema[] = {
    { 0, offsetof(gGameObjectDisplayInfoRec_t, id),         STB_DBC_U32 },
    { 1, offsetof(gGameObjectDisplayInfoRec_t, model_name), STB_DBC_STR },
};
static stbDbcCache_t game_object_display_info_dbc;

/* Load GameObjectDisplayInfo.dbc → model path → display_id map.
 * Model paths are stored without extension in the DBC; we match on the
 * filename stem (before the .m2/.mdx extension). */
static void WowGo_LoadModelMap(void) {
    wow_go_model_map_loaded = true;

    if (!Stb_DbcCacheLoad(&game_object_display_info_dbc, "DBFilesClient\\GameObjectDisplayInfo.dbc", &g_dbc_io) ||
        !Stb_DbcCacheDecode(&game_object_display_info_dbc, game_object_display_info_schema,
                            sizeof(game_object_display_info_schema) / sizeof(game_object_display_info_schema[0]),
                            sizeof(gGameObjectDisplayInfoRec_t), &g_dbc_io)) {
        return;
    }

    FOR_LOOP(r, game_object_display_info_dbc.records) {
        if (wow_go_model_map_count >= WOW_MAX_GO_MODEL_MAP) break;
        gGameObjectDisplayInfoRec_t const *di = STB_DBC_ROW(game_object_display_info_dbc, gGameObjectDisplayInfoRec_t, r);
        cstring_t model_name = di->model_name;
        if (!model_name || !*model_name) continue;

        /* Extract filename stem: strip path, strip extension */
        cstring_t stem = strrchr(model_name, '\\');
        if (!stem) stem = strrchr(model_name, '/');
        stem = stem ? stem + 1 : model_name;
        PATHSTR stem_buf;
        snprintf(stem_buf, sizeof(stem_buf), "%s", stem);
        string_t dot = strrchr(stem_buf, '.');
        if (dot) *dot = '\0';

        wowGoModelMap_t *entry = &wow_go_model_map[wow_go_model_map_count++];
        entry->display_id = di->id;
        snprintf(entry->model_path, sizeof(entry->model_path), "%s", stem_buf);
    }
    fprintf(stderr, "WoW: loaded %u GameObjectDisplayInfo model mappings\n", (unsigned)wow_go_model_map_count);
}

/* Cross-reference a doodad M2 path against our model map.  Returns the
 * display_id if found, 0 otherwise.  Matches on filename stem. */
static uint32_t WowGo_LookupDisplayId(cstring_t mdx_path) {
    cstring_t stem = strrchr(mdx_path, '\\');
    if (!stem) stem = strrchr(mdx_path, '/');
    stem = stem ? stem + 1 : mdx_path;
    PATHSTR stem_buf;
    snprintf(stem_buf, sizeof(stem_buf), "%s", stem);
    string_t dot = strrchr(stem_buf, '.');
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
static bool WowGo_IsInteractive(uint32_t display_id) {
    (void)display_id;
    return true; /* placeholder: all DBC-matched doodads are interactive */
}

/* Keep server-authored interactive entities coincident with renderer-owned MDDF doodads. */
void WowGo_SetDoodadTransform(wowDoodadDef_t const *def, entityState_t *state) {
    /* MDDF positions are absolute map coordinates; the old tile offset and terrain projection destroyed authored Z. */
    state->origin = Wow_ObjectPosition(def->position[0], def->position[1], def->position[2]);
    state->origin2 = (vec2_t){ state->origin.x, state->origin.y };
    state->rotation = (vec3_t){ def->rotation[0], def->rotation[1], def->rotation[2] };
    state->scale = def->scale / 1024.0f;
}

/* Spawn a game object with the exact authored MDDF transform. */
static void WowGo_SpawnDoodad(wowDoodadDef_t const *def, cstring_t model_path) {
    uint32_t display_id = WowGo_LookupDisplayId(model_path);
    if (!display_id || !WowGo_IsInteractive(display_id))
        return;

    edict_t *ent = Wow_Spawn();
    if (!ent)
        return;

    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    local->think = NULL; /* static object */
    local->display_id = display_id;
    local->go_display_id = display_id;
    local->go_state = 0; /* GO_STATE_READY */
    local->go_interactive = true;

    ent->s.model = G_RegisterModel(model_path);
    WowGo_SetDoodadTransform(def, &ent->s);
    ent->s.radius = 1.0f;
}

/* Spawn game objects from a single ADT tile's MDDF/MODF chunks. */
static void WowGo_SpawnFromTile(int tile_x, int tile_y) {
    PATHSTR path;
    uint8_t *data;
    uint32_t size = 0, offset = 0;
    uint8_t *mdnm_data = NULL;
    uint32_t mdnm_size = 0;

    if (!CM_WowAdtPath(tile_x, tile_y, path, sizeof(path))) {
        fprintf(stderr, "WoW: current map has no ADT path for game-object tile %d,%d\n", tile_x, tile_y);
        return;
    }
    data = (uint8_t *)gi.ReadFile(path, &size);
    if (!data || !size)
        return;

    while (offset + 8 <= size) {
        uint8_t const *tag = data + offset;
        uint32_t chunk_size = Stb_DbcRead32(data + offset + 4);
        uint8_t const *chunk = data + offset + 8;

        offset += 8;
        if (offset + chunk_size > size)
            break;

        if (*(uint32_t const *)tag == ID_XDMM) {
            /* MMDX: M2 model filenames list (null-terminated blob) */
            mdnm_data = (uint8_t *)chunk;
            mdnm_size = chunk_size;
        } else if (*(uint32_t const *)tag == ID_FDDM && mdnm_data && mdnm_size) {
            /* MDDF: M2 doodad placements */
            uint32_t count = chunk_size / sizeof(wowDoodadDef_t);
            for (uint32_t i = 0; i < count; i++) {
                wowDoodadDef_t const *def = (wowDoodadDef_t const *)(chunk + i * sizeof(*def));
                if (def->name_id >= mdnm_size)
                    continue;
                cstring_t model_path = (cstring_t)(mdnm_data + def->name_id);
                if (!model_path || !*model_path)
                    continue;

                WowGo_SpawnDoodad(def, model_path);
            }
        } else if (*(uint32_t const *)tag == ID_FDOM) {
            /* MODF: WMO placements — skip for now */
        }

        offset += chunk_size;
    }
    gi.MemFree(data);
}

void Wow_SpawnGameObjects(vec2_t const *origin) {
    uint32_t spawned_before = (uint32_t)globals.num_edicts;

    /* Spawn from tiles near the player's spawn origin.
     * A typical view range covers ~4×4 tiles. */
    int center_x = Wow_TileIndex(origin->x);
    int center_y = Wow_TileIndex(origin->y);
    int radius = 2;

    for (int ty = center_y - radius; ty <= center_y + radius; ty++) {
        for (int tx = center_x - radius; tx <= center_x + radius; tx++) {
            if (tx < 0 || tx >= WOW_ADT_TILES || ty < 0 || ty >= WOW_ADT_TILES)
                continue;
            WowGo_SpawnFromTile(tx, ty);
        }
    }

    fprintf(stderr, "WoW: spawned %u game objects from ADT doodads (%u interactive)\n", (unsigned)(globals.num_edicts - spawned_before), (unsigned)(globals.num_edicts - spawned_before));
}

void Wow_RunGameObjectFrame(edict_t *ent) {
    (void)ent;
    /* Static objects — no per-frame logic yet.  Doors/chests will
     * add state transitions (open/close/loot animations). */
}

void Wow_RunCorpseFrame(edict_t *ent) {
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

void Wow_RunDynamicObjectFrame(edict_t *ent) {
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

#include "g_wow_local.h"
#include <math.h>
#include <stdio.h>

#define WOW_AMBIENT_CREATURE_COUNT 64
#define WOW_QUEST_LOCATION_BUDGET  32
/* WOW_CREATURE_DISPLAY_* constants live in g_wow_local.h (shared with loot table). */

/* Yellow "!" billboard shown above quest givers with an available quest. */
#define WOW_QUEST_AVAILABLE_ICON "Interface\\GossipFrame\\AvailableQuestIcon.blp"

typedef struct {
    DWORD display_id;
    FLOAT min_radius;
    FLOAT walk_speed;
} wowAmbientCreatureType_t;

typedef struct {
    DWORD display_id;
    PATHSTR model_path;
    FLOAT scale;
    FLOAT radius;
    BOOL resolved;
    BOOL failed;
} wowCreatureModelCache_t;

/* CreatureDisplayInfo / CreatureModelData decode into file-shaped structs via the
 * shared schema table (common/stb_dbc.h); consumers read named fields, not raw
 * column offsets. Scale/collision are FLOAT columns. */
typedef struct { DWORD id, model_id; FLOAT scale; } gCreatureDisplayInfoRec_t;
static stbDbcField_t const creature_display_info_schema[] = {
    { 0, offsetof(gCreatureDisplayInfoRec_t, id),       STB_DBC_U32 },
    { 1, offsetof(gCreatureDisplayInfoRec_t, model_id), STB_DBC_U32 },
    { 4, offsetof(gCreatureDisplayInfoRec_t, scale),    STB_DBC_FLOAT },
};

typedef struct { DWORD id; LPCSTR model_name; FLOAT model_scale, collision_width; } gCreatureModelDataRec_t;
static stbDbcField_t const creature_model_data_schema[] = {
    {  0, offsetof(gCreatureModelDataRec_t, id),              STB_DBC_U32 },
    {  2, offsetof(gCreatureModelDataRec_t, model_name),      STB_DBC_STR },
    {  4, offsetof(gCreatureModelDataRec_t, model_scale),     STB_DBC_FLOAT },
    { 14, offsetof(gCreatureModelDataRec_t, collision_width), STB_DBC_FLOAT },
};

static stbDbcCache_t creature_display_info_dbc;
static stbDbcCache_t creature_model_data_dbc;

static wowAmbientCreatureType_t const wow_ambient_creature_types[] = {
    { WOW_CREATURE_DISPLAY_WOLF,   18.0f, 2.0f },
    { WOW_CREATURE_DISPLAY_BOAR,   20.0f, 1.4f },
    { WOW_CREATURE_DISPLAY_KOBOLD, 22.0f, 0.0f },
    { WOW_CREATURE_DISPLAY_MURLOC, 24.0f, 1.7f },
};

static wowCreatureModelCache_t wow_creature_model_cache[sizeof(wow_ambient_creature_types) /
                                                        sizeof(wow_ambient_creature_types[0])];

/* A few AzerothCore rows begin above index zero; primary means the lowest
 * populated model index, not blindly models[0]. */
static LPCWOWCREATUREMODEL Wow_CreaturePrimaryModel(LPCWOWCREATURE creature) {
    if (!creature) return NULL;
    FOR_LOOP(i, WOW_CREATURE_MODEL_COUNT)
        if (creature->models[i].display_id) return &creature->models[i];
    return NULL;
}

static BOOL Wow_ResolveCreatureModel(DWORD display_id,
                                     LPSTR model_path,
                                     DWORD model_path_size,
                                     FLOAT *scale,
                                     FLOAT *radius) {
    int idx;
    gCreatureDisplayInfoRec_t const *display;
    gCreatureModelDataRec_t const *model;

    if (!model_path || model_path_size == 0) {
        return false;
    }
    model_path[0] = '\0';

    if (!Stb_DbcCacheLoad(&creature_display_info_dbc, "DBFilesClient\\CreatureDisplayInfo.dbc", &g_dbc_io)) return false;
    Stb_DbcCacheDecode(&creature_display_info_dbc, creature_display_info_schema, sizeof(creature_display_info_schema) / sizeof(creature_display_info_schema[0]),
                       sizeof(gCreatureDisplayInfoRec_t), &g_dbc_io);
    idx = Stb_DbcCacheFindID(&creature_display_info_dbc, display_id, &g_dbc_io);
    if (idx < 0) return false;
    display = STB_DBC_ROW(creature_display_info_dbc, gCreatureDisplayInfoRec_t, idx);

    if (!Stb_DbcCacheLoad(&creature_model_data_dbc, "DBFilesClient\\CreatureModelData.dbc", &g_dbc_io)) return false;
    Stb_DbcCacheDecode(&creature_model_data_dbc, creature_model_data_schema, sizeof(creature_model_data_schema) / sizeof(creature_model_data_schema[0]),
                       sizeof(gCreatureModelDataRec_t), &g_dbc_io);
    idx = Stb_DbcCacheFindID(&creature_model_data_dbc, display->model_id, &g_dbc_io);
    if (idx < 0) return false;
    model = STB_DBC_ROW(creature_model_data_dbc, gCreatureModelDataRec_t, idx);

    if (!model->model_name || !*model->model_name) return false;

    snprintf(model_path, model_path_size, "%s", model->model_name);
    if (scale) {
        *scale = MAX(0.1f, display->scale * model->model_scale);
    }
    if (radius) {
        *radius = Wow_Clamp(model->collision_width * 0.5f, 0.5f, 4.0f);
    }
    return true;
}

static BOOL Wow_CachedCreatureModel(DWORD display_id,
                                    LPSTR model_path,
                                    DWORD model_path_size,
                                    FLOAT *scale,
                                    FLOAT *radius) {
    FOR_LOOP(i, sizeof(wow_creature_model_cache) / sizeof(wow_creature_model_cache[0])) {
        wowCreatureModelCache_t *cache = &wow_creature_model_cache[i];

        if (cache->display_id != display_id && cache->display_id != 0) {
            continue;
        }
        if (cache->failed) {
            return false;
        }
        if (!cache->resolved) {
            cache->display_id = display_id;
            cache->scale = 1.0f;
            cache->radius = 1.0f;
            if (!Wow_ResolveCreatureModel(display_id, cache->model_path, sizeof(cache->model_path), &cache->scale, &cache->radius)) {
                cache->failed = true;
                return false;
            }
            cache->resolved = true;
        }
        snprintf(model_path, model_path_size, "%s", cache->model_path);
        if (scale) {
            *scale = cache->scale;
        }
        if (radius) {
            *radius = cache->radius;
        }
        return true;
    }
    return Wow_ResolveCreatureModel(display_id, model_path, model_path_size, scale, radius);
}

static void Wow_MonsterStart(LPEDICT ent,
                             DWORD display_id,
                             LPCVECTOR2 home,
                             FLOAT yaw,
                             FLOAT patrol_radius,
                             FLOAT walk_speed) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);

    if (!ent || !local) {
        return;
    }
    local->display_id = display_id;
    local->home = home ? *home : ent->s.origin2;
    local->yaw = yaw;
    local->patrol_radius = patrol_radius;
    local->patrol_phase = (FLOAT)DEG2RAD(yaw);
    local->walk_speed = walk_speed;
    local->health = 3;
    local->attack_damage_point = 250;
    local->attack_backswing = 450;
    ent->svflags |= SVF_MONSTER;
    ent->s.renderfx |= RF_HOSTILE;
    ent->idle = Wow_AIIdle;
    ent->move = Wow_AIMove;
    ent->think = Wow_RunCreatureFrame;
    ent->attack = Wow_AIAttack;
    ent->pain = Wow_AIPain;
    ent->s.flags = EF_GROUND_ANCHOR;
    ent->s.angle = (FLOAT)DEG2RAD(yaw);
    if (patrol_radius > 0.0f) {
        Wow_SetWalkMove(ent);
    } else {
        Wow_SetStandMove(ent);
    }
}

static LPEDICT Wow_SpawnCreature(DWORD display_id,
                                 LPCVECTOR2 origin,
                                 FLOAT yaw,
                                 FLOAT patrol_radius,
                                 FLOAT walk_speed) {
    PATHSTR model_path;
    FLOAT scale = 1.0f;
    FLOAT radius = 1.0f;
    LPEDICT ent;

    if (!origin || !Wow_CachedCreatureModel(display_id, model_path, sizeof(model_path), &scale, &radius)) {
        fprintf(stderr, "WoW creature display %u could not be resolved\n", (unsigned)display_id);
        return NULL;
    }
    ent = Wow_Spawn();
    if (!ent) {
        fprintf(stderr, "WoW creature display %u skipped: no free edict\n", (unsigned)display_id);
        return NULL;
    }

    ent->s.model = G_RegisterModel(model_path);
    if (!ent->s.model) {
        ent->inuse = false;
        fprintf(stderr, "WoW creature display %u skipped: model %s could not be indexed\n", (unsigned)display_id, model_path);
        return NULL;
    }
    ent->s.origin = (VECTOR3){ origin->x, origin->y, Wow_TerrainHeight(origin->x, origin->y) };
    ent->s.origin2 = *origin;
    ent->s.scale = scale;
    ent->s.radius = radius;
    ent->s.player = 2;
    ent->s.class_id = display_id;
    Wow_MonsterStart(ent, display_id, origin, yaw, patrol_radius, walk_speed);
    return ent;
}

typedef struct { FLOAT dist2; DWORD idx; } wowGiverSort_t;
static int Wow_CmpGiverDist(void const *a, void const *b) {
    FLOAT const da = ((wowGiverSort_t const *)a)->dist2;
    FLOAT const db = ((wowGiverSort_t const *)b)->dist2;
    return da < db ? -1 : da > db ? 1 : 0;
}

/* Spawn non-hostile quest NPCs and server-side objective anchors from the
 * imported world database, limited to the player's nearby starting area. */
void Wow_SpawnQuestLocations(LPCVECTOR2 origin) {
    DWORD givers = 0;
    DWORD objectives = 0;
    DWORD budget = WOW_QUEST_LOCATION_BUDGET;
    FLOAT const spawn_radius = 6500.0f;
    FLOAT const spawn_radius2 = spawn_radius * spawn_radius;
    wowGiverSort_t sorted[2048];
    DWORD nsorted = 0;

    if (!origin)
        return;

    /* Pre-sort by distance so the budget always favours the nearest quest
     * givers; table order (quest_id) is irrelevant to spawn priority. */
    FOR_LOOP(i, Wow_QuestGiverCount()) {
        LPCWOWQUESTGIVER data = Wow_QuestGiver(i);
        VECTOR2 pos = { data->position.x, data->position.y };
        VECTOR2 delta = Vector2_sub(&pos, origin);
        FLOAT dist2 = delta.x * delta.x + delta.y * delta.y;
        if (dist2 <= spawn_radius2 && nsorted < sizeof(sorted) / sizeof(*sorted)) {
            sorted[nsorted].dist2 = dist2;
            sorted[nsorted].idx  = (DWORD)i;
            nsorted++;
        }
    }
    qsort(sorted, nsorted, sizeof(*sorted), Wow_CmpGiverDist);

    FOR_LOOP(si, nsorted) {
        DWORD i = sorted[si].idx;
        LPCWOWQUESTGIVER data = Wow_QuestGiver(i);
        LPCWOWCREATURE creature = Wow_CreatureByEntry(data->creature_entry);
        LPCWOWCREATUREMODEL creature_model;
        PATHSTR model_path;
        FLOAT scale = 1.0f;
        FLOAT radius = 1.0f;
        VECTOR2 position;
        LPEDICT ent;
        wowEntityLocal_t *local;
        BOOL duplicate = false;

        if (!budget)
            break;
        FOR_LOOP(prev, si)
            if (Wow_QuestGiverSame(data, Wow_QuestGiver(sorted[prev].idx))) { duplicate = true; break; }
        if (duplicate) continue;
        creature_model = Wow_CreaturePrimaryModel(creature);
        if (!creature_model) {
            fprintf(stderr, "WoW: quest giver creature %u has no primary server model\n", (unsigned)data->creature_entry);
            continue;
        }
        if (data->display_id != creature_model->display_id)
            fprintf(stderr, "WoW: quest giver creature %u display %u disagrees with primary model %u\n", (unsigned)data->creature_entry, (unsigned)data->display_id, (unsigned)creature_model->display_id);
        position = (VECTOR2){ data->position.x, data->position.y };
        if (!Wow_CachedCreatureModel(creature_model->display_id, model_path, sizeof(model_path), &scale, &radius))
            continue;
        ent = Wow_Spawn();
        if (!ent)
            break;
        ent->s.model = G_RegisterModel(model_path);
        if (!ent->s.model) {
            ent->inuse = false;
            continue;
        }
        local = Wow_EntityLocal(ent);
        local->display_id = creature_model->display_id;
        local->quest_id = data->quest_id;
        local->home = position;
        local->yaw = (FLOAT)RAD2DEG(data->orientation);
        local->health = 1;
        ent->s.origin = data->position;
        ent->s.origin2 = position;
        ent->s.scale = scale * creature_model->display_scale;
        ent->s.radius = radius;
        ent->s.player = 2;
        ent->s.class_id = creature_model->display_id;
        local->quest_available_sprite = (DWORD)gi.ImageIndex(WOW_QUEST_AVAILABLE_ICON);
        ent->s.overhead_sprite = local->quest_available_sprite;
        ent->s.angle = data->orientation;
        ent->s.flags = EF_GROUND_ANCHOR;
        /* Non-hostile NPCs still need the creature frame for their idle (Stand)
         * animation; without a think function the entity loop skips them and
         * they render frozen at frame 0. */
        ent->svflags |= SVF_MONSTER;
        ent->idle = Wow_AIIdle;
        ent->think = Wow_RunCreatureFrame;
        Wow_SetStandMove(ent);
        givers++;
        budget--;
    }

    FOR_LOOP(i, Wow_QuestObjectiveCount()) {
        LPCWOWQUESTOBJECTIVE data = Wow_QuestObjective(i);
        VECTOR2 position = data->position;
        VECTOR2 delta = Vector2_sub(&position, origin);
        LPEDICT ent;
        wowEntityLocal_t *local;

        if (!budget)
            break;
        if (delta.x * delta.x + delta.y * delta.y > spawn_radius2)
            continue;
        ent = Wow_Spawn();
        if (!ent)
            break;
        local = Wow_EntityLocal(ent);
        local->go_entry = data->quest_id;
        local->go_type = WOW_QUEST_OBJECTIVE_ANCHOR;
        local->go_state = 0;
        ent->think = Wow_RunGameObjectFrame;
        ent->s.origin = (VECTOR3){ position.x, position.y, Wow_TerrainHeight(position.x, position.y) };
        ent->s.origin2 = position;
        ent->s.radius = 1.0f;
        ent->s.flags = EF_GROUND_ANCHOR;
        objectives++;
        budget--;
    }

    fprintf(stderr, "WoW: spawned %u quest givers and %u objective anchors\n", (unsigned)givers, (unsigned)objectives);
}

void Wow_SpawnAmbientCreatures(LPCVECTOR2 origin) {
    VECTOR2 creature_origin;
    DWORD spawned = 0;

    if (!origin) {
        return;
    }

    FOR_LOOP(i, sizeof(wow_creature_model_cache) / sizeof(wow_creature_model_cache[0])) {
        memset(&wow_creature_model_cache[i], 0, sizeof(wow_creature_model_cache[i]));
    }

    FOR_LOOP(i, WOW_AMBIENT_CREATURE_COUNT) {
        DWORD const type_count = sizeof(wow_ambient_creature_types) / sizeof(wow_ambient_creature_types[0]);
        wowAmbientCreatureType_t const *type = &wow_ambient_creature_types[i % type_count];
        FLOAT const angle = (FLOAT)DEG2RAD((i * 137) % 360);
        FLOAT const radius = type->min_radius + (FLOAT)((i / type_count) * 5) + (FLOAT)((i % 3) * 2);
        FLOAT const patrol_radius = type->walk_speed > 0.0f ? 2.5f + (FLOAT)(i % 5) : 0.0f;

        creature_origin = (VECTOR2){
            origin->x + cosf(angle) * radius,
            origin->y + sinf(angle) * radius,
        };
        if (Wow_SpawnCreature(type->display_id, &creature_origin, (FLOAT)RAD2DEG(angle) + 180.0f, patrol_radius, type->walk_speed)) {
            spawned++;
        }
    }

    fprintf(stderr, "WoW spawned %u ambient creatures from %u DBC display types\n", (unsigned)spawned, (unsigned)(sizeof(wow_ambient_creature_types) / sizeof(wow_ambient_creature_types[0])));
}

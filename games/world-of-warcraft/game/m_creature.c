#include "g_wow_local.h"
#include <math.h>
#include <stdio.h>

#define WOW_AMBIENT_CREATURE_COUNT 64
#define WOW_CREATURE_DISPLAY_WOLF 161
#define WOW_CREATURE_DISPLAY_BOAR 193
#define WOW_CREATURE_DISPLAY_KOBOLD 163
#define WOW_CREATURE_DISPLAY_MURLOC 188

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

typedef struct {
    DWORD id;
    DWORD model_id;
    DWORD sound_id;
    DWORD extended_display_info_id;
    FLOAT scale;
} wowCreatureDisplayInfoDbc_t;

typedef struct {
    DWORD id;
    DWORD flags;
    DWORD model_name_offset;
    DWORD size_class;
    FLOAT model_scale;
    DWORD unused[9];
    FLOAT collision_width;
} wowCreatureModelDataDbc_t;

static wowAmbientCreatureType_t const wow_ambient_creature_types[] = {
    { WOW_CREATURE_DISPLAY_WOLF,   18.0f, 2.0f },
    { WOW_CREATURE_DISPLAY_BOAR,   20.0f, 1.4f },
    { WOW_CREATURE_DISPLAY_KOBOLD, 22.0f, 0.0f },
    { WOW_CREATURE_DISPLAY_MURLOC, 24.0f, 1.7f },
};

static wowCreatureModelCache_t wow_creature_model_cache[sizeof(wow_ambient_creature_types) /
                                                        sizeof(wow_ambient_creature_types[0])];

static BOOL Wow_ResolveCreatureModel(DWORD display_id,
                                     LPSTR model_path,
                                     DWORD model_path_size,
                                     FLOAT *scale,
                                     FLOAT *radius) {
    LPBYTE display_data = NULL;
    LPBYTE model_data = NULL;
    DWORD fields;
    DWORD record_size;
    DWORD string_size;
    BYTE const *record;
    BYTE const *strings;
    DWORD model_id;
    DWORD model_name_offset;
    LPCSTR resolved_model;
    FLOAT display_scale = 1.0f;
    FLOAT model_scale = 1.0f;
    FLOAT collision_width = 1.0f;

    if (!model_path || model_path_size == 0) {
        return false;
    }
    model_path[0] = '\0';

    if (!Wow_FindDbcRecord("DBFilesClient\\CreatureDisplayInfo.dbc",
                           display_id,
                           &display_data,
                           &fields,
                           &record_size,
                           &record,
                           &strings,
                           &string_size) ||
        fields < 2) {
        SAFE_DELETE(display_data, gi.MemFree);
        return false;
    }

    wowCreatureDisplayInfoDbc_t const *display = (wowCreatureDisplayInfoDbc_t const *)record;

    model_id = display->model_id;
    if (record_size >= sizeof(*display)) {
        display_scale = display->scale;
    }
    gi.MemFree(display_data);

    if (!Wow_FindDbcRecord("DBFilesClient\\CreatureModelData.dbc",
                           model_id,
                           &model_data,
                           &fields,
                           &record_size,
                           &record,
                           &strings,
                           &string_size) ||
        fields < 3) {
        SAFE_DELETE(model_data, gi.MemFree);
        return false;
    }

    wowCreatureModelDataDbc_t const *model = (wowCreatureModelDataDbc_t const *)record;

    model_name_offset = model->model_name_offset;
    resolved_model = Wow_DbcString(strings, string_size, model_name_offset);
    if (!resolved_model || !*resolved_model) {
        gi.MemFree(model_data);
        return false;
    }
    if (record_size >= 5 * sizeof(DWORD)) {
        model_scale = model->model_scale;
    }
    if (record_size >= sizeof(*model)) {
        collision_width = model->collision_width;
    }

    snprintf(model_path, model_path_size, "%s", resolved_model);
    if (scale) {
        *scale = MAX(0.1f, display_scale * model_scale);
    }
    if (radius) {
        *radius = Wow_Clamp(collision_width * 0.5f, 0.5f, 4.0f);
    }

    gi.MemFree(model_data);
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
            if (!Wow_ResolveCreatureModel(display_id,
                                          cache->model_path,
                                          sizeof(cache->model_path),
                                          &cache->scale,
                                          &cache->radius)) {
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
    local->kind = WOW_ENTITY_CREATURE;
    local->hostile = true;
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
    ent->run = Wow_AIRunFrame;
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
        fprintf(stderr, "WoW creature display %u skipped: model %s could not be indexed\n",
                (unsigned)display_id,
                model_path);
        return NULL;
    }
    ent->s.origin = (VECTOR3){ origin->x, origin->y, Wow_TerrainHeight(origin->x, origin->y) };
    ent->s.origin2 = *origin;
    ent->s.scale = scale;
    ent->s.radius = radius;
    ent->s.player = 2;
    Wow_MonsterStart(ent, display_id, origin, yaw, patrol_radius, walk_speed);
    return ent;
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
        if (Wow_SpawnCreature(type->display_id,
                              &creature_origin,
                              (FLOAT)RAD2DEG(angle) + 180.0f,
                              patrol_radius,
                              type->walk_speed)) {
            spawned++;
        }
    }

    fprintf(stderr,
            "WoW spawned %u ambient creatures from %u DBC display types\n",
            (unsigned)spawned,
            (unsigned)(sizeof(wow_ambient_creature_types) / sizeof(wow_ambient_creature_types[0])));
}

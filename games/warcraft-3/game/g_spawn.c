#include "g_local.h"
#include "jass/jass.h"

#define MAX_SPAWN_ITERATIONS 10

extern JASSMODULE jass_funcs[];

void G_InitJassHost(void) {
    jass_sethost(&MAKE(JASSHOST,
        .MemAlloc = gi.MemAlloc,
        .MemFree = gi.MemFree,
        .GetTime = gi.GetTime,
        .ReadFile = gi.ReadFile,
        .natives = jass_funcs,
        .GetPlayerByNumber = G_GetPlayerByNumber,
        .SaveHandle = G_SaveJassHandle,
        .LoadHandle = G_LoadJassHandle,
    ));
}

static DWORD G_NormalizeMapObjectPlayer(DWORD player) {
    if (player < MAX_PLAYERS) {
        return player;
    }
    return PLAYER_NEUTRAL_PASSIVE;
}

LPCSTR targs[] = {
    "none", // NONE
    "air",  // AIR
    "aliv", // ALIVE
    "alli", // ALLIES
    "dead", // DEAD
    "debr", // DEBRIS
    "enem", // ENEMIES
    "grou", // GROUND
    "hero", // HERO
    "invu", // INVULNERABLE
    "item", // ITEM
    "mech", // MECHANICAL
    "neut", // NEUTRAL
    "nonh", // NONHERO
    "nons", // NONSAPPER
    "nots", // NOTSELF
    "orga", // ORGANIC
    "play", // PLAYERUNITS
    "sapp", // SAPPER
    "self", // SELF
    "stru", // STRUCTURE
    "terr", // TERRAIN
    "tree", // TREE
    "vuln", // VULNERABLE
    "wall", // WALL
    "ward", // WARD
    "anci", // ANCIENT
    "nona", // NONANCIENT
    "frie", // FRIEND
    "brid", // BRIDGE
    "deco", // DECORATION
};

TARGTYPE G_GetTargetType(LPCSTR str) {
    DWORD const len = (DWORD)strlen(str);
    if (len < 3) return TARG_NONE;
    char buf[64] = { 0 };
    FOR_LOOP(c, len) buf[c] = tolower(str[c]);
    FOR_LOOP(i, sizeof(targs)/sizeof(*targs)) {
        if (*(DWORD *)buf == *(DWORD *)targs[i])
            return i;
    }
    return TARG_NONE;
}

//struct spawn {
//    LPCSTR name;
//    void (*func)(LPEDICT edict);
//};

//static struct spawn spawns[] = {
//    { "opeo", SP_monster_unit },
//    { NULL, NULL }
//};

void SP_monster_unit(LPEDICT edict);
void SP_monster_tree(LPEDICT edict);

static void G_InitEdict(LPEDICT e) {
    memset(e, 0, sizeof(edict_t));
    e->inuse = true;
    e->item.inventory_slot = -1;
    e->s.scale = 1;
    e->s.number = (int)(e - g_edicts);
}

LPEDICT G_Spawn(void) {
    for (DWORD i = game.max_clients; i < globals.num_edicts; i++) {
        LPEDICT e = &g_edicts[i];
        if (!e->inuse && e->freetime + 1000 < level.time) {
            G_InitEdict(e);
            return e;
        }
    }
    if (globals.num_edicts >= globals.max_edicts) {
        gi.error("G_Spawn: no free edicts (%d max)\n", globals.max_edicts);
        return NULL;
    }
    LPEDICT edict = &g_edicts[globals.num_edicts++];
    G_InitEdict(edict);
    return edict;
}

static void SP_SpawnDoodad(LPEDICT edict) {
    Doodads_t const *row = edict->Doodads;
    LPCSTR dir = row->dir;
    LPCSTR file = row->file;
    PATHSTR buffer;
    if (dir) {
        snprintf(buffer, sizeof(buffer), "%s\\%s\\%s%d.mdx", dir, file, file, edict->variation);
    } else {
        snprintf(buffer, sizeof(buffer), "%s%d.mdx", file, edict->variation);
    }
    edict->s.model = G_RegisterModel(buffer);
    edict->movetype = MOVETYPE_NONE;
    edict->svflags |= SVF_STATIC_SCENERY;
}

static void SP_SpawnDestructable(LPEDICT edict) {
    DestructableData_t const *row = edict->DestructableData;
    LPCSTR dir = row->dir;
    LPCSTR file = row->file;
    LPCSTR path_tex = row->pathingTexture;
    FLOAT radius = row->radius;
    PATHSTR buffer;
    LPCSTR tex = row->textureFile;
    /* texFile may include an extension; "_" means the model has no replacement texture. */
    edict->s.image = tex && *tex && strcmp(tex, "_") ? gi.ImageIndex(tex) : 0;
    if (dir) {
        snprintf(buffer, sizeof(buffer), "%s\\%s\\%s%d.mdx", dir, file, file, edict->variation);
    } else {
        snprintf(buffer, sizeof(buffer), "%s%d.mdx", file, edict->variation);
    }
    edict->s.model = G_RegisterModel(buffer);
    edict->destructable.alive_pathtex = M_LoadPathTex(path_tex);
    edict->destructable.death_pathtex = M_LoadPathTex(row->deathPathingTexture);
    edict->pathtex = edict->destructable.alive_pathtex;
    edict->s.radius = radius > 0.0f ? radius : 50.0f;  /* selection/UI circle only */
    /* WC3 trees have collisionSize 0 and block solely via their baked pathing
     * footprint; only destructables with a real radius (bridges, gates) get a
     * collision circle.  Fabricating a 50-unit circle on every tree was a prime
     * cause of units sticking on trunks. */
    edict->collision = radius > 0.0f ? radius : 0.0f;
    edict->destructable.alive_collision = edict->collision;
    edict->destructable.initialized = true;
    edict->destructable.dead = false;
    edict->destructable.item_table = (DWORD)-1;
    edict->destructable.placement_solid = true;
    edict->destructable.pathing_active = edict->pathtex || edict->collision > 0.0f;
#ifndef USE_SHADOWMAPS
    edict->s.shadow = G_LoadShadowTexture(row->shadow, false);
    edict->s.shadow_rect = 0;
#endif
    edict->health.value = row->maxHealth;
    edict->health.max_value = row->maxHealth;
    edict->targtype = G_GetTargetType(row->targetType);
    if (row->occluderHeight > 0 || edict->targtype == TARG_TREE) {
        edict->s.flags |= EF_FOW_BLOCKER;
        G_FowMarkBlockersDirty();
    }
    edict->movetype = MOVETYPE_NONE;
    edict->svflags |= SVF_STATIC_SCENERY;
}

/* The destructable currently being visited by EnumDestructablesInRect, read
 * back by the GetEnumDestructable native inside the enum action (mirrors the
 * jass-lib `currentunit`/GetEnumUnit pair). */
LPEDICT currentdestructable = NULL;

static BOOL G_ClassIdIsPrintable(DWORD class_id) {
    BYTE const *id = (BYTE const *)&class_id;

    FOR_LOOP(i, 4) {
        if (id[i] < 32 || id[i] > 126) {
            return false;
        }
    }
    return true;
}

/* Bind immutable table rows after class_id is assigned and before entity-specific initialization. */
void G_BindEntityData(LPEDICT edict) {
    edict->UnitProfile = G_UnitProfile(edict->class_id);
    edict->UnitBalance = G_UnitBalance(edict->class_id);
    edict->UnitData = G_UnitData(edict->class_id);
    edict->UnitUI = G_UnitUI(edict->class_id);
    edict->UnitWeapons = G_UnitWeapons(edict->class_id);
    edict->UnitAbilities = G_UnitAbil(edict->class_id);
    edict->Doodads = G_Doodad(edict->class_id);
    edict->ItemData = G_ItemData(edict->class_id);
    edict->DestructableData = G_DestructableData(edict->class_id);
}

/* Save files omit process addresses; restore class-owned callbacks without replaying spawn-time gameplay initialization. */
void G_BindEntityRuntime(LPEDICT edict) {
    if (edict->DestructableData->file) {
        edict->stand = tree_stand; edict->birth = tree_birth; edict->pain = tree_pain; edict->die = tree_die;
        edict->think = monster_think;
    } else if (edict->UnitBalance->id || edict->UnitUI->modelFile) {
        edict->stand = unit_stand; edict->birth = unit_birth; edict->die = unit_die;
        edict->think = monster_think;
    }
}

void SP_CallSpawn(LPEDICT edict) {
    if (!edict->class_id)
        return;
    edict->s.class_id = edict->class_id;
    G_BindEntityData(edict);
    if (edict->Doodads->id) {
        SP_SpawnDoodad(edict);
    } else if (edict->DestructableData->file) {
        SP_SpawnDestructable(edict);
        SP_monster_tree(edict);
    } else if (edict->UnitUI->modelFile) {
        SP_SpawnUnit(edict);
        SP_monster_unit(edict);
    } else if (edict->ItemData->file) {
        SP_SpawnItem(edict);
    } else if (MAKEFOURCC('s', 'l', 'o', 'c') == edict->class_id) {
        edict->svflags |= SVF_NOCLIENT;
    } else {
        if (edict->class_id == MAKEFOURCC('L', 'T', 'l', 't')) {
            (void)G_DestructableData(edict->class_id)->file; /* TODO: use model path */
        }
        edict->svflags |= SVF_NOCLIENT;
        if (!G_ClassIdIsPrintable(edict->class_id)) {
            fprintf(stderr, "Warning: Invalid map object ID %.4s\n", (const char *)&edict->class_id);
        }
    }
//    for (struct spawn *s = spawns; s->func; s++) {
//        if (*((int const *)s->name) == edict->class_id) {
//            s->func(edict);
//            return;
//        }
//    }
}

void SP_worldspawn(LPEDICT ent) {
    SetAbilityNames();
}

static DWORD G_MapPlayerTeam(LPCMAPINFO mapinfo, DWORD playernum) {
    if (!mapinfo || !mapinfo->teams) {
        return playernum;
    }
    FOR_LOOP(i, mapinfo->num_teams) {
        if (mapinfo->teams[i].playerMasks & (1u << playernum)) {
            return i;
        }
    }
    return playernum;
}

static DWORD G_LocalMapPlayerNumber(LPCMAPINFO mapinfo) {
    if (!mapinfo) {
        return 0;
    }
    FOR_LOOP(i, MAX_PLAYERS) {
        if (mapinfo->players[i].used && mapinfo->players[i].playerType == kPlayerTypeHuman) {
            return i;
        }
    }
    return 0;
}

static DWORD G_ClientSlotMapPlayerNumber(LPCMAPINFO mapinfo, DWORD slot, DWORD local_player) {
    DWORD count = 1;

    if (slot == 0) {
        return local_player;
    }
    FOR_LOOP(i, MAX_PLAYERS) {
        if (i == local_player) {
            continue;
        }
        if (count++ == slot) {
            return i;
        }
    }
    return slot;
}

/* JASS mapcontrol values do not match W3I playerType values after computer. */
static DWORD G_MapControl(LPCMAPPLAYER player) {
    if (!player) return 5;
    switch (player->playerType) {
        case kPlayerTypeHuman: return 0;
        case kPlayerTypeComputer: return 1;
        case kPlayerTypeRescuable: return 2;
        case kPlayerTypeNeutral: return 3;
        default: return 5;
    }
}

/* Race preferences are bit flags, unlike the sequential W3I race enum. */
static DWORD G_RacePreference(LPCMAPPLAYER player) {
    if (!player) return 0;
    switch (player->playerRace) {
        case kPlayerRaceHuman: return 1;
        case kPlayerRaceOrc: return 2;
        case kPlayerRaceNightElf: return 4;
        case kPlayerRaceUndead: return 8;
        default: return 32;
    }
}

static void G_InitMapPlayer(LPEDICT clent, LPCMAPINFO mapinfo, DWORD playernum) {
    LPCMAPPLAYER player = mapinfo ? mapinfo->players + playernum : NULL;
    LPPLAYER ps = &clent->client->ps;
    G_SetClientConnected(clent, false);
    G_ResetSelectionFocus(clent->client);
    clent->client->commands_dirty = false;
    memset(&clent->client->jass, 0, sizeof(clent->client->jass));
    memset(clent->client->tech, 0, sizeof(clent->client->tech));
    memset(ps, 0, sizeof(PLAYER));
    ps->number = playernum;
    ps->team = G_MapPlayerTeam(mapinfo, playernum);
    ps->color = player ? player->color : playernum;
    ps->race = player ? player->playerRace : kPlayerRaceNone;
    ps->name = player ? player->playerName : NULL;
    ps->start_location = player ? (LONG)playernum : -1;
    ps->stats[PLAYERSTATE_FOOD_CAP_CEILING] = (USHORT)MIN(MAX(0, game.constants.foodCeiling), USHRT_MAX);
    ps->stats[PLAYERSTATE_GOLD_UPKEEP_RATE] = 100;
    ps->stats[PLAYERSTATE_LUMBER_UPKEEP_RATE] = 100;
    ps->origin.x = player ? player->startingPosition.x : 0.0f;
    ps->origin.y = player ? player->startingPosition.y : 0.0f;
    ps->viewquat = Quaternion_fromEuler(&MAKE(VECTOR3, 326, 0, 0), ROTATE_ZYX);
    ps->fov = 50;
    ps->distance = 1650;
    clent->client->camera.state.position = ps->origin;
    clent->client->camera.state.viewangles = (VECTOR3){ 326, 0, 0 };
    clent->client->camera.state.fov = 50;
    clent->client->camera.state.target_distance = 1650;
    clent->client->camera.old_state = clent->client->camera.state;
    if (mapinfo) {
        FOR_LOOP(i, mapinfo->num_techAvailabilities) {
            mapTechAvailability_t const *tech = mapinfo->techAvailabilities + i;
            if (tech->playerFlags & (1u << playernum)) {
                G_SetPlayerTechMaxAllowed(clent->client, tech->techID, 0);
            }
        }
        G_SetClientCameraBounds(clent->client, mapinfo->cameraBounds.bounds);
    }
    clent->client->mapplayer = player;
    clent->client->jass.controller = G_MapControl(player);
    clent->client->jass.race_pref = G_RacePreference(player);
    clent->client->jass.race_selectable = true;
    clent->client->jass.handicap = clent->client->jass.handicap_xp = 100.0f;
    strlcpy(clent->client->jass.name, player && player->playerName ? player->playerName : "", sizeof(clent->client->jass.name));
    ps->name = clent->client->jass.name;
}

void G_SpawnEntities(void) {
    LPCMAPINFO mapinfo = CM_GetMapInfo();
    LPCDOODAD entities = CM_GetDoodads();
    DWORD local_player = G_LocalMapPlayerNumber(mapinfo);
    LONG difficulty = 1;
    LPCSTR map_path = gi.CvarString ? gi.CvarString("map", "") : "";

    /* Map replacement must release script roots before level pointers are cleared. */
    G_BotShutdown();
    if (level.vm) { jass_close(level.vm); level.vm = NULL; }
    G_ClearSaveRegistries();
    G_FowShutdown();
    memset(&level, 0, sizeof(level));
    level.time = gi.GetTime ? gi.GetTime() : 0;

    level.mapinfo = mapinfo;
    level.setup.teams = mapinfo ? mapinfo->num_teams : 0;
    if (mapinfo) FOR_LOOP(i, MAX_PLAYERS) level.setup.players += mapinfo->players[i].used;
    level.setup.game_type = 4;
    level.setup.speed = 2;
    if ((!strncasecmp(map_path, "Maps\\Campaign\\", 14) ||
         !strncasecmp(map_path, "Maps/Campaign/", 14) ||
         !strncasecmp(map_path, "Maps\\FrozenThrone\\Campaign\\", 26) ||
         !strncasecmp(map_path, "Maps/FrozenThrone/Campaign/", 26)) && gi.CvarString) {
        difficulty = atoi(gi.CvarString("wc3_campaign_difficulty", "1"));
    }
    if (difficulty < 0) difficulty = 0;
    if (difficulty > 3) difficulty = 3;
    level.setup.difficulty = (DWORD)difficulty;
    level.setup.default_difficulty = (DWORD)difficulty;
    level.setup.resource_density = level.setup.creature_density = 2;
    if (mapinfo) {
        strlcpy(level.setup.name, mapinfo->mapName ? mapinfo->mapName : "", sizeof(level.setup.name));
        strlcpy(level.setup.description, mapinfo->mapDescription ? mapinfo->mapDescription : "", sizeof(level.setup.description));
    }
    G_FowInit();
    G_InitJassHost();
    level.vm = jass_newstate();
    
    FOR_LOOP(p, MAX_PLAYERS) {
        LPGAMECLIENT client = game.clients+p;
        DWORD playernum = G_ClientSlotMapPlayerNumber(mapinfo, p, local_player);
        g_edicts[p].client = client;
        G_InitMapPlayer(g_edicts+p, mapinfo, playernum);
    }

    globals.num_edicts = game.max_clients;
    /* Quake II's body queue reserves real edicts before map entities, keeping all entity pointers in one address domain. */
    G_InitWaypoints();

    FOR_EACH_LIST(DOODAD const, doodad, entities) {
//        if (doodad->doodID == MAKEFOURCC('h', 'C', '0', '2')) {
//            int a=0;
//            printf("%.4s", )
//        }
        LPEDICT ent = G_Spawn();
        if (!ent) {
            break;
        }
        ent->class_id = doodad->doodID;
        ent->variation = doodad->variation;
        ent->hero = doodad->hero;
        ent->s.player = G_NormalizeMapObjectPlayer(doodad->player);
        ent->s.origin = doodad->position;
        ent->s.angle = doodad->angle;
        ent->s.scale = doodad->scale.x;
        SP_CallSpawn(ent);
        if (G_IsDestructable(ent)) {
            G_InitializeDestructablePlacement(ent, doodad);
            G_RegisterGroundSurface(ent);
        }
        gi.LinkEntity(ent);
    }
    SP_worldspawn(NULL);
    
    jass_dofile(level.vm, "Scripts\\common.j");
    jass_dofile(level.vm, "Scripts\\Blizzard.j");
//    jass_dofilenative(level.vm, "/Users/igor/Desktop/war3map.j");
    jass_dobuffer(level.vm, level.mapinfo->mapscript);

    UI_Init();
    CM_BakeStaticObstacles();
    /* Start simulation from the map load itself so dedicated and listen-server restores share one lifecycle. */
    level.started = true;
}
 
LPEDICT SP_SpawnAtLocation(DWORD class_id, DWORD player, LPCVECTOR2 location) {
    LPEDICT ent = G_Spawn();
    LPGAMECLIENT client;
    if (!ent) {
        return NULL;
    }
    ent->class_id = class_id;
    ent->s.class_id = class_id;
    ent->spawn_time = gi.GetTime();
    ent->s.origin.x = location->x;
    ent->s.origin.y = location->y;
    ent->s.origin.z = CM_GetHeightAtPoint(location->x, location->y);
    ent->s.scale = 1;
    ent->s.angle = -M_PI / 2;
    ent->s.player = player;
    gi.LinkEntity(ent);
    SP_CallSpawn(ent);
    if (ent->birth) {
        ent->birth(ent);
    }
    client = G_GetPlayerClientByNumber(player);
    if ((ent->svflags & SVF_MONSTER) && client && client->ps.number == player) {
        G_InvalidateCommands(client);
        G_InvalidateUnitShortcutsForUnit(ent);
    }
    return ent;
}

static BOOL bind_map_destructables = false;

void G_SetDestructableScriptBinding(BOOL enabled) {
    bind_map_destructables = enabled;
}

/* Runtime (JASS CreateDestructable) spawn of a destructable.  Mirrors the
 * map-doodad spawn loop in G_SpawnEntities: set class_id/variation/origin/
 * facing/scale, then route through SP_CallSpawn (which sends a destructable
 * class_id to SP_SpawnDestructable + SP_monster_tree, giving it a model, life,
 * collision and the core destructable lifecycle; tree_die remains a legacy
 * callback entry point, but death does not depend on that callback).
 * Destructables are neutral-passive, like the map-placed ones.  facing is in
 * radians (the native converts from JASS degrees).
 *
 * Parity note (Ghidra): the original CreateDestructable (FUN_003f80b0 ->
 * worker FUN_00621d90) always creates a fresh instance — its hash lookup
 * resolves the destructable *type* by objectid, not an existing entity by
 * position.  We diverge with find-or-create because OUR engine already spawns
 * every war3map.doo destructable in G_SpawnEntities, and the map's generated
 * CreateAllDestructables then "creates" the 13 named ones again to bind their
 * gg_dest_* handles + death triggers.  Reusing the pre-placed entity (like
 * unit_createorfind does for CreateUnit) yields the same observable result as
 * the original — one crate/gate carrying the trigger — instead of a stacked
 * duplicate.  Match a same-type destructable within 10 units of the spot. */
/* HACK: Positional binding is required until the map parser exposes the
 * generated script variable's editor creation ID. */
LPEDICT G_CreateDestructable(DWORD class_id, FLOAT x, FLOAT y, FLOAT z, FLOAT facing, FLOAT scale, DWORD variation) {
    if (bind_map_destructables) {
        LPEDICT best = NULL;
        FLOAT best_distance = 10.0f;

        FOR_LOOP(i, globals.num_edicts) {
            LPEDICT existing = &g_edicts[i];
            FLOAT distance;

            if (!existing->inuse ||
                existing->class_id != class_id ||
                !G_IsDestructable(existing) ||
                !existing->destructable.map_placed ||
                existing->destructable.script_bound) {
                continue;
            }

            distance = Vector2_distance(
                &MAKE(VECTOR2, x, y),
                &existing->s.origin2);

            if (distance >= best_distance) {
                continue;
            }

            best = existing;
            best_distance = distance;
        }

        if (best) {
            best->destructable.script_bound = true;

            G_ActivateScriptedDestructable(best,
                                           x,
                                           y,
                                           z,
                                           facing,
                                           scale,
                                           variation);

            CM_BakeStaticObstacles();
            return best;
        }
    }
    LPEDICT ent = G_Spawn();
    if (!ent) return NULL;
    ent->class_id = class_id;
    ent->variation = variation;
    ent->s.player = PLAYER_NEUTRAL_PASSIVE;
    ent->s.origin = MAKE(VECTOR3, x, y, z);
    ent->s.angle = facing;
    ent->s.scale = scale;
    ent->spawn_time = gi.GetTime();
    SP_CallSpawn(ent);
    G_RegisterGroundSurface(ent);
    gi.LinkEntity(ent);
    if (G_IsDestructable(ent)) CM_BakeStaticObstacles();
    return ent;
}

LPEDICT G_CreateDeadDestructable(DWORD class_id,
                                 FLOAT x,
                                 FLOAT y,
                                 FLOAT z,
                                 FLOAT facing,
                                 FLOAT scale,
                                 DWORD variation) {
    LPEDICT ent = G_CreateDestructable(class_id, x, y, z, facing, scale, variation);

    if (ent) {
        G_SetDestructableDeadState(ent, false);
    }
    return ent;
}

BOOL SP_FindEmptySpaceAround(LPEDICT townhall, DWORD class_id, LPVECTOR2 out, FLOAT *angle) {
    FLOAT const colsize = G_UnitUI(class_id)->selectionScale * SEL_SCALE / 2;
    FLOAT const start_angle = M_PI * 1.25f;
    FOR_LOOP(i, MAX_SPAWN_ITERATIONS) {
        FLOAT const radius = townhall->s.radius + colsize * (i * 2 + 1);
        FLOAT const num_points = M_PI * radius / colsize;
        FOR_LOOP(j, num_points) {
            *angle = start_angle + 2 * M_PI * j / num_points;
            *out = MAKE(VECTOR2,
                townhall->s.origin2.x + cosf(*angle) * radius,
                townhall->s.origin2.y + sinf(*angle) * radius,
            );
            if (M_CheckCollision(out, colsize))
                continue;
            return true;
        }
    }
    return false;
}

static BOOL SP_CanPlaceUnitAt(LPEDICT unit, LPCVECTOR2 point) {
    if (!CM_PointIsPathableForRadius(point, unit->collision)) {
        return false;
    }

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT other = &globals.edicts[i];
        VECTOR2 delta;

        if (other == unit || IS_HOLLOW(other) || other->movetype == MOVETYPE_NONE || other->collision <= 0.0f) {
            continue;
        }
        delta = Vector2_sub(&other->s.origin2, point);
        if (Vector2_len(&delta) < unit->collision + other->collision) {
            return false;
        }
    }
    return true;
}

typedef struct {
    LPEDICT   producer;
    LPEDICT   unit;
    FLOAT     spacing;
    LPVECTOR2 out;
    FLOAT    *angle;
} unitExitCtx_t;

static BOOL SP_TryUnitExitCandidate(unitExitCtx_t const *ctx, int grid_x, int grid_y) {
    VECTOR2 const candidate = {
        ctx->producer->s.origin2.x + (FLOAT)grid_x * ctx->spacing,
        ctx->producer->s.origin2.y + (FLOAT)grid_y * ctx->spacing,
    };

    if (!SP_CanPlaceUnitAt(ctx->unit, &candidate)) {
        return false;
    }
    *ctx->out = candidate;
    *ctx->angle = atan2f(candidate.y - ctx->producer->s.origin2.y,
                         candidate.x - ctx->producer->s.origin2.x);
    return true;
}

/* Trained units are created at their producer and remain hidden until a legal
 * exit point is found. Search deterministic 64-world-unit square rings, using
 * the trained unit's real collision radius against both the baked static
 * pathmap and dynamic unit circles. */
BOOL SP_FindUnitExitPosition(LPEDICT producer, LPEDICT unit, LPVECTOR2 out, FLOAT *angle) {
    DWORD const max_candidates = 300;
    DWORD tested = 0;
    unitExitCtx_t ctx;

    if (!producer || !unit || !out || !angle) {
        return false;
    }

    ctx = (unitExitCtx_t){ producer, unit, 64.0f, out, angle };

    for (int ring = 1; tested < max_candidates; ring++) {
        int const lo = -ring;
        int const hi = ring;

        for (int x = lo; x <= hi && tested < max_candidates; x++, tested++) {
            if (SP_TryUnitExitCandidate(&ctx, x, lo)) return true;
        }
        for (int y = lo + 1; y <= hi && tested < max_candidates; y++, tested++) {
            if (SP_TryUnitExitCandidate(&ctx, hi, y)) return true;
        }
        for (int x = hi - 1; x >= lo && tested < max_candidates; x--, tested++) {
            if (SP_TryUnitExitCandidate(&ctx, x, hi)) return true;
        }
        for (int y = hi - 1; y > lo && tested < max_candidates; y--, tested++) {
            if (SP_TryUnitExitCandidate(&ctx, lo, y)) return true;
        }
    }
    return false;
}

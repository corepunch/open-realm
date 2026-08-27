#include "g_local.h"
#include "jass/jass.h"

#define MAX_SPAWN_ITERATIONS 10

extern JASSMODULE jass_funcs[];

static void G_InitJassHost(void) {
    jass_sethost(&MAKE(JASSHOST,
        .MemAlloc = gi.MemAlloc,
        .MemFree = gi.MemFree,
        .GetTime = gi.GetTime,
        .ReadFile = gi.ReadFile,
        .natives = jass_funcs,
        .GetPlayerByNumber = G_GetPlayerByNumber,
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

extern sheetRow_t *Doodads;

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
    LPCSTR class_id = GetClassName(edict->class_id);
    LPCSTR dir = FS_FindSheetCell(Doodads, class_id, "dir");
    LPCSTR file = FS_FindSheetCell(Doodads, class_id, "file");
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
    LPCSTR dir = DESTRUCTABLE_DIRECTORY(edict->class_id);
    LPCSTR file = DESTRUCTABLE_FILE(edict->class_id);
    LPCSTR path_tex = DESTRUCTABLE_PATH_TEX(edict->class_id);
    FLOAT radius = DESTRUCTABLE_RADIUS(edict->class_id);
    PATHSTR buffer;
    LPCSTR tex = DESTRUCTABLE_TEXTURE(edict->class_id);
    /* texFile may include an extension; "_" means the model has no replacement texture. */
    edict->s.image = tex && *tex && strcmp(tex, "_") ? gi.ImageIndex(tex) : 0;
    if (dir) {
        snprintf(buffer, sizeof(buffer), "%s\\%s\\%s%d.mdx", dir, file, file, edict->variation);
    } else {
        snprintf(buffer, sizeof(buffer), "%s%d.mdx", file, edict->variation);
    }
    edict->s.model = G_RegisterModel(buffer);
    edict->destructable.alive_pathtex = M_LoadPathTex(path_tex);
    edict->destructable.death_pathtex = M_LoadPathTex(DESTRUCTABLE_DEATH_PATH_TEX(edict->class_id));
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
    edict->s.shadow = G_LoadShadowTexture(DESTRUCTABLE_SHADOW(edict->class_id), false);
    edict->s.shadow_rect = 0;
#endif
    edict->health.value = DESTRUCTABLE_HIT_POINT_MAXIMUM(edict->class_id);
    edict->health.max_value = DESTRUCTABLE_HIT_POINT_MAXIMUM(edict->class_id);
    edict->targtype = G_GetTargetType(DESTRUCTABLE_TARGETED_AS(edict->class_id));
    if (DESTRUCTABLE_OCCLUDER_HEIGHT(edict->class_id) > 0 || edict->targtype == TARG_TREE) {
        edict->s.flags |= EF_FOW_BLOCKER;
    }
    edict->movetype = MOVETYPE_NONE;
    edict->svflags |= SVF_STATIC_SCENERY;
}

/* The destructable currently being visited by EnumDestructablesInRect, read
 * back by the GetEnumDestructable native inside the enum action (mirrors the
 * jass-lib `currentunit`/GetEnumUnit pair). */
LPEDICT currentdestructable = NULL;

sheetRow_t *find_row(LPCSTR dood_id) {
    FOR_EACH_LIST(sheetRow_t, d, Doodads){
        if (!strcmp(d->name, dood_id))
            return d;
    }
    return NULL;
}

static BOOL G_ClassIdIsPrintable(DWORD class_id) {
    BYTE const *id = (BYTE const *)&class_id;

    FOR_LOOP(i, 4) {
        if (id[i] < 32 || id[i] > 126) {
            return false;
        }
    }
    return true;
}

void SP_CallSpawn(LPEDICT edict) {
    if (!edict->class_id)
        return;
    edict->s.class_id = edict->class_id;
    if (find_row(GetClassName(edict->class_id))) {
        SP_SpawnDoodad(edict);
    } else if (DESTRUCTABLE_FILE(edict->class_id)) {
        SP_SpawnDestructable(edict);
        SP_monster_tree(edict);
    } else if (UNIT_MODEL(edict->class_id)) {
        SP_SpawnUnit(edict);
        SP_monster_unit(edict);
    } else if (ITEM_FILE(edict->class_id)) {
        SP_SpawnItem(edict);
    } else if (MAKEFOURCC('s', 'l', 'o', 'c') == edict->class_id) {
        edict->svflags |= SVF_NOCLIENT;
    } else {
        if (edict->class_id == MAKEFOURCC('L', 'T', 'l', 't')) {
            UnitStringField(DestructableMetaData, edict->class_id, "bfil");
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
    gi.configstring(CS_HEALTHBAR, Theme_String("SimpleHpBarConsole", "Default"));
    gi.configstring(CS_MANAHBAR, Theme_String("SimpleManaBarConsole", "Default"));
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
    memset(&clent->client->jass, 0, sizeof(clent->client->jass));
    memset(ps, 0, sizeof(PLAYER));
    ps->number = playernum;
    ps->team = G_MapPlayerTeam(mapinfo, playernum);
    ps->color = player ? player->color : playernum;
    ps->race = player ? player->playerRace : kPlayerRaceNone;
    ps->name = player ? player->playerName : NULL;
    ps->start_location = player ? (LONG)playernum : -1;
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

    G_FowShutdown();
    memset(&level, 0, sizeof(level));

    level.mapinfo = mapinfo;
    level.setup.teams = mapinfo ? mapinfo->num_teams : 0;
    if (mapinfo) FOR_LOOP(i, MAX_PLAYERS) level.setup.players += mapinfo->players[i].used;
    level.setup.game_type = 4;
    level.setup.speed = 2;
    level.setup.difficulty = 1;
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
}
 
LPEDICT SP_SpawnAtLocation(DWORD class_id, DWORD player, LPCVECTOR2 location) {
    LPEDICT ent = G_Spawn();
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
    return ent;
}

static BOOL bind_map_destructables = false;

void G_SetDestructableScriptBinding(BOOL enabled) {
    bind_map_destructables = enabled;

    if (G_DebugDestructables()) {
        fprintf(stderr,
                "WC3 dest script: map destructable binding %s\n",
                enabled ? "begin" : "end");
    }
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
LPEDICT G_CreateDestructable(DWORD class_id, FLOAT x, FLOAT y, FLOAT z, FLOAT facing, FLOAT scale, DWORD variation) {
    if (G_DebugDestructables())
        fprintf(stderr, "WC3 dest script: create request type=%.4s pos=(%.1f %.1f %.1f) face=%.3f scale=%.2f var=%u\n",
                (LPCSTR)&class_id, x, y, z, facing, scale, (unsigned)variation);
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

            if (G_DebugDestructables()) {

                fprintf(stderr,
                        "WC3 dest script: create matched+activated ent=%u type=%.4s distance=%.2f "
                        "editor=%u var=%u requested_var=%u pos=(%.1f %.1f) "
                        "hidden=%u solid=%u life=%.1f\n",
                        (unsigned)best->s.number,
                        (LPCSTR)&best->class_id,
                        best_distance,
                        (unsigned)best->destructable.editor_id,
                        (unsigned)best->variation,
                        (unsigned)variation,
                        best->s.origin2.x,
                        best->s.origin2.y,
                        (unsigned)((best->s.renderfx & RF_HIDDEN) != 0),
                        (unsigned)best->destructable.placement_solid,
                        best->health.value);
            }

            CM_BakeStaticObstacles();

            return best;
        }

        if (G_DebugDestructables()) {
            fprintf(stderr,
                    "WC3 dest script: create no preplaced match type=%.4s pos=(%.1f %.1f) var=%u\n",
                    (LPCSTR)&class_id,
                    x,
                    y,
                    (unsigned)variation);
        }
    }
    LPEDICT ent = G_Spawn();
    if (!ent) {
        if (G_DebugDestructables())
            fprintf(stderr, "WC3 dest script: create failed type=%.4s reason=no-edict\n", (LPCSTR)&class_id);
        return NULL;
    }
    ent->class_id = class_id;
    ent->variation = variation;
    ent->s.player = PLAYER_NEUTRAL_PASSIVE;
    ent->s.origin = MAKE(VECTOR3, x, y, z);
    ent->s.angle = facing;
    ent->s.scale = scale;
    ent->spawn_time = gi.GetTime();
    SP_CallSpawn(ent);
    gi.LinkEntity(ent);
    if (G_IsDestructable(ent)) {
        CM_BakeStaticObstacles();
    }
    if (G_DebugDestructables())
        fprintf(stderr, "WC3 dest script: create spawned ent=%u type=%.4s is_dest=%u inuse=%u flags=0x%x "
                        "renderfx=0x%x\n", (unsigned)ent->s.number, (LPCSTR)&ent->class_id,
                (unsigned)G_IsDestructable(ent), (unsigned)ent->inuse,
                (unsigned)ent->s.flags, (unsigned)ent->s.renderfx);
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
    FLOAT const colsize = UNIT_SELECTION_SCALE(class_id) * SEL_SCALE / 2;
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

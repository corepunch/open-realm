#include "g_local.h"
#include "jass/jass.h"

#define MAX_SPAWN_ITERATIONS 10

extern JASSMODULE jass_funcs[];

static BOOL G_TutorialFlowDebugEnabledForMapSource(void) {
    return WC3_TUTORIAL_DEBUG_ENABLED();
}

/* Keep generated war3map.j as the authoritative source for preplaced units and items. */
static BOOL G_LoadMapUnitData(void) {
    return atoi(gi.CvarString("wc3_load_units_from_map_data", "0")) != 0;
}

/* Unit/item placements are duplicated by the generated CreateAllUnits/CreateAllItems functions. */
static BOOL G_MapObjectCreatedByMapScript(DWORD id) {
    if (id == MAKEFOURCC('s', 'l', 'o', 'c') || G_Doodad(id)->id || G_DestructableData(id)->file)
        return false;
    return G_UnitUI(id)->modelFile || G_ItemData(id)->file;
}

#ifdef BZ_TESTS
BOOL G_TestMapObjectCreatedByMapScript(DWORD id) { return G_MapObjectCreatedByMapScript(id); }
#endif

static void G_JassCoroutineTrace(HANDLE trigger_handle, LPCSTR function, LPCSTR phase,
                                 DWORD now, DWORD wake_time, BOOL yielded, BOOL done) {
    LPTRIGGER trigger = trigger_handle;
    LONG ordinal;
    if (!G_TutorialFlowDebugEnabledForMapSource() || !trigger ||
        trigger < level.triggers || trigger >= level.triggers + level.num_triggers) {
        return;
    }
    ordinal = (LONG)(trigger - level.triggers);
    if (ordinal < 120 || ordinal > 165) return;
    fprintf(stderr,
            "WC3_TUTORIAL_COROUTINE phase=%s trigger=%ld function=\"%s\" now=%u wake=%u yielded=%d done=%d\n",
            phase ? phase : "unknown", (long)ordinal,
            function ? function : "(none)", (unsigned)now, (unsigned)wake_time,
            (int)yielded, (int)done);
}

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
        .CoroutineTrace = G_JassCoroutineTrace,
        .VariableChanged = G_JassVariableChanged,
    ));
}

static LPCSTR G_FindJassMapFunction(LPCSTR script, LPCSTR name, LPCSTR *finish) {
    char needle[192];
    LPCSTR start, end;
    if (finish) *finish = NULL;
    if (!script || !name || !*name) return NULL;
    snprintf(needle, sizeof(needle), "function %s takes", name);
    start = strstr(script, needle);
    if (!start) return NULL;
    end = strstr(start, "endfunction");
    if (!end) return start;
    end += strlen("endfunction");
    while (*end == '\r' || *end == '\n') end++;
    if (finish) *finish = end;
    return start;
}

static void G_DumpTutorialJassFunction(LPCSTR script, LPCSTR name) {
    LPCSTR start, finish;
    size_t length;
    start = G_FindJassMapFunction(script, name, &finish);
    if (!start) {
        fprintf(stdout, "WC3_TUTORIAL_SOURCE missing function=\"%s\"\n", name);
        return;
    }
    if (!finish) {
        fprintf(stdout, "WC3_TUTORIAL_SOURCE unterminated function=\"%s\"\n", name);
        return;
    }
    length = (size_t)(finish - start);
    fprintf(stdout, "WC3_TUTORIAL_SOURCE begin function=\"%s\"\n%.*s", name, (int)length, start);
    if (!length || start[length - 1] != '\n') fputc('\n', stdout);
    fprintf(stdout, "WC3_TUTORIAL_SOURCE end function=\"%s\"\n", name);
}

static BOOL G_JassRangeContains(LPCSTR start, LPCSTR finish, LPCSTR needle) {
    LPCSTR hit;
    if (!start || !finish || !needle || start >= finish) return false;
    hit = strstr(start, needle);
    return hit && hit < finish;
}

static void G_DumpTutorialJassFunctionsReferencing(LPCSTR script, LPCSTR needle) {
    LPCSTR cursor = script;
    char function_name[160];
    if (!script || !needle || !*needle) return;
    while ((cursor = strstr(cursor, "function ")) != NULL) {
        LPCSTR name_start = cursor + strlen("function ");
        LPCSTR name_end = strstr(name_start, " takes");
        LPCSTR finish = strstr(name_start, "endfunction");
        size_t name_len;
        if (!name_end || !finish) break;
        finish += strlen("endfunction");
        if (G_JassRangeContains(cursor, finish, needle)) {
            name_len = (size_t)(name_end - name_start);
            if (name_len > 0 && name_len < sizeof(function_name)) {
                memcpy(function_name, name_start, name_len);
                function_name[name_len] = '\0';
                fprintf(stdout,
                        "WC3_TUTORIAL_SOURCE reference token=\"%s\" function=\"%s\"\n",
                        needle, function_name);
                G_DumpTutorialJassFunction(script, function_name);
            }
        }
        cursor = finish;
    }
}

static void G_DumpReferencedTutorialTriggers(LPCSTR script, LPCSTR start, LPCSTR finish) {
    LPCSTR cursor = start;
    char seen[16][96] = {{0}};
    DWORD seen_count = 0;
    while (cursor && cursor < finish && seen_count < 16) {
        LPCSTR ref = strstr(cursor, "gg_trg_");
        size_t len;
        BOOL duplicate = false;
        char suffix[96];
        char function_name[160];
        if (!ref || ref >= finish) break;
        ref += strlen("gg_trg_");
        len = 0;
        while (ref + len < finish &&
               ((ref[len] >= 'A' && ref[len] <= 'Z') ||
                (ref[len] >= 'a' && ref[len] <= 'z') ||
                (ref[len] >= '0' && ref[len] <= '9') || ref[len] == '_')) {
            len++;
        }
        if (!len || len >= sizeof(suffix)) { cursor = ref + (len ? len : 1); continue; }
        memcpy(suffix, ref, len);
        suffix[len] = '\0';
        FOR_LOOP(i, seen_count) if (!strcmp(seen[i], suffix)) duplicate = true;
        if (!duplicate) {
            snprintf(seen[seen_count++], sizeof(seen[0]), "%s", suffix);
            fprintf(stdout, "WC3_TUTORIAL_SOURCE reference trigger=\"gg_trg_%s\"\n", suffix);
            snprintf(function_name, sizeof(function_name), "Trig_%s_Conditions", suffix);
            G_DumpTutorialJassFunction(script, function_name);
            snprintf(function_name, sizeof(function_name), "Trig_%s_Actions", suffix);
            G_DumpTutorialJassFunction(script, function_name);
            snprintf(function_name, sizeof(function_name), "InitTrig_%s", suffix);
            G_DumpTutorialJassFunction(script, function_name);
        }
        cursor = ref + len;
    }
}

static void G_DumpPrologue02BurrowHandoffSource(LPCSTR script) {
    static LPCSTR const root_names[] = {
        "Trig_W2_BurrowComplete_Q_Func002001",
        "Trig_W2_BurrowComplete_Q_Func007001",
        "Trig_W2_BurrowComplete_Q_Conditions",
        "Trig_W2_BurrowComplete_Q_Actions",
        "InitTrig_W2_BurrowComplete_Q",
        "Trig_W2_BurrowComplete_Abort_Conditions",
        "Trig_W2_BurrowComplete_Abort_Actions",
        "InitTrig_W2_BurrowComplete_Abort",
        "Trig_W_Burrow_Check_Conditions",
        "Trig_W_Burrow_Check_Actions",
        "InitTrig_W_Burrow_Check",
        "Trig_Done_Burrows_Q_Conditions",
        "Trig_Done_Burrows_Q_Actions",
        "InitTrig_Done_Burrows_Q",
        "Trig_U1_SelectWarMill_Q_Conditions",
        "Trig_U1_SelectWarMill_Q_Actions",
        "InitTrig_U1_SelectWarMill_Q",
    };
    LPCSTR action_start, action_finish;
    if (!G_TutorialFlowDebugEnabledForMapSource()) return;
    FOR_LOOP(i, sizeof(root_names) / sizeof(root_names[0]))
        G_DumpTutorialJassFunction(script, root_names[i]);
    action_start = G_FindJassMapFunction(script, "Trig_W2_BurrowComplete_Q_Actions", &action_finish);
    if (action_start && action_finish)
        G_DumpReferencedTutorialTriggers(script, action_start, action_finish);
    G_DumpTutorialJassFunctionsReferencing(script, "gg_snd_T02Narrator031");
    G_DumpTutorialJassFunctionsReferencing(script, "gg_snd_T02Narrator032");
    G_DumpTutorialJassFunctionsReferencing(script, "gg_snd_T02Narrator033");
    G_DumpTutorialJassFunctionsReferencing(script, "gg_snd_T02Narrator034");
    G_DumpTutorialJassFunctionsReferencing(script, "gg_snd_T02Narrator035");
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
    /* Missing target metadata means no target flags; strlen(NULL) previously crashed sparse unit transforms. */
    if (!str || !*str) return TARG_NONE;
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
    e->animation_speed = 1.0f;
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

/* Confirm a candidate variation resolves through the authoritative VFS. */
static BOOL SP_DoodadModelExists(LPCSTR filename) {
    DWORD size = 0;
    HANDLE data;

    if (!filename || !*filename) return false;
    data = gi.ReadFile(filename, &size);
    if (!data) return false;
    gi.MemFree(data);
    return true;
}

/* Resolve an authored doodad model and only use a variation file that exists. */
static void SP_DoodadModelFilename(Doodads_t const *row, DWORD variation,
                                   LPSTR out, size_t out_size) {
    PATHSTR stem = { 0 };
    PATHSTR varied = { 0 };
    LPCSTR file;
    char *dot;

    if (!out || !out_size) return;
    out[0] = '\0';
    if (!row || !(file = row->file) || !*file) return;

    /* Doodads.slk `file` is already the authoritative model stem.  Do not
     * rebuild it from the legacy `dir` column: doing so turns entries such as
     * LOo2 into a path that the game-side MDX loader cannot open even though
     * the map renderer can still display the placement.  Warsmash likewise
     * resolves doodads from `file` directly. */
    strlcpy(stem, file, sizeof(stem));
    dot = strrchr(stem, '.');
    if (dot && (!strcasecmp(dot, ".mdx") || !strcasecmp(dot, ".mdl")))
        *dot = '\0';

    if (row->numVar > 1) {
        DWORD const max_variation = (DWORD)row->numVar - 1;
        char suffix[16];
        snprintf(suffix, sizeof(suffix), "%u.mdx", MIN(variation, max_variation));
        if (strlen(stem) + strlen(suffix) >= sizeof(varied)) {
            fprintf(stderr, "WC3 doodad model path is too long for '%.4s': %s%s\n",
                    (LPCSTR)&row->id, stem, suffix);
            return;
        }
        strlcpy(varied, stem, sizeof(varied));
        strlcat(varied, suffix, sizeof(varied));
        if (SP_DoodadModelExists(varied)) {
            strlcpy(out, varied, out_size);
            return;
        }
        /* Some SLK rows advertise multiple variations even when a particular
         * suffixed file is absent.  Warcraft/Warsmash fall back to the base
         * model rather than registering a path that cannot be loaded. */
    }
    snprintf(out, out_size, "%s.mdx", stem);
}

static void SP_SpawnDoodad(LPEDICT edict) {
    Doodads_t const *row = edict->data.Doodads;
    PATHSTR buffer;

    SP_DoodadModelFilename(row, edict->variation, buffer, sizeof(buffer));
    edict->s.model = G_RegisterModel(buffer);
    edict->movetype = MOVETYPE_NONE;
    edict->svflags |= SVF_STATIC_SCENERY;
}

/* DestructableData may provide either a complete model stem (TFT/current
 * data) or the older dir + short file pair. As in Warsmash, a variation
 * suffix exists only when numVar > 1; appending "0" to a single-variation
 * bridge points the game-side animation loader at a file that is not in
 * War3.mpq even though the renderer can recover by stripping that digit. */
static void SP_DestructableModelFilename(DestructableData_t const *row,
                                         DWORD variation,
                                         LPSTR out,
                                         size_t out_size) {
    PATHSTR stem = { 0 };
    LPCSTR file;
    char *dot;

    if (!out || !out_size) return;
    out[0] = '\0';
    if (!row || !(file = row->file) || !*file) return;

    if (strchr(file, '\\') || strchr(file, '/'))
        strlcpy(stem, file, sizeof(stem));
    else if (row->dir && *row->dir)
        snprintf(stem, sizeof(stem), "%s\\%s\\%s", row->dir, file, file);
    else
        strlcpy(stem, file, sizeof(stem));

    dot = strrchr(stem, '.');
    if (dot && (!strcasecmp(dot, ".mdx") || !strcasecmp(dot, ".mdl")))
        *dot = '\0';

    if (row->numVar > 1) {
        DWORD const max_variation = (DWORD)row->numVar - 1;
        snprintf(out, out_size, "%s%u.mdx", stem, MIN(variation, max_variation));
    } else {
        snprintf(out, out_size, "%s.mdx", stem);
    }
}

static void SP_SpawnDestructable(LPEDICT edict) {
    DestructableData_t const *row = edict->data.DestructableData;
    LPCSTR path_tex = row->pathingTexture;
    FLOAT radius = row->radius;
    PATHSTR buffer;
    LPCSTR tex = row->textureFile;
    /* texFile may include an extension; "_" means the model has no replacement texture. */
    edict->s.image = tex && *tex && strcmp(tex, "_") ? gi.ImageIndex(tex) : 0;
    SP_DestructableModelFilename(row, edict->variation, buffer, sizeof(buffer));
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
    edict->data.UnitProfile = G_UnitProfile(edict->class_id);
    edict->data.UnitBalance = G_UnitBalance(edict->class_id);
    edict->data.UnitData = G_UnitData(edict->class_id);
    edict->data.UnitUI = G_UnitUI(edict->class_id);
    edict->data.UnitWeapons = G_UnitWeapons(edict->class_id);
    edict->data.UnitAbilities = G_UnitAbil(edict->class_id);
    edict->data.Doodads = G_Doodad(edict->class_id);
    edict->data.ItemData = G_ItemData(edict->class_id);
    edict->data.DestructableData = G_DestructableData(edict->class_id);
}

/* Install class-owned unit/destructable lifecycle callbacks. Load restores the saved C callbacks
 * through F_CFUNCTION; this helper is for spawn/tests that have class data but have not assigned
 * those pointers yet. */
void G_BindEntityRuntime(LPEDICT edict) {
    if (edict->data.DestructableData->file) {
        edict->stand = tree_stand; edict->birth = tree_birth; edict->pain = tree_pain; edict->die = tree_die;
        edict->think = monster_think;
    } else if (edict->data.UnitBalance->id || edict->data.UnitUI->modelFile) {
        edict->stand = unit_stand; edict->birth = unit_birth; edict->die = unit_die;
        edict->think = monster_think;
    }
}

void SP_CallSpawn(LPEDICT edict) {
    if (!edict->class_id)
        return;
    edict->s.class_id = edict->class_id;
    G_BindEntityData(edict);
    if (edict->data.Doodads->id) {
        SP_SpawnDoodad(edict);
    } else if (edict->data.DestructableData->file) {
        SP_SpawnDestructable(edict);
        SP_monster_tree(edict);
    } else if (edict->data.UnitUI->modelFile) {
        SP_SpawnUnit(edict);
        SP_monster_unit(edict);
    } else if (edict->data.ItemData->file) {
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
    LPCSTR name = player && player->playerName ? G_LevelString(player->playerName) : NULL;
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
    ps->name = (LPSTR)name;
    ps->start_location = player ? (LONG)playernum : -1;
    ps->stats[PLAYERSTATE_FOOD_CAP_CEILING] = (USHORT)MIN(MAX(0, game.constants.foodCeiling), USHRT_MAX);
    ps->stats[PLAYERSTATE_GOLD_UPKEEP_RATE] = 100;
    ps->stats[PLAYERSTATE_LUMBER_UPKEEP_RATE] = 100;
    ps->vieworigin = G_MakeServerOrigin(player ? player->startingPosition.x : 0.0f, player ? player->startingPosition.y : 0.0f, 0.0f);
    {
        gameCamera_t cam;
        CL_GameDefaultCamera(&cam);
        ps->viewangles = (VECTOR3){ cam.pitch, 0, cam.yaw };
        ps->distance = cam.distance;
        player_set_lens(ps, &cam);
        clent->client->camera.state.position = (VECTOR2){ ps->vieworigin.x, ps->vieworigin.y };
        clent->client->camera.state.viewangles = ps->viewangles;
        clent->client->camera.state.fov = cam.fov;
        clent->client->camera.state.target_distance = cam.distance;
        clent->client->camera.state.z_offset = 0.0f;
        clent->client->camera.state.near_z = cam.znear;
        clent->client->camera.state.far_z = cam.zfar;
    }
    clent->client->camera.target_height = ps->vieworigin.z;
    clent->client->camera.old_state = clent->client->camera.state;
    clent->client->camera.target_inherit_orientation = false;
    if (mapinfo) {
        FOR_LOOP(i, mapinfo->num_techAvailabilities) {
            mapTechAvailability_t const *tech = mapinfo->techAvailabilities + i;
            if (tech->playerFlags & (1u << playernum)) {
                G_SetPlayerTechMaxAllowed(clent->client, tech->techID, 0);
            }
        }
    }
    clent->client->mapplayer = player;
    clent->client->jass.controller = G_MapControl(player);
    clent->client->jass.race_pref = G_RacePreference(player);
    clent->client->jass.race_selectable = true;
    clent->client->jass.handicap = clent->client->jass.handicap_xp = 100.0f;
    strlcpy(clent->client->jass.name, name ? name : "", sizeof(clent->client->jass.name));
    ps->name = clent->client->jass.name;
}

void G_SpawnEntities(void) {
    LPCMAPINFO mapinfo = CM_GetMapInfo();
    LPCDOODAD entities = CM_GetDoodads();
    DWORD local_player = G_LocalMapPlayerNumber(mapinfo);
    LONG difficulty = 1;
    LPCSTR map_path = gi.CvarString("map", "");

    /* Map replacement must release script roots before level pointers are cleared. */
    G_BotShutdown();
    if (level.vm) { jass_close(level.vm); level.vm = NULL; }
    G_JassSoundRuntimeReset();
    G_ClearSaveRegistries();
    G_ClearJassGroupRegistry();
    G_ClearHashtableRegistry();
    G_FowShutdown();
    memset(&level, 0, sizeof(level));
    FOR_LOOP(i, MAX_PLAYERS) level.player_leaderboards[i] = -1;
    G_ResetStartingResourceCheat();
    level.time = gi.GetTime();

    level.mapinfo = mapinfo;
    G_EnvironmentFogInitMap();
    G_InitPlayerAlliances(mapinfo);
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
        strlcpy(level.setup.name, G_LevelString(mapinfo->mapName ? mapinfo->mapName : ""), sizeof(level.setup.name));
        strlcpy(level.setup.description, G_LevelString(mapinfo->mapDescription ? mapinfo->mapDescription : ""), sizeof(level.setup.description));
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
    if (mapinfo)
        G_SetCameraBounds(mapinfo->cameraBounds.bounds);
    G_WeatherInitMap();

    globals.num_edicts = game.max_clients;
    /* Quake II's body queue reserves real edicts before map entities, keeping all entity pointers in one address domain. */
    G_InitWaypoints();

    DWORD spawn_count = 0;
    FOR_EACH_LIST(DOODAD const, doodad, entities) {
        if ((spawn_count++ & 127u) == 0) gi.LoadingFrame();
        if (!G_LoadMapUnitData() && G_MapObjectCreatedByMapScript(doodad->doodID))
            continue;
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
        if ((S_GoldMineIsMine(ent) || S_GoldMineIsOverlay(ent)) && doodad->goldAmount != (DWORD)-1)
            ent->resources = doodad->goldAmount;
        if (G_IsDestructable(ent)) {
            G_InitializeDestructablePlacement(ent, doodad);
            G_RegisterGroundSurface(ent);
        }
        gi.LinkEntity(ent);
    }
    S_MineOverlayBindPreplaced();
    SP_worldspawn(NULL);
    
    jass_dofile(level.vm, "Scripts\\common.j");
    gi.LoadingFrame();
    jass_dofile(level.vm, "Scripts\\Blizzard.j");
    gi.LoadingFrame();
//    jass_dofilenative(level.vm, "/Users/igor/Desktop/war3map.j");
    G_DumpPrologue02BurrowHandoffSource(level.mapinfo->mapscript);
    if (level.mapinfo->mapscript)
        jass_dobuffer(level.vm, level.mapinfo->mapscript);
    else
        fprintf(stderr, "G_SpawnEntities: missing mapscript; skipping jass_dobuffer\n");
    gi.LoadingFrame();

    UI_Init();
    CM_BakeStaticObstacles();
    /* Start simulation from the map load itself so dedicated and listen-server restores share one lifecycle. */
    level.started = true;
}
 
/* Spawn a unit at a point while allowing map-restoration paths to skip presentation-only birth. */
static LPEDICT SP_SpawnAtLocationInternal(DWORD class_id, DWORD player, LPCVECTOR2 location, BOOL play_birth) {
    LPEDICT ent = G_Spawn();
    LPGAMECLIENT client;
    if (!ent) {
        return NULL;
    }
    ent->class_id = class_id;
    ent->s.class_id = class_id;
    ent->spawn_time = G_Time();
    ent->s.origin2 = *location;
    ent->s.origin.x = location->x;
    ent->s.origin.y = location->y;
    ent->s.origin.z = CM_GetHeightAtPoint(location->x, location->y);
    ent->s.scale = 1;
    ent->s.angle = -M_PI / 2;
    ent->s.player = player;
    SP_CallSpawn(ent);
    /* SP_SpawnUnit fills collision and the server broad-phase bounds depend on
     * that value. Link only after the class-owned spawn initializer runs. */
    gi.LinkEntity(ent);
    /* Dynamic unit creation must establish Hero progression independently of
     * presentation data.  SP_SpawnUnit already initializes normal Heroes, but
     * custom/minimal data may omit UnitUI/model rows while still defining Hero
     * attributes in UnitBalance.  The helper is idempotent, so applying it here
     * also keeps CreateUnit/training at Level 1 with one initial skill point. */
    if (G_UnitIsHero(ent)) {
        G_HeroInitializeProgression(ent);
    }
    if (play_birth && ent->birth) {
        ent->birth(ent);
    }
    client = G_GetPlayerClientByNumber(player);
    if ((ent->svflags & SVF_MONSTER) && client && client->ps.number == player) {
        G_InvalidateCommands(client);
        G_InvalidateUnitShortcutsForUnit(ent);
    }
    return ent;
}

LPEDICT SP_SpawnAtLocation(DWORD class_id, DWORD player, LPCVECTOR2 location) {
    return SP_SpawnAtLocationInternal(class_id, player, location, true);
}

LPEDICT SP_SpawnAtLocationNoBirth(DWORD class_id, DWORD player, LPCVECTOR2 location) {
    return SP_SpawnAtLocationInternal(class_id, player, location, false);
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
    ent->spawn_time = G_Time();
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
    BYTE const blocked_flags = M_UnitStaticPathingFlags(unit);
    if (!unit || !point) {
        return false;
    }
    if (!CM_PointIsPathableForRadiusFlags(point, unit->collision, blocked_flags)) {
        return false;
    }

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT other = &globals.edicts[i];
        VECTOR2 delta;

        if (other == unit || IS_HOLLOW(other) || other->movetype == MOVETYPE_NONE || other->collision <= 0.0f) {
            continue;
        }
        if (!!(other->aiflags & AI_FLYING) != !!(unit->aiflags & AI_FLYING)) {
            continue;
        }
        delta = Vector2_sub(&other->s.origin2, point);
        if (Vector2_len(&delta) < unit->collision + other->collision) {
            return false;
        }
    }
    return true;
}

static BOOL G_CanRepositionUnitAt(LPEDICT unit, LPCVECTOR2 point) {
    BYTE const blocked_flags = M_UnitStaticPathingFlags(unit);
    if (!unit || !point) {
        return false;
    }
    if (!CM_PointIsPathableForRadiusFlags(point, unit->collision, blocked_flags)) {
        return false;
    }

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT other = &globals.edicts[i];
        VECTOR2 delta;

        /* A carried item is intentionally released at its carrier's feet; the
         * carrier must not make the item appear blocked before it becomes a
         * world entity. */
        if (other == unit || (G_IsItem(unit) && other == unit->item.carrier) ||
            IS_HOLLOW(other) || other->collision <= 0.0f) {
            continue;
        }
        if (!!(other->aiflags & AI_FLYING) != !!(unit->aiflags & AI_FLYING)) {
            continue;
        }
        delta = Vector2_sub(&other->s.origin2, point);
        if (Vector2_len(&delta) < unit->collision + other->collision) {
            return false;
        }
    }
    return true;
}

/* Warcraft III SetUnitPosition is not the raw X/Y setter. Warsmash models the
 * native through CUnit.setPointAndCheckUnstuck(): test the requested point,
 * then walk a deterministic 64-world-unit square spiral for at most 300
 * candidates. Keep the requested point as the fallback when no candidate is
 * legal, matching Warsmash's outputX/outputY initialization. */
BOOL G_FindUnitUnstuckPosition(LPEDICT unit, LPCVECTOR2 requested, LPVECTOR2 out) {
    int check_x = 0, check_y = 0;

    if (!unit || !requested || !out) {
        return false;
    }
    *out = *requested;
    for (int i = 0; i < 300; i++) {
        VECTOR2 const candidate = {
            requested->x + check_x * 64.0f,
            requested->y + check_y * 64.0f,
        };
        int const phase = ((int)floor(sqrt((double)(4 * i + 1)))) % 4;

        if (G_CanRepositionUnitAt(unit, &candidate)) {
            *out = candidate;
            return true;
        }

        /* Equivalent to Warsmash's cardinal cos/sin update, without relying
         * on floating-point truncation around PI/2 and 3*PI/2. */
        switch (phase) {
        case 0: check_x--; break;
        case 1: check_y--; break;
        case 2: check_x++; break;
        default: check_y++; break;
        }
    }
    return false;
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

    /* MOVETYPE_NONE buildings are omitted from the dynamic collision scan;
     * their authored pathing footprint still owns the space the new unit
     * must clear before it becomes visible. */
    if (CM_DistanceToPathingFootprint(ctx->producer, &candidate) < ctx->unit->collision)
        return false;
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

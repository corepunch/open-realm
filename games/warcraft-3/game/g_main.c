/*
 * g_main.c — Game library entry point and main simulation loop.
 *
 * This file implements the game_export interface consumed by the server
 * (sv_game.c).  GetGameAPI() is called once at startup and returns a
 * vtable of function pointers used by the server to drive the game.
 *
 * Key callbacks:
 *   Init        — allocates entity pool, loads config/unit data tables.
 *   LoadMap     — loads a map and spawns all its entities.
 *   RunFrame    — called once per server frame; runs events, client camera
 *                 interpolation, entity physics/AI, and collision resolution.
 *   ClientBegin — called when a client finishes connecting; sends the initial
 *                 UI layout (svc_layout) and tallies food counts.
 *   ClientCommand — routes player commands to the skills system.
 *
 * G_RunFrame() is the inner loop:
 *   1. G_RunEvents()      — dispatch queued game events to triggers.
 *   2. G_RunClients()     — interpolate camera positions for smooth panning.
 *   3. G_RunEntities()    — call G_RunEntity() on every live entity.
 *   4. G_SolveCollisions() — resolve entity overlaps (g_phys.c).
 */
#include "common/common.h"
#include "g_local.h"
#include "jass/jass.h"

struct game_export globals;
struct game_import gi;
struct game_locals game;
struct level_locals level;
struct edict_s *g_edicts;

extern JASSMODULE jass_funcs[];

static bool G_LoadMap(LPCSTR mapFilename) {
    if (!CM_LoadMap(mapFilename)) {
        return false;
    }
    if (gi.ApplyLobbySettings) {
        gi.ApplyLobbySettings((LPMAPINFO)CM_GetMapInfo());
    }
    if (gi.ClearWorld) {
        gi.ClearWorld();
    }
    G_SpawnEntities();
    return true;
}

LPCSTR miscdata_files[] = {
    "UI\\MiscData.txt",
    "Units\\MiscData.txt",
    "Units\\MiscGame.txt",
    "UI\\MiscUI.txt",
    "UI\\SoundInfo\\MiscData.txt",
    "war3mapMisc.txt",
    NULL
};

static void InitMiscValue(LPCSTR name, FLOAT *dest) {
    LPCSTR strvalue = Stb_IniCacheFind(&game.config.misc, "Misc", name);
    *dest = strvalue ? atof(strvalue) : 0;
}

static void InitConstants(void) {
    Stb_IniCacheLoadFiles(&game.config.misc, miscdata_files);
    InitMiscValue("AttackHalfAngle", &game.constants.attackHalfAngle);
    InitMiscValue("MaxCollisionRadius", &game.constants.maxCollisionRadius);
    InitMiscValue("DecayTime", &game.constants.decayTime);
    InitMiscValue("BoneDecayTime", &game.constants.boneDecayTime);
    InitMiscValue("DissipateTime", &game.constants.dissipateTime);
    InitMiscValue("StructureDecayTime", &game.constants.structureDecayTime);
    InitMiscValue("BulletDeathTime", &game.constants.bulletDeathTime);
    InitMiscValue("CloseEnoughRange", &game.constants.closeEnoughRange);
    InitMiscValue("Dawn", &game.constants.dawnTimeGameHours);
    InitMiscValue("Dusk", &game.constants.duskTimeGameHours);
    InitMiscValue("DayHours", &game.constants.gameDayHours);
    InitMiscValue("DayLength", &game.constants.gameDayLength);
    InitMiscValue("BuildingAngle", &game.constants.buildingAngle);
    InitMiscValue("RootAngle", &game.constants.rootAngle);
}

/* -------------------------------------------------------------------------
 * In-game JASS test runner.
 *
 * Activated by passing +set jass_test <script.j> on the command line.
 * Optionally specify the entrypoint with +set jass_test_entry <function>.
 * The game binary exits 0 on success, 1 on any assertion failure.
 *
 * Example:
 *   openwarcraft3 -data <dir> +set jass_test games/warcraft-3/tests/fixtures/test_jass_assertions.j
 * ------------------------------------------------------------------------- */
static void G_RunJassTests(LPCSTR script, LPCSTR entry) {
    if (!entry || !*entry) {
        entry = "run_tests";
    }
    fprintf(stderr, "JASS test mode: script=%s entry=%s\n", script, entry);

    jass_sethost(&MAKE(JASSHOST,
        .MemAlloc           = gi.MemAlloc,
        .MemFree            = gi.MemFree,
        .GetTime            = gi.GetTime,
        .ReadFile = gi.ReadFile,
        .natives            = jass_funcs,
        .GetPlayerByNumber  = G_GetPlayerByNumber,
    ));

    LPJASS j = jass_newstate();
    if (!jass_dofile(j, script)) {
        fprintf(stderr, "JASS test error: could not load '%s'\n", script);
        jass_close(j);
        exit(1);
    }

    jass_callbyname(j, entry, true);
    /* Pump coroutines until all finish (no timer advancement needed for immediate tests). */
    jass_runevents(j);

    BOOL failed = jass_rterror_pending(j);
    if (failed) {
        fprintf(stderr, "JASS test FAILED: %s\n", jass_rterror_message(j));
    } else {
        fprintf(stderr, "JASS test PASSED\n");
    }
    jass_close(j);
    exit(failed ? 1 : 0);
}

static void G_InitGame(void) {
    if (gi.CvarString) {
        LPCSTR jass_test = gi.CvarString("jass_test", "");
        if (jass_test && *jass_test) {
            LPCSTR jass_entry = gi.CvarString("jass_test_entry", "");
            G_RunJassTests(jass_test, jass_entry);
            /* G_RunJassTests always calls exit() */
        }
    }

    fprintf(stderr, "Game initialization.\n");
    fprintf(stderr, "Game is starting up.\n");
    fprintf(stderr, "Game is openwarcraft3 built on %s.\n", __DATE__);

    g_edicts = gi.MemAlloc(sizeof(edict_t) * MAX_ENTITIES);
    memset(g_edicts, 0, sizeof(edict_t) * MAX_ENTITIES);
    
    globals.edicts = g_edicts;
    globals.max_edicts = MAX_ENTITIES;
    globals.max_clients = MAX_CLIENTS;
    globals.num_edicts = globals.max_clients;
    FOR_LOOP(i, globals.max_clients) {
        g_edicts[i].s.number = i;
    }

    game.max_clients = globals.max_clients;
    game.clients = gi.MemAlloc(game.max_clients * sizeof(GAMECLIENT));
    Stb_IniCacheLoad(&game.config.theme, "UI\\war3skins.txt");
    InitConstants();
    InitUnitData();
    InitAbilities();
    G_RegisterGlobalSounds();
    fprintf(stderr, "Game initialized.\n\n");
}

static void G_ShutdownGame(void) {
    if (g_edicts == NULL) {
        return;
    }
    G_FowShutdown();
    G_FreeModels();
    gi.MemFree(g_edicts);
    g_edicts = NULL;
    globals.edicts = NULL;
    globals.num_edicts = 0;

    ShutdownUnitData();
    Stb_IniCacheFree(&game.config.theme); Stb_IniCacheFree(&game.config.misc);
    SAFE_DELETE(game.clients, gi.MemFree);
}

FLOAT G_Cinefade(void) {
    if (G_SkipCutscene()) {
        return 0;
    }
    DWORD duration = level.cinefilter.end.time - level.cinefilter.start.time;
    if (!level.cinefilter.displayed) {
        return 0;
    }
    if (!duration || gi.GetTime() > level.cinefilter.end.time) {
        return level.cinefilter.end.color.a / 255.0;
    } else {
        FLOAT k = (gi.GetTime() - level.cinefilter.start.time) / (FLOAT)duration;
        return LerpNumber(level.cinefilter.start.color.a, level.cinefilter.end.color.a, k) / 255.0;
    }
}

BOOL G_SkipCutscene(void) {
    LPCSTR value;

    if (!gi.CvarString) {
        return false;
    }
    value = gi.CvarString("skip_cutscene", "0");
    return value && *value && strcmp(value, "0");
}

VECTOR2 G_ClampCameraPosition(LPGAMECLIENT client, LPCVECTOR2 position) {
    VECTOR2 clamped = position ? *position : (VECTOR2){ 0, 0 };

    if (!client || !position) {
        return clamped;
    }
    if (client->ps.camera_bounds.max.x > client->ps.camera_bounds.min.x) {
        clamped.x = MAX(client->ps.camera_bounds.min.x,
                        MIN(client->ps.camera_bounds.max.x, clamped.x));
    }
    if (client->ps.camera_bounds.max.y > client->ps.camera_bounds.min.y) {
        clamped.y = MAX(client->ps.camera_bounds.min.y,
                        MIN(client->ps.camera_bounds.max.y, clamped.y));
    }
    return clamped;
}

void G_SetClientCameraBounds(LPGAMECLIENT client, FLOAT const bounds[8]) {
    VECTOR2 position;

    if (!client || !bounds) {
        return;
    }

    client->ps.camera_bounds.min.x = MIN(MIN(bounds[0], bounds[2]), MIN(bounds[4], bounds[6]));
    client->ps.camera_bounds.max.x = MAX(MAX(bounds[0], bounds[2]), MAX(bounds[4], bounds[6]));
    client->ps.camera_bounds.min.y = MIN(MIN(bounds[1], bounds[3]), MIN(bounds[5], bounds[7]));
    client->ps.camera_bounds.max.y = MAX(MAX(bounds[1], bounds[3]), MAX(bounds[5], bounds[7]));

    position = G_ClampCameraPosition(client, &client->ps.origin);
    client->ps.origin = position;
    position = G_ClampCameraPosition(client, &client->camera.old_state.position);
    client->camera.old_state.position = position;
    position = G_ClampCameraPosition(client, &client->camera.state.position);
    client->camera.state.position = position;
}

void G_ClearCameraTarget(LPGAMECLIENT client, LPCSTR func) {
    (void)func;
    if (!client || !client->camera.target_controller) {
        return;
    }
    client->camera.target_controller = NULL;
    client->camera.target_offset = (VECTOR2){ 0, 0 };
}

static void G_UpdateCameraTarget(LPGAMECLIENT client) {
    LPEDICT target = client->camera.target_controller;
    VECTOR2 position;

    if (!target) {
        return;
    }
    if (!target->inuse) {
        G_ClearCameraTarget(client, "G_UpdateCameraTarget");
        return;
    }
    position.x = target->s.origin2.x + client->camera.target_offset.x;
    position.y = target->s.origin2.y + client->camera.target_offset.y;
    position = G_ClampCameraPosition(client, &position);
    client->camera.old_state.position = position;
    client->camera.state.position = position;
    client->camera.start_time = gi.GetTime();
    client->camera.end_time = client->camera.start_time;
}

static void G_RunClients(void) {
    FLOAT cinefade = G_Cinefade();
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = game.clients+i;
        DWORD duration;
        G_UpdateCameraTarget(client);
        duration = client->camera.end_time - client->camera.start_time;
        if (gi.GetTime() < client->camera.end_time && duration > 0) {
            FLOAT k = (gi.GetTime() - client->camera.start_time) / (FLOAT)duration;
            LPCCAMERASETUP a = &client->camera.old_state;
            LPCCAMERASETUP b = &client->camera.state;
            QUATERNION qa = Quaternion_fromEuler(&a->viewangles, ROTATE_ZYX);
            QUATERNION qb = Quaternion_fromEuler(&b->viewangles, ROTATE_ZYX);
            client->ps.origin = Vector2_lerp(&a->position, &b->position, k);
            client->ps.viewquat = Quaternion_slerp(&qa, &qb, k);
            client->ps.fov = LerpNumber(a->fov, b->fov, k);
            client->ps.distance = LerpNumber(a->target_distance, b->target_distance, k);
        } else {
            client->ps.origin = client->camera.state.position;
            client->ps.viewquat = Quaternion_fromEuler(&client->camera.state.viewangles, ROTATE_ZYX);
            client->ps.fov = client->camera.state.fov;
            client->ps.distance = client->camera.state.target_distance;
        }
        /* Auto-clear cinematic scene when its duration expires.
         * Blizzard.j TransmissionFromUnitWithNameBJ never calls
         * EndCinematicScene; the original WC3 engine times out via the
         * sceneDuration parameter of SetCinematicScene. */
        if (client->cinematic_end_time && gi.GetTime() >= client->cinematic_end_time) {
            G_SetPlayerText(client, PLAYERTEXT_SPEAKER, "");
            G_SetPlayerText(client, PLAYERTEXT_DIALOGUE, "");
            client->ps.cinematic_portrait = 0;
            client->cinematic_end_time = 0;
            UI_WriteCinematicLayer(G_GetPlayerEntityByNumber(client->ps.number));
        }
        client->ps.cinefade = cinefade;
    }
}

void G_InvalidateCommands(LPGAMECLIENT client) {
    if (client) client->commands_dirty = true;
}

static void G_UpdateClientCommandCards(void) {
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = game.clients + i;
        LPEDICT clent;

        if (!client->connected || !client->commands_dirty) continue;
        if (client->menu.on_entity_selected || client->menu.on_location_selected) continue;
        clent = G_GetPlayerEntityByNumber(client->ps.number);
        if (!clent || clent->client != client) continue;
        if (client->menu.refresh) {
            client->commands_dirty = false;
            client->menu.refresh(clent);
        } else {
            Get_Commands_f(clent);
        }
    }
}

static void G_StartScripts(void) {
    if (level.scriptsStarted) {
        return;
    }

    /*
     * war3map.doo objects already exist in OpenRealm before generated
     * war3map.j main() runs. During this initial execution only,
     * CreateDestructable() may rebind generated gg_dest_* handles to those
     * preplaced instances.
     */
    G_SetDestructableScriptBinding(true);

    jass_callbyname(level.vm, "main", true);
    level.scriptsStarted = true;
    jass_runevents(level.vm);

    G_SetDestructableScriptBinding(false);
}

/* One complete server-frame simulation step.
 * Skipped until the first map has been started; on the very first frame after
 * a map loads, the JASS "main" function is invoked to run map initialization
 * triggers. */
static void G_RunFrame(void) {
    /* extern DWORD g_heatmap_builds_this_frame; */ /* unused for now */

    if (!level.started)
        return;

    G_StartScripts();
    
    G_RunEvents();
    jass_runevents(level.vm);

    G_RunClients();

    G_RunEntities();

    G_UpdateClientCommandCards();

    G_UpdateClientInfoPanels();
    G_UpdateClientResourceBars();

    G_SolveCollisions();
    G_FowUpdate();
    G_FowSendDeltas();
}

static LPCSTR G_GetThemeValue(LPCSTR filename) {
    LPCSTR skinned = NULL;
    if (!strstr(filename, "\\")) {
        skinned = Stb_IniCacheFind(&game.config.theme, "Default", filename);
    }
    return skinned ? skinned : filename;
}

LPEDICT G_GetPlayerEntityByNumber(DWORD number) {
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = g_edicts+i;
        if (ent->client && ent->client->ps.number == number) {
            return ent;
        }
    }
    return NULL;
}

LPGAMECLIENT G_GetPlayerClientByNumber(DWORD number) {
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT cl = game.clients+i;
        if (cl->ps.number == number) {
            return cl;
        }
    }
    return &game.clients[MAX_PLAYERS-1];
//    return NULL;
}

LPPLAYER G_GetPlayerByNumber(DWORD number) {
    FOR_LOOP(i, game.max_clients) {
        if (game.clients[i].ps.number == number) {
            return &game.clients[i].ps;
        }
    }
    return &game.clients[MAX_PLAYERS-1].ps;
//    return NULL;
}

GAMEEVENT *G_PublishEventWithSource(LPEDICT edict, EVENTTYPE type, LPEDICT source) {
    DWORD index = level.events.write++;
    GAMEEVENT *evt = &level.events.queue[index % MAX_EVENT_QUEUE];

    memset(evt, 0, sizeof(*evt));
    evt->type = type;
    evt->edict = edict;
    evt->source = source;
    return evt;
}

GAMEEVENT *G_PublishEvent(LPEDICT edict, EVENTTYPE type) {
    return G_PublishEventWithSource(edict, type, NULL);
}

/* Gameplay messages expose state-machine transitions without turning internal
 * engine flow into Warcraft/JASS events or retaining entity pointers. */
BOOL G_SubscribeMessage(gameMsgFn fn, void *ctx) {
    FOR_LOOP(i, MAX_MESSAGE_SUBSCRIBERS) {
        GAMEMSGSUB *sub = &level.messages.subs[i];
        if (sub->fn == fn && sub->ctx == ctx)
            return true;
        if (!sub->fn) {
            sub->fn = fn; sub->ctx = ctx;
            return true;
        }
    }
    fprintf(stderr, "G_SubscribeMessage: subscriber limit %d reached\n", MAX_MESSAGE_SUBSCRIBERS);
    return false;
}

/* Tests and tools unsubscribe explicitly so later state transitions cannot
 * call a callback whose capture storage has left scope. */
void G_UnsubscribeMessage(gameMsgFn fn, void *ctx) {
    FOR_LOOP(i, MAX_MESSAGE_SUBSCRIBERS) {
        GAMEMSGSUB *sub = &level.messages.subs[i];
        if (sub->fn == fn && sub->ctx == ctx) {
            memset(sub, 0, sizeof(*sub));
            return;
        }
    }
}

/* Synchronous delivery preserves the exact transition order and copies stable
 * entity numbers, so subscribers never depend on edict lifetime. */
void G_PublishMessage(LPEDICT actor, GAMEMSGTYPE type, LPEDICT target) {
    GAMEMSG msg = { type, actor->s.number, target->s.number };
    FOR_LOOP(i, MAX_MESSAGE_SUBSCRIBERS) {
        GAMEMSGSUB const *sub = &level.messages.subs[i];
        if (sub->fn)
            sub->fn(&msg, sub->ctx);
    }
}

LPCSTR G_LevelString(LPCSTR name) {
    unsigned int string_id;
    char trailing;

    if (!name || strncmp(name, "TRIGSTR_", 8) ||
        sscanf(name, "TRIGSTR_%u%c", &string_id, &trailing) != 1 ||
        !level.mapinfo) {
        return name;
    }
    FOR_EACH_LIST(mapTrigStr_t, trigstr, level.mapinfo->strings) {
        if (trigstr->id == (DWORD)string_id) {
            return trigstr->text;
        }
    }
    return name;
}

void G_SetClientConnected(LPEDICT player, BOOL connected) {
    player->client->connected = connected;
}

/* Called when a client finishes the connection handshake and is ready to play.
 * The in-game HUD is server-authored through svc_layout; this binds the game
 * client and initializes gameplay state when a map is loaded. */
static void G_ClientBegin(LPEDICT edict) {
    LPGAMECLIENT client = edict->client ? edict->client : game.clients;
    if (!edict->client) {
        edict->client = client;
    }

    G_SetClientConnected(edict, true);
    client->ps.client_ui_state = CLIENT_UI_GAME;
    if (!client->mapplayer) {
        client->ps.origin = (VECTOR2){ 0, 0 };
    }
    fprintf(stderr,
            "G_ClientBegin: edict=%u player=%u team=%u race=%u color=%u start_location=%ld origin=(%.1f %.1f) name=\"%s\"\n",
            (unsigned)(edict - globals.edicts),
            (unsigned)client->ps.number,
            (unsigned)client->ps.team,
            (unsigned)client->ps.race,
            (unsigned)client->ps.color,
            (long)client->ps.start_location,
            client->ps.origin.x,
            client->ps.origin.y,
            client->ps.name ? client->ps.name : "");
    level.started = true;
    G_StartScripts();

    UI_ShowGameInterface(edict);

    FILTER_EDICTS(ent, client->ps.number == ent->s.player) {
        UnitBalance_t const *b = ent->UnitBalance;
        client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] += b->foodMade;
        client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] += b->foodUsed;
    }
    /* Invalidate cache so the initial resource bar write always fires. */
    client->resourcebar.gold = -1;
    G_RefreshResourceBar(edict);
    Get_Portrait_f(edict);
    Get_Commands_f(edict);

    G_FowConnectPlayer(client->ps.number);
    G_FowUpdate();
    G_FowSendFull(edict);
}

/* Selection voices are local feedback; suppress them in snapshots for clients
 * that did not select this entity while leaving world sounds unchanged. */
static void G_CustomizeEntity(DWORD player, LPCEDICT ent, LPENTITYSTATE state) {
    BOOL const hoverable = (ent->svflags & SVF_MONSTER) &&
        !(ent->svflags & SVF_DEADMONSTER) &&
        ent->health.value > 0.0f &&
        !(state->flags & EF_NOT_SELECTABLE) &&
        G_FowPlayerCanHoverEntity(player, ent);

    state->flags &= ~(EF_HOVER_HEALTH | EF_HOSTILE | EF_NEUTRAL);
    if (hoverable) {
        DWORD const owner = ent->s.player;
        BOOL const valid_players = player < MAX_PLAYERS && owner < MAX_PLAYERS;
        BOOL const passive_ally = valid_players &&
            (level.alliances[player][owner] & (1 << ALLIANCE_PASSIVE));
        BOOL const shared_control = valid_players &&
            (level.alliances[player][owner] & (1 << ALLIANCE_SHARED_CONTROL));

        state->flags |= EF_HOVER_HEALTH;
        if (owner != player && !passive_ally) {
            state->flags |= EF_HOSTILE;
        } else if (owner != player && !shared_control) {
            state->flags |= EF_NEUTRAL;
        }
    }

    if (state->event == EV_ACK && !(ent->selected & (1 << player))) {
        state->event = EV_NONE;
        state->sound = 0;
    }
}

/* Return the game API vtable to the server.
 * Called once at startup; after this point the server drives the game
 * exclusively through the returned function pointers. */
struct game_export *GetGameAPI(struct game_import *import) {
    gi = *import;
    FS_SetSheetHost(&MAKE(SHEETHOST,
        .ReadFile = gi.ReadFile,
        .FreeFile = (void (*)(HANDLE))gi.MemFree,
        .MemAlloc = gi.MemAlloc,
        .MemFree = gi.MemFree,
    ));
    globals.Init = G_InitGame;
    globals.Shutdown = G_ShutdownGame;
    globals.RunFrame = G_RunFrame;
    globals.ClientCommand = G_ClientCommand;
    globals.ClientSetCameraPosition = G_ClientSetCameraPosition;
    globals.ClientBegin = G_ClientBegin;
    globals.CanSeeEntity = G_FowPlayerCanSeeEntity;
    globals.CustomizeEntity = G_CustomizeEntity;
    globals.GetThemeValue = G_GetThemeValue;
    globals.LoadMap = G_LoadMap;
    globals.GetWorldBounds = CM_GetWorldBounds;
    globals.edict_size = sizeof(struct edict_s);
    return &globals;
}

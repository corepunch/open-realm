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
 *   1. Sync level.time and advance the Warcraft time-of-day clock.
 *   2. G_RunTimers()      — publish expired timer events.
 *   3. G_RunEvents()      — dispatch queued game events to triggers.
 *   4. G_RunClients()     — interpolate camera positions for smooth panning.
 *   5. G_RunEntities()    — call G_RunEntity() on every live entity.
 *   6. G_SolveCollisions() — resolve entity overlaps (g_phys.c).
 */
#include "common/common.h"
#include "g_local.h"
#include "jass/jass.h"
#include <stdarg.h>

struct game_export globals;
struct game_import gi;
struct game_locals game;
struct level_locals level;
struct edict_s *g_edicts;

extern JASSMODULE jass_funcs[];

static void G_StartScripts(void);

static BOOL G_TimeLimitMatches(DWORD op, FLOAT value, FLOAT limit) {
    switch (op) {
        case WC3_LIMITOP_LESS_THAN: return value < limit;
        case WC3_LIMITOP_LESS_THAN_OR_EQUAL: return value <= limit;
        case WC3_LIMITOP_EQUAL: return value == limit;
        case WC3_LIMITOP_GREATER_THAN_OR_EQUAL: return value >= limit;
        case WC3_LIMITOP_GREATER_THAN: return value > limit;
        case WC3_LIMITOP_NOT_EQUAL: return value != limit;
        default: return false;
    }
}

FLOAT G_GetTimeOfDay(void) {
    FLOAT const day_hours = game.constants.gameDayHours;
    FLOAT const day_length = game.constants.gameDayLength;

    if (day_hours <= 0.0f || day_length <= 0.0f)
        return 0.0f;
    return (level.timeofday.elapsed / day_length) * day_hours;
}

void G_SetTimeOfDay(FLOAT value) {
    level.timeofday.pending = value;
    level.timeofday.pending_valid = true;
}

void G_SuspendTimeOfDay(BOOL suspended) {
    level.timeofday.suspended = suspended;
}

static void G_CheckTimeOfDayEvents(FLOAT before, FLOAT after) {
    FOR_EACH_LIST(EVENT, evt, level.events.handlers) {
        if (evt->type != EVENT_GAME_STATE_LIMIT || evt->state != WC3_GAME_STATE_TIME_OF_DAY)
            continue;
        if (!G_TimeLimitMatches(evt->limitop, before, evt->limitval) &&
            G_TimeLimitMatches(evt->limitop, after, evt->limitval))
        {
            G_PublishEvent(NULL, EVENT_GAME_STATE_LIMIT)->responseTo = evt;
        }
    }
}

/* Warcraft owns one simulation clock for gameplay time of day. Misc.Dawn,
 * Dusk, DayHours and DayLength define its scale; presentation systems should
 * consume G_GetTimeOfDay() rather than maintain an independent timer. */
void G_UpdateTimeOfDay(void) {
    FLOAT const day_hours = game.constants.gameDayHours;
    FLOAT const day_length = game.constants.gameDayLength;
    FLOAT before, after;

    if (day_hours <= 0.0f || day_length <= 0.0f)
        return;

    before = G_GetTimeOfDay();
    if (level.timeofday.pending_valid) {
        level.timeofday.elapsed =
            (level.timeofday.pending / day_hours) * day_length;
        level.timeofday.pending_valid = false;
    } else if (!level.timeofday.suspended) {
        level.timeofday.elapsed = fmodf(
            level.timeofday.elapsed + (FLOAT)FRAMETIME / 1000.0f,
            day_length);
    }
    after = G_GetTimeOfDay();
    G_CheckTimeOfDayEvents(before, after);
}

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
    /* SV_Map already wiped CS_IMAGES/CS_FONTS. Clear hud, then bind every
     * panel once so write paths do not parse FDF on first use. */
    UI_ResetHud();
    UI_LoadHud();
    G_SpawnEntities();
    strlcpy(level.map_path, mapFilename, sizeof(level.map_path));
    G_StartScripts();
    level.started = true;
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

static void InitMiscValueDefault(LPCSTR name, FLOAT *dest, FLOAT fallback) {
    LPCSTR strvalue = Stb_IniCacheFind(&game.config.misc, "Misc", name);
    /* BZ_HARDCODED_DATA_FALLBACK: stock WC3 1.29 defaults are used only when
     * the authoritative MiscGame field is absent from the active data set. */
    *dest = strvalue && *strvalue ? (FLOAT)atof(strvalue) : fallback;
}

static DWORD InitMiscList(LPCSTR name, FLOAT *dest, DWORD capacity) {
    LPCSTR value = Stb_IniCacheFind(&game.config.misc, "Misc", name);
    DWORD count = 0;

    if (!value || !*value) return 0;
    while (*value && count < capacity) {
        char *end = NULL;
        while (*value == ' ' || *value == '\t') value++;
        dest[count] = strtof(value, &end);
        if (!end || end == value) {
            fprintf(stderr, "Invalid Misc.%s list near '%s'\n", name, value);
            break;
        }
        count++;
        value = end;
        while (*value == ' ' || *value == '\t') value++;
        if (*value == ',') {
            value++;
        } else if (*value) {
            fprintf(stderr, "Invalid Misc.%s separator near '%s'\n", name, value);
            break;
        }
    }
    return count;
}

static void InitConstants(void) {
    static FLOAT const default_damage_bonus[8][8] = {
        /* BZ_HARDCODED_DATA_FALLBACK: WC3 1.29 MiscGame defaults. */
        { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f }, /* unknown */
        { 1.00f, 1.50f, 1.00f, 0.70f, 1.00f, 1.00f, 0.05f, 1.00f }, /* normal  */
        { 2.00f, 0.75f, 1.00f, 0.35f, 1.00f, 0.50f, 0.05f, 1.50f }, /* pierce  */
        { 1.00f, 0.50f, 1.00f, 1.50f, 1.00f, 0.50f, 0.05f, 1.50f }, /* siege   */
        { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.70f, 0.05f, 1.00f }, /* spells  */
        { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f }, /* chaos   */
        { 1.25f, 0.75f, 2.00f, 0.35f, 1.00f, 0.50f, 0.05f, 1.00f }, /* magic   */
        { 1.00f, 1.00f, 1.00f, 0.50f, 1.00f, 1.00f, 0.05f, 1.00f }, /* hero    */
    };
    static struct { DWORD type; LPCSTR key; } const damage_rows[] = {
        { ATK_NORMAL, "DamageBonusNormal" },
        { ATK_PIERCE, "DamageBonusPierce" },
        { ATK_SIEGE,  "DamageBonusSiege"  },
        { ATK_CHAOS,  "DamageBonusChaos"  },
        { ATK_MAGIC,  "DamageBonusMagic"  },
        { ATK_HERO,   "DamageBonusHero"   },
    };
    FLOAT food_ceiling;
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

    memcpy(game.constants.damageBonus, default_damage_bonus, sizeof(default_damage_bonus));
    FOR_LOOP(i, sizeof(damage_rows) / sizeof(damage_rows[0])) {
        DWORD const row = damage_rows[i].type;
        InitMiscList(damage_rows[i].key, game.constants.damageBonus[row], 8);
    }
    /* Warsmash falls SPELLS back to the active Magic row when a dedicated
     * DamageBonusSpells field is absent. */
    if (!InitMiscList("DamageBonusSpells", game.constants.damageBonus[ATK_SPELLS], 8)) {
        memcpy(game.constants.damageBonus[ATK_SPELLS], game.constants.damageBonus[ATK_MAGIC],
               sizeof(game.constants.damageBonus[ATK_SPELLS]));
    }
    InitMiscValueDefault("DefenseArmor", &game.constants.defenseArmor, 0.06f);
    InitMiscValueDefault("StrAttackBonus", &game.constants.strAttackBonus, 1.0f);
    InitMiscValueDefault("AgiDefenseBonus", &game.constants.agiDefenseBonus, 0.3f);
    InitMiscValueDefault("AgiAttackSpeedBonus", &game.constants.agiAttackSpeedBonus, 0.02f);
    game.constants.combatConstantsLoaded = true;

    InitMiscValue("FoodCeiling", &food_ceiling);
    game.constants.foodCeiling = MAX(0, (LONG)food_ceiling);
    game.constants.upkeepUsageCount = InitMiscList("UpkeepUsage", game.constants.upkeepUsage, MAX_UPKEEP_TIERS);
    game.constants.upkeepGoldTaxCount = InitMiscList("UpkeepGoldTax", game.constants.upkeepGoldTax, MAX_UPKEEP_TIERS);
    game.constants.upkeepLumberTaxCount = InitMiscList("UpkeepLumberTax", game.constants.upkeepLumberTax, MAX_UPKEEP_TIERS);
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
    UI_ResetHud();
    fprintf(stderr, "Game initialized.\n\n");
}

static void G_ShutdownGame(void) {
    if (g_edicts == NULL) {
        return;
    }
    UI_ResetHud();
    gi.SetPaused(false);
    G_BotShutdown();
    if (level.vm) { jass_close(level.vm); level.vm = NULL; }
    G_FowShutdown();
    G_FreeModels();
    FOR_LOOP(i, globals.max_edicts) G_FreeActorSkills(g_edicts + i);
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
        LPEDICT client_ent = G_GetPlayerEntityByNumber(client->ps.number);
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
        /* Transmission scene and voice lifetimes are independent. Blizzard.j
         * keeps the portrait scene alive past the voice, so Portrait Talk must
         * fall back to Portrait before the entire transmission disappears. */
        if (client->cinematic_end_time && gi.GetTime() >= client->cinematic_end_time) {
            G_SetPlayerText(client, PLAYERTEXT_SPEAKER, "");
            G_SetPlayerText(client, PLAYERTEXT_DIALOGUE, "");
            client->ps.cinematic_portrait = 0;
            client->cinematic_end_time = 0;
            client->cinematic_voice_end_time = 0;
            client->presentation_dirty = true;
        } else if (client->cinematic_voice_end_time && gi.GetTime() >= client->cinematic_voice_end_time) {
            client->cinematic_voice_end_time = 0;
            client->presentation_dirty = true;
        }
        if (client->message.end_time && gi.GetTime() >= client->message.end_time) {
            memset(&client->message, 0, sizeof(client->message));
            client->presentation_dirty = true;
        }
        if (client->connected && client->presentation_dirty && client_ent) {
            UI_WriteDialoguePresentation(client_ent);
            client->presentation_dirty = false;
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

BOOL G_IsSinglePlayer(void) {
    DWORD humans = 0;

    if (!level.mapinfo) return true;
    FOR_LOOP(i, MAX_PLAYERS) {
        LPCMAPPLAYER player = level.mapinfo->players + i;
        if (player->used && player->playerType == kPlayerTypeHuman) humans++;
    }
    return humans <= 1;
}

BOOL G_GameResultDebugEnabled(void) {
    return gi.CvarString && atoi(gi.CvarString("wc3_game_result_debug", "0")) != 0;
}

void G_GameResultDebug(LPCSTR format, ...) {
    va_list args;

    if (!G_GameResultDebugEnabled()) return;
    fprintf(stderr, "WC3_RESULT ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

void G_RequestEndGame(BOOL do_score_screen) {
    /* Score-screen transport is not implemented yet. Keep the argument at the
     * game/session boundary so EndGame(true) does not get baked into HUD code. */
    G_GameResultDebug("request EndGame score_screen=%u menu_action=%u",
        (unsigned)do_score_screen, (unsigned)(gi.MenuAction != NULL));
    if (gi.MenuAction) gi.MenuAction("menu", "menu_main");
}

void G_RequestChangeLevel(LPCSTR map, BOOL do_score_screen) {
    G_GameResultDebug("request ChangeLevel map=%s score_screen=%u menu_action=%u",
        map ? map : "(null)", (unsigned)do_score_screen, (unsigned)(gi.MenuAction != NULL));
    if (gi.MenuAction && map && *map) gi.MenuAction("map", map);
}

void G_RequestRestartGame(BOOL do_score_screen) {
    LPCSTR map = gi.CvarString ? gi.CvarString("map", "") : "";
    G_GameResultDebug("request RestartGame map=%s score_screen=%u menu_action=%u",
        map ? map : "(null)", (unsigned)do_score_screen, (unsigned)(gi.MenuAction != NULL));
    if (gi.MenuAction && map && *map) gi.MenuAction("map", map);
}

void G_RequestLoadGameMenu(void) {
    G_GameResultDebug("request LoadGameMenu menu_action=%u", (unsigned)(gi.MenuAction != NULL));
    if (gi.MenuAction) gi.MenuAction("menu", "menu_loadgame");
}

void G_RequestCampaignSelect(void) {
    G_GameResultDebug("request CampaignSelect menu_action=%u", (unsigned)(gi.MenuAction != NULL));
    if (gi.MenuAction) gi.MenuAction("menu", "menu_single_player_campaign");
}

/* One complete server-frame simulation step.
 * Skipped until the first map has been started; on the very first frame after
 * a map loads, the JASS "main" function is invoked to run map initialization
 * triggers. */
static void G_RunFrame(void) {
    int path_work_budget = WC3_PATH_WORK_BUDGET;
    LPCSTR path_work_value;

    if (!level.started)
        return;

    if (gi.GetTime)
        level.time = gi.GetTime();

    G_StartScripts();
    G_UpdateTimeOfDay();
    G_RunTimers();
    G_RunEvents();
    jass_runevents(level.vm);

    /* A result action may call RemovePlayer() and then PauseGame(true) from
     * the JASS work above.  The pause takes effect immediately at the server
     * boundary, so consume the newly published terminal result event before
     * this becomes the last simulation frame. */
    G_DrainPausedResultEvents();
    G_BotRunFrame();

    G_RunClients();

    G_RunEntities();

    /* Flow-field cache misses are resumable so arbitrary reachable move orders
     * never depend on a lifetime quota of synchronous whole-map floods.  Keep
     * the per-frame relaxation budget runtime-tunable for slower handhelds. */
    path_work_value = gi.CvarString
        ? gi.CvarString("wc3_path_work_budget", BZ_STRINGIFY(WC3_PATH_WORK_BUDGET)) : NULL;
    if (path_work_value)
        path_work_budget = atoi(path_work_value);
    path_work_budget = MAX(256, MIN(path_work_budget, 65536));
    CM_ProcessPathJobs((DWORD)path_work_budget);

    G_UpdateClientCommandCards();

    G_UpdateClientInfoPanels();
    G_UpdateClientResourceBars();
    G_UpdateClientUnitShortcuts();

    /* RemovePlayer queues its fallback result UI instead of writing it inline.
     * This gives victory/defeat event handlers a chance to enter cinematic mode
     * first; the overlay is emitted once that cinematic has returned to gameplay. */
    UI_FlushPendingGameResults();

    G_SolveCollisions();
    G_FowUpdate();
    G_UpdateClientSelections();
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
    if (type == EVENT_PLAYER_VICTORY || type == EVENT_PLAYER_DEFEAT) {
        G_GameResultDebug("publish event type=%s ordinal=%u subject_ent=%ld owner=%u read=%u write=%u",
            type == EVENT_PLAYER_VICTORY ? "VICTORY" : "DEFEAT",
            (unsigned)(index + 1),
            edict ? (long)edict->s.number : -1L,
            edict ? (unsigned)edict->s.player : 0u,
            (unsigned)level.events.read, (unsigned)level.events.write);
    }
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

static void G_RefreshPauseState(void) { gi.SetPaused(level.script_paused || level.modal_paused); }

/* Quest presentation is local, so only a single connected client may promote
 * that modal state into an authoritative simulation pause. */
static void G_RefreshQuestPause(void) {
    DWORD connected = 0;
    BOOL modal_open = false;

    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = game.clients + i;
        if (!client->connected) continue;
        connected++;
        if (client->modal_flags) modal_open = true;
    }

    /* A local/single-player modal may freeze the simulation. One player's
     * quest screen must never globally pause a multi-client match. */
    level.modal_paused = connected == 1 && modal_open;
    level.quest_paused = level.modal_paused;
    G_RefreshPauseState();
}

/* Script pause owns an independent reason so UI close cannot clear it. */
void G_SetScriptPaused(BOOL paused) {
    level.script_paused = !!paused;
    G_RefreshPauseState();
}

void G_SetClientModal(LPEDICT player, DWORD modal, BOOL open) {
    if (!player || !player->client || !modal) return;
    if (open) player->client->modal_flags |= modal;
    else player->client->modal_flags &= ~modal;
    G_RefreshQuestPause();
}

/* Track Quest ownership per connected client before recomputing global policy. */
void G_SetQuestDialogOpen(LPEDICT player, BOOL open) {
    if (!player || !player->client || !player->client->connected) return;
    player->client->quest_dialog_open = !!open;
    if (open) player->client->modal_flags |= WC3_MODAL_QUEST;
    else player->client->modal_flags &= ~WC3_MODAL_QUEST;
    G_RefreshQuestPause();
}

/* Disconnect clears modal ownership so an abandoned dialog cannot hold pause. */
void G_SetClientConnected(LPEDICT player, BOOL connected) {
    if (!player || !player->client) return;
    player->client->connected = connected;
    if (!connected) player->client->quest_dialog_open = false, player->client->modal_flags = 0;
    G_RefreshQuestPause();
}

/* Client slots and free edicts have zero-initialized player ownership but no
 * unit row.  Only live, metadata-bound units contribute authored food values. */
void G_AccumulatePlayerFood(LPGAMECLIENT client) {
    FILTER_EDICTS(ent, ent->inuse && ent->UnitBalance && client->ps.number == ent->s.player) {
        if (ent->svflags & SVF_DEADMONSTER || ent->training) continue;
        G_SetUnitFoodUsed(ent, ent->UnitBalance->foodUsed);
        if (!ent->construction.active) G_SetUnitFoodMade(ent, ent->UnitBalance->foodMade);
    }
    G_RecomputePlayerUpkeep(client);
}

/* Preserve valid map/save-authored modes while keeping corrupt connection state out of the network contract. */
void G_InitClientUIState(LPGAMECLIENT client) {
    if (client && client->ps.client_ui_state > CLIENT_UI_CINEMATIC)
        client->ps.client_ui_state = CLIENT_UI_GAME;
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
    G_InitClientUIState(client);
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
    UI_WriteHoverLayout(edict);

    G_AccumulatePlayerFood(client);
    /* Invalidate cache so the initial resource bar write always fires. */
    client->resourcebar.gold = -1;
    G_RefreshResourceBar(edict);
    Get_Portrait_f(edict);
    Get_Commands_f(edict);
    client->shortcuts.last_idle_worker = 0;
    client->shortcuts.dirty = false;
    UI_WriteUnitShortcutLayer(edict);

    G_FowConnectPlayer(client->ps.number);
    G_FowUpdate();
    G_FowSendFull(edict);

#ifdef BZ_TESTS
    if (gi.CvarString && atoi(gi.CvarString("wc3_quest_layout_test", "0"))) {
        LPQUEST q = G_MakeQuest();
        LPQUESTITEM it;
        q->title = strdup("Establish Base");
        q->description = strdup(
            "To ensure that the Orc threat is dealt with effectively, you must establish a base "
            "camp and bolster your forces. Only when the camp is prepared can the area be "
            "considered properly garrisoned. Scout the surrounding roads, secure the nearby "
            "farms, and keep the footmen ready for the next attack. The enemy will not wait "
            "for the camp to be complete, so reinforce the walls and patrol the forest edge. "
            "When the base is secure, report back to the command tent for further orders.");
        it = gi.MemAlloc(sizeof(QUESTITEM)); it->description = strdup("Construct a Barracks");
        ADD_TO_LIST(it, q->items);
        it = gi.MemAlloc(sizeof(QUESTITEM)); it->description = strdup("Construct 2 Farms");
        ADD_TO_LIST(it, q->items);
        it = gi.MemAlloc(sizeof(QUESTITEM)); it->description = strdup("Train 6 Footmen");
        ADD_TO_LIST(it, q->items);
        q->discovered = true;
        q->required = true;
        UI_ShowQuests(edict);
    }
#endif
}

/* Look up or register a display name in the packed CS_GENERAL configstring pool.
 * Each configstring stores ENT_NAMES_PER_CS names of ENT_NAME_SLOT_SIZE bytes each.
 * Returns a 1-based packed index (0 = not found / pool full). */
static USHORT G_UnitNameConfigstring(LPCSTR name) {
    char buf[ENT_NAME_SLOT_SIZE * ENT_NAMES_PER_CS];
    if (!name || !*name) return 0;
    for (DWORD slot = 0; slot < CS_MAX_NAMES / ENT_NAMES_PER_CS; slot++) {
        DWORD idx = CS_GENERAL + slot;
        LPCSTR cs = gi.GetConfigstring(idx);
        for (DWORD sub = 0; sub < ENT_NAMES_PER_CS; sub++) {
            LPCSTR entry = cs ? cs + sub * ENT_NAME_SLOT_SIZE : NULL;
            if (entry && !entity_name_slot_empty(entry)) {
                if (entity_name_slot_equals(entry, name))
                    return (USHORT)(slot * ENT_NAMES_PER_CS + sub + 1);
                continue;
            }
            entity_name_pool_prepare(buf, cs);
            entity_name_slot_store(buf, sub, name);
            gi.configstring(idx, buf);
            return (USHORT)(slot * ENT_NAMES_PER_CS + sub + 1);
        }
    }
    fprintf(stderr, "G_UnitNameConfigstring: pool full for \"%s\"\n", name);
    return 0;
}

/* Selection voices are local feedback; suppress them in snapshots for clients
 * that did not select this entity while leaving world sounds unchanged. */
static void G_CustomizeEntity(DWORD player, LPCEDICT ent, LPENTITYSTATE state) {
    BOOL const hoverable = (ent->svflags & SVF_MONSTER) &&
        !(ent->svflags & SVF_DEADMONSTER) &&
        ent->health.value > 0.0f &&
        !(state->renderfx & RF_HIDDEN) &&
        !(state->flags & EF_NOT_SELECTABLE) &&
        G_FowPlayerCanHoverEntity(player, ent);

    state->flags &= ~(EF_HOVER_HEALTH | EF_HOSTILE | EF_NEUTRAL);
    state->name = 0;
    if (hoverable) {
        selectionRelation_t const relation = G_SelectionRelation(player, ent);
        UnitProfile_t const *prof = G_UnitProfile(ent->s.class_id);
        /* UnitProfile.Name is empty for some ROC building rows; the rawcode is
         * still the authoritative identity used by the loaded UnitData row. */
        state->name = G_UnitNameConfigstring(prof->name && *prof->name
            ? prof->name : GetClassName(ent->s.class_id));
        state->flags |= EF_HOVER_HEALTH;
        if (relation == SELECT_RELATION_ENEMY) {
            state->flags |= EF_HOSTILE;
        } else if (relation == SELECT_RELATION_NEUTRAL) {
            state->flags |= EF_NEUTRAL;
        }
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
    globals.SaveGame = WriteGame;
    globals.LoadGame = ReadGame;
    globals.GetSaveMap = G_GetSaveMap;
    globals.GetWorldBounds = CM_GetWorldBounds;
    globals.edict_size = sizeof(struct edict_s);
    return &globals;
}

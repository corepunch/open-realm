#include "games/warcraft-3/common/minimap.h"

extern player_t *currentplayer;

static bool TutorialTextDebugEnabledMisc(void) {
    return WC3_TUTORIAL_DEBUG_ENABLED();
}

static void TutorialTextDebugContextMisc(jass_t *j, int32_t *trigger_ordinal, cstring_t *caller) {
    jassContext_t const *context = jass_getcontext(j);
    if (trigger_ordinal)
        *trigger_ordinal = context && context->trigger ? (int32_t)(context->trigger - level.triggers) : -1L;
    if (caller)
        *caller = context && context->func ? jass_functionname(context->func) : NULL;
}

uint32_t class_id(cstring_t str) { return *(uint32_t *)str; }

/* Converted enums are owned JASS handles; enum equality compares their uint32_t payload. */
static uint32_t JassPushEnumHandle(jass_t *j, cstring_t type, int32_t value) {
    uint32_t *handle = jass_newhandle(j, sizeof(*handle), type);
    *handle = (uint32_t)value;
    return 1;
}

static uint32_t JassPushRaceHandle(jass_t *j, int32_t value) {
    return JassPushEnumHandle(j, "race", value);
}

static uint32_t JassPushPlayerSlotStateHandle(jass_t *j, int32_t value) {
    return JassPushEnumHandle(j, "playerslotstate", value);
}

#define CONVERT_FUNC(NAME, TYPE) \
static uint32_t JassPush##NAME##Handle(jass_t *j, int32_t value) { return JassPushEnumHandle(j, #TYPE, value); } \
uint32_t Convert##NAME(jass_t *j) { \
    return JassPush##NAME##Handle(j, jass_checkinteger(j, 1)); \
}

#define MATH_FUNC(NAME, FUNC, INPUT, OUTPUT) \
uint32_t NAME(jass_t *j) { \
    return jass_push##OUTPUT(j, FUNC(jass_check##INPUT(j, 1))); \
}

#define MATH_FUNC2(NAME, FUNC, OUTPUT) \
uint32_t NAME(jass_t *j) { \
    float arg1 = jass_checknumber(j, 1); \
    float arg2 = jass_checknumber(j, 2); \
    return jass_push##OUTPUT(j, FUNC(arg1, arg2)); \
}

uint32_t ConvertRace(jass_t *j) {
    return JassPushRaceHandle(j, jass_checkinteger(j, 1));
}
CONVERT_FUNC(AllianceType, alliancetype);
CONVERT_FUNC(RacePref, racepreference);
CONVERT_FUNC(IGameState, igamestate);
CONVERT_FUNC(FGameState, fgamestate);
CONVERT_FUNC(PlayerState, playerstate);
CONVERT_FUNC(PlayerGameResult, playergameresult);
CONVERT_FUNC(UnitState, unitstate);
CONVERT_FUNC(GameEvent, gameevent);
CONVERT_FUNC(PlayerEvent, playerevent);
CONVERT_FUNC(PlayerUnitEvent, playerunitevent);
CONVERT_FUNC(WidgetEvent, widgetevent);
CONVERT_FUNC(DialogEvent, dialogevent);
CONVERT_FUNC(UnitEvent, unitevent);
CONVERT_FUNC(LimitOp, limitop);
CONVERT_FUNC(UnitType, unittype);
CONVERT_FUNC(GameSpeed, gamespeed);
CONVERT_FUNC(Placement, placement);
CONVERT_FUNC(StartLocPrio, startlocprio);
CONVERT_FUNC(GameDifficulty, gamedifficulty);
CONVERT_FUNC(GameType, gametype);
CONVERT_FUNC(MapFlag, mapflag);
CONVERT_FUNC(MapVisibility, mapvisibility);
CONVERT_FUNC(MapSetting, mapsetting);
CONVERT_FUNC(MapDensity, mapdensity);
CONVERT_FUNC(MapControl, mapcontrol);
CONVERT_FUNC(PlayerColor, playercolor);
uint32_t ConvertPlayerSlotState(jass_t *j) {
    return JassPushPlayerSlotStateHandle(j, jass_checkinteger(j, 1));
}
CONVERT_FUNC(VolumeGroup, volumegroup);
CONVERT_FUNC(CameraField, camerafield);
CONVERT_FUNC(BlendMode, blendmode);
CONVERT_FUNC(RarityControl, raritycontrol);
CONVERT_FUNC(TexMapFlags, texmapflags);
CONVERT_FUNC(FogState, fogstate);
CONVERT_FUNC(EffectType, effecttype);

MATH_FUNC(Deg2Rad, DEG2RAD, number, number);
MATH_FUNC(Rad2Deg, RAD2DEG, number, number);
MATH_FUNC(Sin, sin, number, number);
MATH_FUNC(Cos, cos, number, number);
MATH_FUNC(Tan, tan, number, number);
MATH_FUNC(Asin, asin, number, number);
MATH_FUNC(Acos, acos, number, number);
MATH_FUNC(Atan, atan, number, number);
MATH_FUNC(SquareRoot, sqrt, number, number);
MATH_FUNC(I2R, (float), integer, number);
MATH_FUNC(R2I, (int32_t), number, integer);
MATH_FUNC2(Pow, pow, number);
MATH_FUNC2(Atan2, atan2, number);
uint32_t OrderId(jass_t *j) {
    return jass_pushinteger(j, (int32_t)G_OrderId(jass_checkstring(j, 1)));
}
uint32_t OrderId2String(jass_t *j) {
    return jass_pushstring(j, G_OrderId2String((uint32_t)jass_checkinteger(j, 1)));
}
MATH_FUNC(UnitId, class_id, string, integer);
MATH_FUNC(AbilityId, class_id, string, integer);
MATH_FUNC(UnitId2String, GetClassName, integer, string);
MATH_FUNC(AbilityId2String, GetClassName, integer, string);
MATH_FUNC(S2I, atoi, string, integer);
MATH_FUNC(S2R, atoi, string, number);

uint32_t I2S(jass_t *j) {
    int32_t i = jass_checkinteger(j, 1);
    char buffer[64] = { 0 };
    snprintf(buffer, sizeof(buffer), "%d", i);
    return jass_pushstring(j, buffer);
}
uint32_t R2S(jass_t *j) {
    float r = jass_checknumber(j, 1);
    char buffer[64] = { 0 };
    snprintf(buffer, sizeof(buffer), "%f", r);
    return jass_pushstring(j, buffer);
}
uint32_t R2SW(jass_t *j) {
    float r = jass_checknumber(j, 1);
    int32_t width = jass_checkinteger(j, 2);
    int32_t precision = jass_checkinteger(j, 3);
    /* Clamp to safe values so the formatted float fits in the 64-byte buffer.
     * A floating-point number needs at most ~25 chars; add width up to 32
     * and precision up to 16 for a safe upper bound well within 64 bytes. */
    if (width < 0 || width > 32) width = 0;
    if (precision < 0 || precision > 16) precision = 6;
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%*.*f", (int)width, (int)precision, (double)r);
    return jass_pushstring(j, buffer);
}
uint32_t SubString(jass_t *j) {
    cstring_t source = jass_checkstring(j, 1);
    int32_t start = jass_checkinteger(j, 2);
    int32_t end = jass_checkinteger(j, 3);
    if (!source) return jass_pushstring(j, "");
    int32_t len = (int32_t)strlen(source);
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) return jass_pushstring(j, "");
    int32_t n = end - start;
    char *buf = gi.MemAlloc(n + 1);
    memcpy(buf, source + start, (size_t)n);
    buf[n] = '\0';
    uint32_t result = jass_pushstring(j, buf);
    gi.MemFree(buf);
    return result;
}
uint32_t GetLocalizedString(jass_t *j) {
    cstring_t source = jass_checkstring(j, 1);
    return jass_pushstring(j, source ? source : "");
}
uint32_t GetLocalizedHotkey(jass_t *j) {
    //cstring_t source = jass_checkstring(j, 1);
    return jass_pushinteger(j, 0);
}
/* Map-configuration natives run from config() before main(). They must mutate a
 * per-level setup snapshot initialized from war3map.w3i; level.mapinfo is
 * authoritative input and must not remain the writable runtime store. */
uint32_t SetMapName(jass_t *j) {
    strlcpy(level.setup.name, jass_checkstring(j, 1), sizeof(level.setup.name));
    return 0;
}
uint32_t SetMapDescription(jass_t *j) {
    strlcpy(level.setup.description, jass_checkstring(j, 1), sizeof(level.setup.description));
    return 0;
}
uint32_t SetTeams(jass_t *j) {
    level.setup.teams = MIN(MAX(0, jass_checkinteger(j, 1)), MAX_PLAYERS);
    return 0;
}
uint32_t SetPlayers(jass_t *j) {
    level.setup.players = MIN(MAX(0, jass_checkinteger(j, 1)), MAX_PLAYERS);
    return 0;
}
uint32_t DefineStartLocation(jass_t *j) {
    int32_t whichStartLoc = jass_checkinteger(j, 1);
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);

    if (level.mapinfo && whichStartLoc >= 0 && whichStartLoc < MAX_PLAYERS) {
        ((mapInfo_t *)level.mapinfo)->players[whichStartLoc].startingPosition = (vector2_t){ x, y };
    }
    return 0;
}
uint32_t DefineStartLocationLoc(jass_t *j) {
    int32_t whichStartLoc = jass_checkinteger(j, 1);
    vector2_t const *whichLocation = jass_checkhandle(j, 2, "location");

    if (level.mapinfo && whichLocation &&
        whichStartLoc >= 0 && whichStartLoc < MAX_PLAYERS) {
        ((mapInfo_t *)level.mapinfo)->players[whichStartLoc].startingPosition = *whichLocation;
    }
    return 0;
}
uint32_t SetStartLocPrioCount(jass_t *j) {
    int32_t loc = jass_checkinteger(j, 1), count = jass_checkinteger(j, 2);
    if (loc >= 0 && loc < MAX_PLAYERS) level.setup.start_prio[loc].count = MIN(MAX(0, count), MAX_START_PRIO);
    return 0;
}
uint32_t SetStartLocPrio(jass_t *j) {
    int32_t loc = jass_checkinteger(j, 1), slot = jass_checkinteger(j, 2), other = jass_checkinteger(j, 3);
    uint32_t *priority = jass_checkhandle(j, 4, "startlocprio");
    if (loc >= 0 && loc < MAX_PLAYERS && slot >= 0 && slot < (int32_t)level.setup.start_prio[loc].count && priority)
        level.setup.start_prio[loc].slots[slot] = (typeof(*level.setup.start_prio[loc].slots)){ other, *priority };
    return 0;
}
uint32_t GetStartLocPrioSlot(jass_t *j) {
    int32_t loc = jass_checkinteger(j, 1), slot = jass_checkinteger(j, 2);
    int32_t value = loc >= 0 && loc < MAX_PLAYERS && slot >= 0 && slot < (int32_t)level.setup.start_prio[loc].count ?
        level.setup.start_prio[loc].slots[slot].location : 0;
    return jass_pushinteger(j, value);
}
uint32_t GetStartLocPrio(jass_t *j) {
    int32_t loc = jass_checkinteger(j, 1), slot = jass_checkinteger(j, 2);
    int32_t value = loc >= 0 && loc < MAX_PLAYERS && slot >= 0 && slot < (int32_t)level.setup.start_prio[loc].count ?
        level.setup.start_prio[loc].slots[slot].priority : 0;
    return JassPushStartLocPrioHandle(j, value);
}
uint32_t SetGameTypeSupported(jass_t *j) {
    uint32_t *type = jass_checkhandle(j, 1, "gametype");
    bool value = jass_checkboolean(j, 2);
    if (type) {
        SET_FLAG(level.setup.game_types, *type, value);
    }
    return 0;
}
uint32_t SetMapFlag(jass_t *j) {
    uint32_t *flag = jass_checkhandle(j, 1, "mapflag");
    bool value = jass_checkboolean(j, 2);
    if (flag) {
        SET_FLAG(level.setup.map_flags, *flag, value);
    }
    return 0;
}
uint32_t SetGamePlacement(jass_t *j) {
    uint32_t *value = jass_checkhandle(j, 1, "placement");
    if (value) level.setup.placement = *value;
    return 0;
}
uint32_t SetGameSpeed(jass_t *j) {
    uint32_t *value = jass_checkhandle(j, 1, "gamespeed");
    if (value) level.setup.speed = *value;
    return 0;
}
uint32_t SetGameDifficulty(jass_t *j) {
    uint32_t *value = jass_checkhandle(j, 1, "gamedifficulty");
    if (value) level.setup.difficulty = *value;
    return 0;
}
uint32_t SetResourceDensity(jass_t *j) {
    uint32_t *value = jass_checkhandle(j, 1, "mapdensity");
    if (value) level.setup.resource_density = *value;
    return 0;
}
uint32_t SetCreatureDensity(jass_t *j) {
    uint32_t *value = jass_checkhandle(j, 1, "mapdensity");
    if (value) level.setup.creature_density = *value;
    return 0;
}
uint32_t GetTeams(jass_t *j) {
    return jass_pushinteger(j, level.setup.teams);
}
uint32_t GetPlayers(jass_t *j) {
    return jass_pushinteger(j, level.setup.players);
}
uint32_t IsGameTypeSupported(jass_t *j) {
    uint32_t *type = jass_checkhandle(j, 1, "gametype");
    return jass_pushboolean(j, type && (level.setup.game_types & *type));
}
uint32_t GetGameTypeSelected(jass_t *j) {
    return JassPushGameTypeHandle(j, level.setup.game_type);
}
uint32_t IsMapFlagSet(jass_t *j) {
    uint32_t *flag = jass_checkhandle(j, 1, "mapflag");
    return jass_pushboolean(j, flag && (level.setup.map_flags & *flag));
}
uint32_t GetGamePlacement(jass_t *j) {
    return JassPushPlacementHandle(j, level.setup.placement);
}
uint32_t GetGameSpeed(jass_t *j) {
    return JassPushGameSpeedHandle(j, level.setup.speed);
}
uint32_t GetGameDifficulty(jass_t *j) {
    return JassPushGameDifficultyHandle(j, level.setup.difficulty);
}
uint32_t GetResourceDensity(jass_t *j) {
    return JassPushMapDensityHandle(j, level.setup.resource_density);
}
uint32_t GetCreatureDensity(jass_t *j) {
    return JassPushMapDensityHandle(j, level.setup.creature_density);
}
uint32_t GetStartLocationX(jass_t *j) {
    int32_t whichStartLocation = jass_checkinteger(j, 1);

    if (!level.mapinfo || whichStartLocation < 0 || whichStartLocation >= MAX_PLAYERS) {
        return jass_pushnumber(j, 0);
    }
    return jass_pushnumber(j, level.mapinfo->players[whichStartLocation].startingPosition.x);
}
uint32_t GetStartLocationY(jass_t *j) {
    int32_t whichStartLocation = jass_checkinteger(j, 1);

    if (!level.mapinfo || whichStartLocation < 0 || whichStartLocation >= MAX_PLAYERS) {
        return jass_pushnumber(j, 0);
    }
    return jass_pushnumber(j, level.mapinfo->players[whichStartLocation].startingPosition.y);
}
uint32_t GetStartLocationLoc(jass_t *j) {
    int32_t whichStartLocation = jass_checkinteger(j, 1);
    API_ALLOC(vector2_t, location);

    if (level.mapinfo && whichStartLocation >= 0 && whichStartLocation < MAX_PLAYERS) {
        *location = level.mapinfo->players[whichStartLocation].startingPosition;
    }
    return 1;
}

uint32_t CreateTimer(jass_t *j) {
    gtimer_t *timer = G_AllocJassTimer();
    if (!timer) { jass_rterror(j, "CreateTimer: timer registry is full"); return 0; }
    return jass_pushlighthandle(j, timer, "timer");
}
uint32_t DestroyTimer(jass_t *j) {
    gtimer_t *whichTimer = jass_checkhandle(j, 1, "timer");
    G_TimerDestroy(whichTimer);
    return 0;
}
uint32_t TimerStart(jass_t *j) {
    gtimer_t *whichTimer = jass_checkhandle(j, 1, "timer");
    float timeout = jass_checknumber(j, 2);
    bool periodic = jass_checkboolean(j, 3);
    /* Warcraft accepts null to start/reset a timer without an expiration callback. */
    jassFunc_t const *handlerFunc = jass_toboolean(j, 4) ? jass_checkcode(j, 4) : NULL;
    if (whichTimer) G_TimerStart(whichTimer, (uint32_t)(MAX(0.0f, timeout) * 1000.0f), periodic, handlerFunc);
    return 0;
}
uint32_t TimerGetElapsed(jass_t *j) {
    gtimer_t *whichTimer = jass_checkhandle(j, 1, "timer");
    return jass_pushnumber(j, whichTimer ? (whichTimer->duration - G_TimerRemaining(whichTimer)) / 1000.0f : 0.0f);
}
uint32_t TimerGetRemaining(jass_t *j) {
    gtimer_t *whichTimer = jass_checkhandle(j, 1, "timer");
    return jass_pushnumber(j, G_TimerRemaining(whichTimer) / 1000.0f);
}
uint32_t TimerGetTimeout(jass_t *j) {
    gtimer_t *whichTimer = jass_checkhandle(j, 1, "timer");
    return jass_pushnumber(j, whichTimer ? whichTimer->duration / 1000.0f : 0.0f);
}
uint32_t PauseTimer(jass_t *j) {
    G_TimerPause(jass_checkhandle(j, 1, "timer")); return 0;
}
uint32_t ResumeTimer(jass_t *j) {
    G_TimerResume(jass_checkhandle(j, 1, "timer")); return 0;
}
uint32_t GetExpiredTimer(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->timer, "timer");
}
uint32_t CreateForce(jass_t *j) {
    API_ALLOC(uint32_t, force);
    (void)force;
    return 1;
}
uint32_t ForceAddPlayer(jass_t *j) {
    uint32_t *whichForce = jass_checkhandle(j, 1, "force");
    player_t *whichPlayer = jass_checkhandle(j, 2, "player");
    if (whichForce && whichPlayer) *whichForce |= 1 << PLAYER_NUM(whichPlayer);
    return 0;
}
uint32_t ForceRemovePlayer(jass_t *j) {
    uint32_t *whichForce = jass_checkhandle(j, 1, "force");
    player_t *whichPlayer = jass_checkhandle(j, 2, "player");
    if (whichForce && whichPlayer) *whichForce &= ~(1 << PLAYER_NUM(whichPlayer));
    return 0;
}
uint32_t ForceClear(jass_t *j) {
    uint32_t *whichForce = jass_checkhandle(j, 1, "force");
    if (whichForce) *whichForce = 0;
    return 0;
}
uint32_t DestroyForce(jass_t *j) {
    uint32_t *whichForce = jass_checkhandle(j, 1, "force");
    if (whichForce) *whichForce = 0;
    return 0;
}
/* Force filters bind each candidate as GetFilterPlayer(); limits count accepted
 * players, matching group enumeration rather than limiting candidates tested. */
uint32_t ForceEnumPlayers(jass_t *j) {
    uint32_t *whichForce = jass_checkhandle(j, 1, "force");
    jassFunc_t const *filter = jass_checkhandle(j, 2, "boolexpr");
    if (!whichForce) return 0;
    FOR_LOOP(i, game.max_clients)
        if (jass_evaluateplayerexpr(j, filter, &game.clients[i].ps)) *whichForce |= 1 << game.clients[i].ps.number;
    return 0;
}
uint32_t ForceEnumPlayersCounted(jass_t *j) {
    uint32_t *whichForce = jass_checkhandle(j, 1, "force");
    jassFunc_t const *filter = jass_checkhandle(j, 2, "boolexpr");
    int32_t countLimit = jass_checkinteger(j, 3);
    if (!whichForce || countLimit <= 0) return 0;
    FOR_LOOP(i, game.max_clients) {
        player_t *player = &game.clients[i].ps;
        if (jass_evaluateplayerexpr(j, filter, player)) *whichForce |= 1 << player->number, countLimit--;
        if (!countLimit) break;
    }
    return 0;
}
uint32_t ForceEnumAllies(jass_t *j) {
    uint32_t *whichForce = jass_checkhandle(j, 1, "force");
    player_t *whichPlayer = jass_checkhandle(j, 2, "player");
    jassFunc_t const *filter = jass_checkhandle(j, 3, "boolexpr");
    if (!whichForce || !whichPlayer) return 0;
    FOR_LOOP(i, game.max_clients) {
        player_t *player = &game.clients[i].ps;
        if (G_GetPlayerAlliance(whichPlayer, player, ALLIANCE_PASSIVE) && jass_evaluateplayerexpr(j, filter, player))
            *whichForce |= 1 << player->number;
    }
    return 0;
}
uint32_t ForceEnumEnemies(jass_t *j) {
    uint32_t *whichForce = jass_checkhandle(j, 1, "force");
    player_t *whichPlayer = jass_checkhandle(j, 2, "player");
    jassFunc_t const *filter = jass_checkhandle(j, 3, "boolexpr");
    if (!whichForce || !whichPlayer) return 0;
    FOR_LOOP(i, game.max_clients) {
        player_t *player = &game.clients[i].ps;
        if (!G_GetPlayerAlliance(whichPlayer, player, ALLIANCE_PASSIVE) && jass_evaluateplayerexpr(j, filter, player))
            *whichForce |= 1 << player->number;
    }
    return 0;
}
uint32_t ForForce(jass_t *j) {
    extern player_t *currentenumplayer;
    uint32_t *whichForce = jass_checkhandle(j, 1, "force");
    jassFunc_t const *callback = jass_checkcode(j, 2);
    player_t *previous = currentenumplayer;

    if (!whichForce || !callback) {
        return 0;
    }
    FOR_LOOP(i, MAX_PLAYERS) {
        if (!(*whichForce & (1 << i))) {
            continue;
        }
        currentenumplayer = G_GetPlayerByNumber(i);
        if (!currentenumplayer) {
            continue;
        }
        jass_pushfunction(j, callback);
        jass_call(j, 0);
    }
    currentenumplayer = previous;
    return 0;
}
uint32_t IsUnitInRegion(jass_t *j) {
    region_t const *whichRegion = G_RegionFromHandle(jass_checkhandle(j, 1, "region"));
    edict_t const *whichUnit = jass_checkhandle(j, 2, "unit");
    return jass_pushboolean(j, whichRegion && whichUnit && G_RegionContains(whichRegion, &whichUnit->s.origin2));
}
uint32_t IsPointInRegion(jass_t *j) {
    region_t const *whichRegion = G_RegionFromHandle(jass_checkhandle(j, 1, "region"));
    vector2_t point = { jass_checknumber(j, 2), jass_checknumber(j, 3) };
    return jass_pushboolean(j, whichRegion && G_RegionContains(whichRegion, &point));
}
uint32_t IsLocationInRegion(jass_t *j) {
    region_t const *whichRegion = G_RegionFromHandle(jass_checkhandle(j, 1, "region"));
    vector2_t const *whichLocation = jass_checkhandle(j, 2, "location");
    return jass_pushboolean(j, whichRegion && whichLocation && G_RegionContains(whichRegion, whichLocation));
}
/* Return the loaded terrain bounds so Blizzard.j's GetEntireMapRect can enumerate every map unit. */
uint32_t GetWorldBounds(jass_t *j) {
    API_ALLOC(box2_t, rect);
    *rect = CM_GetWorldBounds();
    return 1;
}
uint32_t GetFilterUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetEnumUnit(jass_t *j) {
    extern edict_t *currentunit;
    return jass_pushlighthandle(j, currentunit, "unit");
}
uint32_t GetFilterDestructable(jass_t *j) {
    return jass_pushnullhandle(j, "destructable");
}
uint32_t GetEnumDestructable(jass_t *j) {
    extern edict_t *currentdestructable;
    return jass_pushlighthandle(j, currentdestructable, "destructable");
}
uint32_t GetFilterPlayer(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->playerState, "player");
}
uint32_t GetEnumPlayer(jass_t *j) {
    extern player_t *currentenumplayer;
    return jass_pushlighthandle(j, currentenumplayer, "player");
}
uint32_t ExecuteFunc(jass_t *j) {
    cstring_t funcName = jass_checkstring(j, 1);
    (void)jass_startcoroutinebyname(j, funcName);
    return 0;
}
uint32_t newthread(jass_t *j) {
    jassFunc_t const *func = jass_checkcode(j, 1);
    jassContext_t context = *jass_getcontext(j);
    context.func = func;
    jass_startcoroutine(j, &context);
    return 0;
}
uint32_t And(jass_t *j) {
    //handle_t operandA = jass_checkhandle(j, 1, "boolexpr");
    //handle_t operandB = jass_checkhandle(j, 2, "boolexpr");
    return jass_pushnullhandle(j, "boolexpr");
}
uint32_t Or(jass_t *j) {
    //handle_t operandA = jass_checkhandle(j, 1, "boolexpr");
    //handle_t operandB = jass_checkhandle(j, 2, "boolexpr");
    return jass_pushnullhandle(j, "boolexpr");
}
uint32_t Not(jass_t *j) {
    //handle_t operand = jass_checkhandle(j, 1, "boolexpr");
    return jass_pushnullhandle(j, "boolexpr");
}
uint32_t Condition(jass_t *j) {
    jassFunc_t const *func = jass_checkcode(j, 1);
    return jass_pushlighthandle(j, (handle_t)func, "conditionfunc");
}
uint32_t DestroyCondition(jass_t *j) {
    //handle_t c = jass_checkhandle(j, 1, "conditionfunc");
    return 0;
}
uint32_t Filter(jass_t *j) {
    /* Like Condition(): wrap the code as a boolexpr handle so enumeration
     * natives (GroupEnumUnitsInRect, ForceEnum*, etc.) can evaluate it per
     * candidate via jass_evaluateboolexpr.  Was a stub returning null, which
     * made every Filter()-based enum match everything (e.g. GetUnitsInRectOf-
     * Player returned all players' units, polluting victory/kill-count groups). */
    jassFunc_t const *func = jass_checkcode(j, 1);
    return jass_pushlighthandle(j, (handle_t)func, "filterfunc");
}
uint32_t DestroyFilter(jass_t *j) {
    //handle_t f = jass_checkhandle(j, 1, "filterfunc");
    return 0;
}
uint32_t DestroyBoolExpr(jass_t *j) {
    //handle_t e = jass_checkhandle(j, 1, "boolexpr");
    return 0;
}
uint32_t GetEventGameState(jass_t *j) {
    return jass_pushnullhandle(j, "gamestate");
}
uint32_t GetWinningPlayer(jass_t *j) {
    return jass_pushnullhandle(j, "player");
}
uint32_t GetEnteringUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetLeavingUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetTriggeringTrackable(jass_t *j) {
    return jass_pushnullhandle(j, "trackable");
}
uint32_t GetClickedButton(jass_t *j) {
    return jass_pushnullhandle(j, "button");
}
uint32_t GetClickedDialog(jass_t *j) {
    return jass_pushnullhandle(j, "dialog");
}
uint32_t GetLevelingUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetLearningUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetLearnedSkill(jass_t *j) {
    return jass_pushinteger(j, 0);
}
uint32_t GetLearnedSkillLevel(jass_t *j) {
    return jass_pushinteger(j, 0);
}
uint32_t GetRevivableUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetRevivingUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetAttacker(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetRescuer(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetDyingUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetKillingUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->source, "unit");
}
uint32_t GetDecayingUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetConstructingStructure(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetCancelledStructure(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetConstructedStructure(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetResearchingUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetResearched(jass_t *j) {
    return jass_pushinteger(j, jass_getcontext(j)->eventValue);
}
uint32_t GetTrainedUnitType(jass_t *j) {
    jassContext_t const *context = jass_getcontext(j);
    edict_t *trained = context->source ? context->source : context->unit;
    return jass_pushinteger(j, trained ? (int32_t)trained->class_id : 0);
}
uint32_t GetTrainedUnit(jass_t *j) {
    jassContext_t const *context = jass_getcontext(j);
    edict_t *trained = context->source ? context->source : context->unit;
    return jass_pushlighthandle(j, trained, "unit");
}
uint32_t GetDetectedUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetSummoningUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetSummonedUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->source, "unit");
}
uint32_t GetTransportUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetLoadedUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
/* GetChangingUnit/GetChangingUnitPrevOwner read ownership-change event context.
 * eventValue carries prev_owner+1; zero means no change context. */
uint32_t GetChangingUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetChangingUnitPrevOwner(jass_t *j) {
    int32_t val = jass_getcontext(j)->eventValue;
    player_t *player = val > 0 ? G_GetPlayerByNumber((uint32_t)(val - 1)) : NULL;
    return jass_pushlighthandle(j, player, "player");
}
uint32_t GetManipulatingUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetOrderedUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetIssuedOrderId(jass_t *j) {
    return jass_pushinteger(j, G_GetIssuedOrderId(jass_getcontext(j)->unit));
}
uint32_t GetOrderPointX(jass_t *j) {
    vector2_t point = { 0.0f, 0.0f };
    G_GetIssuedOrderPoint(jass_getcontext(j)->unit, &point);
    return jass_pushnumber(j, point.x);
}
uint32_t GetOrderPointY(jass_t *j) {
    vector2_t point = { 0.0f, 0.0f };
    G_GetIssuedOrderPoint(jass_getcontext(j)->unit, &point);
    return jass_pushnumber(j, point.y);
}
uint32_t GetOrderPointLoc(jass_t *j) {
    vector2_t point = { 0.0f, 0.0f };
    API_ALLOC(vector2_t, location);
    G_GetIssuedOrderPoint(jass_getcontext(j)->unit, &point);
    *location = point;
    return 1;
}
uint32_t GetOrderTarget(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->source, "widget");
}
uint32_t GetOrderTargetDestructable(jass_t *j) {
    edict_t *target = jass_getcontext(j)->source;
    return target && G_IsDestructable(target) ?
        jass_pushlighthandle(j, target, "destructable") : jass_pushnullhandle(j, "destructable");
}
uint32_t GetOrderTargetUnit(jass_t *j) {
    edict_t *target = jass_getcontext(j)->source;
    return target && (target->svflags & SVF_MONSTER) ?
        jass_pushlighthandle(j, target, "unit") : jass_pushnullhandle(j, "unit");
}

uint32_t GetSpellAbilityUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetSpellAbilityId(jass_t *j) {
    return jass_pushinteger(j, jass_getcontext(j)->eventValue);
}
uint32_t GetSpellTargetUnit(jass_t *j) {
    edict_t *target = jass_getcontext(j)->source;
    return target && (target->svflags & SVF_MONSTER) ?
        jass_pushlighthandle(j, target, "unit") : jass_pushnullhandle(j, "unit");
}
uint32_t GetSpellTargetDestructable(jass_t *j) {
    edict_t *target = jass_getcontext(j)->source;
    return target && G_IsDestructable(target) ?
        jass_pushlighthandle(j, target, "destructable") : jass_pushnullhandle(j, "destructable");
}
uint32_t GetSpellTargetItem(jass_t *j) {
    edict_t *target = jass_getcontext(j)->source;
    return target && G_IsItem(target) ?
        jass_pushlighthandle(j, target, "item") : jass_pushnullhandle(j, "item");
}
uint32_t GetSpellTargetX(jass_t *j) {
    jassContext_t const *ctx = jass_getcontext(j);
    return jass_pushnumber(j, ctx->hasPoint ? ctx->point.x : 0.0f);
}
uint32_t GetSpellTargetY(jass_t *j) {
    jassContext_t const *ctx = jass_getcontext(j);
    return jass_pushnumber(j, ctx->hasPoint ? ctx->point.y : 0.0f);
}
uint32_t GetSpellTargetLoc(jass_t *j) {
    jassContext_t const *ctx = jass_getcontext(j);
    API_ALLOC(vector2_t, location);
    *location = ctx->hasPoint ? ctx->point : (vector2_t){ 0.0f, 0.0f };
    return 1;
}
uint32_t GetEventPlayerState(jass_t *j) {
    return jass_pushnullhandle(j, "playerstate");
}
uint32_t GetEventPlayerChatString(jass_t *j) {
    return jass_pushstring(j, 0);
}
uint32_t GetEventPlayerChatStringMatched(jass_t *j) {
    return jass_pushstring(j, 0);
}
uint32_t GetEventUnitState(jass_t *j) {
    return jass_pushnullhandle(j, "unitstate");
}
uint32_t GetEventDamage(jass_t *j) {
    return jass_pushnumber(j, (float)jass_getcontext(j)->eventValue);
}
uint32_t GetEventDamageSource(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->source, "unit");
}
uint32_t GetObjectName(jass_t *j) {
    int32_t objectId = jass_checkinteger(j, 1);
    return jass_pushstring(j, G_ObjectName((uint32_t)objectId));
}
uint32_t GetSellingUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetBuyingUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->source, "unit");
}
uint32_t GetSoldUnit(jass_t *j) {
    return jass_pushlighthandle(j, eventsoldunit, "unit");
}
uint32_t GetSoldItem(jass_t *j) {
    return jass_pushlighthandle(j, eventsolditem, "item");
}
uint32_t StringLength(jass_t *j) {
    cstring_t s = jass_checkstring(j, 1);
    return jass_pushinteger(j, s ? (int32_t)strlen(s) : 0);
}
uint32_t StringCase(jass_t *j) {
    cstring_t source = jass_checkstring(j, 1);
    bool upper = jass_checkboolean(j, 2);
    char buf[1024];
    uint32_t i, n;
    if (!source) return jass_pushstring(j, "");
    n = (uint32_t)strlen(source);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)source[i];
        buf[i] = (char)(upper ? toupper(c) : tolower(c));
    }
    buf[n] = '\0';
    return jass_pushstring(j, buf);
}
uint32_t GetEventDetectingPlayer(jass_t *j) {
    return jass_pushnullhandle(j, "player");
}
uint32_t GetEventTargetUnit(jass_t *j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetWidgetLife(jass_t *j) {
    edict_t *whichWidget = jass_checkhandle(j, 1, "widget");
    return jass_pushnumber(j, whichWidget ? whichWidget->health.value : 0);
}
uint32_t SetWidgetLife(jass_t *j) {
    edict_t *whichWidget = jass_checkhandle(j, 1, "widget");
    float newLife = jass_checknumber(j, 2);
    if (whichWidget) {
        bool const was_dead = M_IsDead(whichWidget);
        G_SetHealth(whichWidget, newLife);
        if ((whichWidget->s.flags & EF_FOW_BLOCKER) && was_dead != M_IsDead(whichWidget)) G_FowMarkBlockersDirty();
    }
    return 0;
}
uint32_t GetWidgetX(jass_t *j) {
    edict_t *whichWidget = jass_checkhandle(j, 1, "widget");
    return jass_pushnumber(j, whichWidget ? whichWidget->s.origin.x : 0);
}
uint32_t GetWidgetY(jass_t *j) {
    edict_t *whichWidget = jass_checkhandle(j, 1, "widget");
    return jass_pushnumber(j, whichWidget ? whichWidget->s.origin.y : 0);
}
uint32_t GetFoodMade(jass_t *j) {
    int32_t unitId = jass_checkinteger(j, 1);
    UnitBalance_t const *balance = G_UnitBalance((uint32_t)unitId);
    return jass_pushinteger(j, balance ? balance->foodMade : 0);
}
uint32_t GetFoodUsed(jass_t *j) {
    int32_t unitId = jass_checkinteger(j, 1);
    UnitBalance_t const *balance = G_UnitBalance((uint32_t)unitId);
    return jass_pushinteger(j, balance ? balance->foodUsed : 0);
}

uint32_t EndGame(jass_t *j) {
    bool doScoreScreen = jass_checkboolean(j, 1);
    G_RequestEndGame(doScoreScreen);
    return 0;
}
uint32_t ChangeLevel(jass_t *j) {
    cstring_t newLevel = jass_checkstring(j, 1);
    bool doScoreScreen = jass_checkboolean(j, 2);
    G_RequestChangeLevel(newLevel, doScoreScreen);
    return 0;
}
uint32_t RestartGame(jass_t *j) {
    bool doScoreScreen = jass_checkboolean(j, 1);
    G_RequestRestartGame(doScoreScreen);
    return 0;
}
uint32_t ReloadGame(jass_t *j) {
    return 0;
}
uint32_t DoNotSaveReplay(jass_t *j) { /* TODO: replay recording not yet implemented */ return 0; }
uint32_t SaveGame(jass_t *j) {
    cstring_t name = jass_checkstring(j, 1);
    PATHSTR path;

    if (!name || !*name || strchr(name, '/') || strchr(name, '\\') || !gi.SavePath) {
        fprintf(stderr, "WC3 SaveGame: invalid save name\n");
        return 0;
    }
    gi.SavePath(name, path, sizeof(path));
    if (!WriteGame(path)) fprintf(stderr, "WC3 SaveGame: could not write %s\n", path);
    return 0;
}
uint32_t LoadGame(jass_t *j) {
    cstring_t name = jass_checkstring(j, 1);
    PATHSTR path;

    if (!name || !*name || strchr(name, '/') || strchr(name, '\\') || !gi.SavePath) {
        fprintf(stderr, "WC3 LoadGame: invalid save name\n");
        return 0;
    }
    gi.SavePath(name, path, sizeof(path));
    if (!ReadGame(path)) fprintf(stderr, "WC3 LoadGame: could not read %s\n", path);
    return 0;
}
uint32_t SetCampaignMenuRace(jass_t *j) {
    //handle_t r = jass_checkhandle(j, 1, "race");
    return 0;
}
static bool creep_camp_filter_state = true;

static void set_minimap_ally_color_state(uint16_t value) {
    if (currentplayer) {
        currentplayer->stats[WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR] = value;
        return;
    }
    FOR_LOOP(i, game.max_clients)
        game.clients[i].ps.stats[WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR] = value;
}

uint32_t GetAllyColorFilterState(jass_t *j) {
    int32_t const state = currentplayer ? currentplayer->stats[WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR]
                                     : game.max_clients ? game.clients[0].ps.stats[WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR]
                                                        : WC3_MINIMAP_ALLY_COLOR_PLAYERS;
    return jass_pushinteger(j, state);
}
uint32_t SetAllyColorFilterState(jass_t *j) {
    int32_t state = jass_checkinteger(j, 1);
    state = MAX(WC3_MINIMAP_ALLY_COLOR_PLAYERS, MIN(state, WC3_MINIMAP_ALLY_COLOR_WORLD));
    set_minimap_ally_color_state((uint16_t)state);
    return 0;
}
uint32_t GetCreepCampFilterState(jass_t *j) { (void)j; return jass_pushboolean(j, creep_camp_filter_state); }
uint32_t SetCreepCampFilterState(jass_t *j) { creep_camp_filter_state = jass_checkboolean(j, 1); return 0; }
uint32_t EnableMinimapFilterButtons(jass_t *j) { (void)jass_checkboolean(j, 1); (void)jass_checkboolean(j, 2); return 0; }
uint32_t EnableDragSelect(jass_t *j) { (void)jass_checkboolean(j, 1); (void)jass_checkboolean(j, 2); return 0; }
uint32_t EnablePreSelect(jass_t *j) { (void)jass_checkboolean(j, 1); (void)jass_checkboolean(j, 2); return 0; }
uint32_t EnableSelect(jass_t *j) { (void)jass_checkboolean(j, 1); (void)jass_checkboolean(j, 2); return 0; }
uint32_t SetReservedLocalHeroButtons(jass_t *j) { (void)jass_checkinteger(j, 1); return 0; }
uint32_t CopySaveGame(jass_t *j) {
    (void)jass_checkstring(j, 1);
    (void)jass_checkstring(j, 2);
    return jass_pushboolean(j, false); /* PMV Lua bridge; no save-file copy */
}
uint32_t GetTerrainType(jass_t *j) { (void)jass_checknumber(j, 1); (void)jass_checknumber(j, 2); return jass_pushinteger(j, 0); }
uint32_t GetTerrainVariance(jass_t *j) { (void)jass_checknumber(j, 1); (void)jass_checknumber(j, 2); return jass_pushinteger(j, 0); }
uint32_t IsPointBlighted(jass_t *j) {
    vector2_t point = { jass_checknumber(j, 1), jass_checknumber(j, 2) };
    return jass_pushboolean(j, G_IsPointBlighted(&point));
}
uint32_t IsTerrainPathable(jass_t *j) {
    (void)jass_checknumber(j, 1); (void)jass_checknumber(j, 2); (void)jass_checkhandle(j, 3, "pathingtype");
    return jass_pushboolean(j, true);
}
uint32_t SetTerrainPathable(jass_t *j) {
    (void)jass_checknumber(j, 1); (void)jass_checknumber(j, 2); (void)jass_checkhandle(j, 3, "pathingtype");
    (void)jass_checkboolean(j, 4);
    return 0;
}
uint32_t TerrainDeformRipple(jass_t *j) {
    (void)jass_checknumber(j, 1); (void)jass_checknumber(j, 2); (void)jass_checknumber(j, 3); (void)jass_checknumber(j, 4);
    (void)jass_checkinteger(j, 5); (void)jass_checkinteger(j, 6);
    (void)jass_checknumber(j, 7); (void)jass_checknumber(j, 8); (void)jass_checknumber(j, 9);
    (void)jass_checkboolean(j, 10);
    return jass_pushnullhandle(j, "terraindeformation");
}
uint32_t SetCampaignMenuRaceEx(jass_t *j) {
    //int32_t campaignIndex = jass_checkinteger(j, 1); /* TODO: wire to campaign UI */
    return 0;
}
uint32_t ForceCampaignSelectScreen(jass_t *j) {
    G_RequestCampaignSelect();
    return 0;
}
uint32_t SyncSelections(jass_t *j) {
    return 0;
}
uint32_t SetFloatGameState(jass_t *j) {
    uint32_t *whichFloatGameState = jass_checkhandle(j, 1, "fgamestate");
    float value = jass_checknumber(j, 2);
    if (whichFloatGameState && *whichFloatGameState == WC3_GAME_STATE_TIME_OF_DAY)
        G_SetTimeOfDay(value);
    return 0;
}
uint32_t GetFloatGameState(jass_t *j) {
    uint32_t *whichFloatGameState = jass_checkhandle(j, 1, "fgamestate");
    if (whichFloatGameState && *whichFloatGameState == WC3_GAME_STATE_TIME_OF_DAY)
        return jass_pushnumber(j, G_GetTimeOfDay());
    return jass_pushnumber(j, 0);
}
uint32_t SetIntegerGameState(jass_t *j) {
    //handle_t whichIntegerGameState = jass_checkhandle(j, 1, "igamestate");
    //int32_t value = jass_checkinteger(j, 2);
    return 0;
}
uint32_t GetIntegerGameState(jass_t *j) {
    //handle_t whichIntegerGameState = jass_checkhandle(j, 1, "igamestate");
    return jass_pushinteger(j, 0);
}
uint32_t SetTutorialCleared(jass_t *j) {
    bool cleared = jass_checkboolean(j, 1);
    G_CampaignProgressSetTutorialCleared(cleared);
    return 0;
}
uint32_t SetMissionAvailable(jass_t *j) {
    int32_t campaignNumber = jass_checkinteger(j, 1);
    int32_t missionNumber = jass_checkinteger(j, 2);
    bool available = jass_checkboolean(j, 3);
    G_CampaignProgressSetMissionAvailable(campaignNumber, missionNumber, available);
    return 0;
}
uint32_t SetCampaignAvailable(jass_t *j) {
    int32_t campaignNumber = jass_checkinteger(j, 1);
    bool available = jass_checkboolean(j, 2);
    G_CampaignProgressSetCampaignAvailable(campaignNumber, available);
    return 0;
}
uint32_t SetOpCinematicAvailable(jass_t *j) {
    //int32_t campaignNumber = jass_checkinteger(j, 1);
    //bool available = jass_checkboolean(j, 2);
    return 0;
}
uint32_t SetEdCinematicAvailable(jass_t *j) {
    //int32_t campaignNumber = jass_checkinteger(j, 1);
    //bool available = jass_checkboolean(j, 2);
    return 0;
}
uint32_t GetDefaultDifficulty(jass_t *j) {
    return JassPushGameDifficultyHandle(j, level.setup.default_difficulty);
}
uint32_t SetDefaultDifficulty(jass_t *j) {
    uint32_t *difficulty = jass_checkhandle(j, 1, "gamedifficulty");
    if (difficulty) level.setup.default_difficulty = MIN(*difficulty, 3);
    return 0;
}
uint32_t DialogCreate(jass_t *j) {
    return jass_pushnullhandle(j, "dialog");
}
uint32_t DialogDestroy(jass_t *j) {
    //handle_t whichDialog = jass_checkhandle(j, 1, "dialog");
    return 0;
}
uint32_t DialogSetAsync(jass_t *j) {
    //handle_t whichDialog = jass_checkhandle(j, 1, "dialog");
    return 0;
}
uint32_t DialogClear(jass_t *j) {
    //handle_t whichDialog = jass_checkhandle(j, 1, "dialog");
    return 0;
}
uint32_t DialogSetMessage(jass_t *j) {
    //handle_t whichDialog = jass_checkhandle(j, 1, "dialog");
    //cstring_t messageText = jass_checkstring(j, 2);
    return 0;
}
uint32_t DialogAddButton(jass_t *j) {
    //handle_t whichDialog = jass_checkhandle(j, 1, "dialog");
    //cstring_t buttonText = jass_checkstring(j, 2);
    //int32_t hotkey = jass_checkinteger(j, 3);
    return jass_pushnullhandle(j, "button");
}
uint32_t DialogDisplay(jass_t *j) {
    //player_t *whichPlayer = jass_checkhandle(j, 1, "player");
    //handle_t whichDialog = jass_checkhandle(j, 2, "dialog");
    //bool flag = jass_checkboolean(j, 3);
    return 0;
}
uint32_t InitGameCache(jass_t *j) {
    API_ALLOC(ggamecache_t, gamecache);
    G_GameCacheInit(gamecache, jass_checkstring(j, 1));
    return 1;
}
uint32_t SaveGameCache(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheSave(cache));
}
uint32_t StoreInteger(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    G_GameCacheStoreInteger(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), jass_checkinteger(j, 4));
    return 0;
}
uint32_t StoreReal(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    G_GameCacheStoreReal(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), jass_checknumber(j, 4));
    return 0;
}
uint32_t StoreBoolean(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    G_GameCacheStoreBoolean(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), jass_checkboolean(j, 4));
    return 0;
}
uint32_t StoreUnit(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    edict_t *unit = jass_checkhandle(j, 4, "unit");
    return jass_pushboolean(j, G_GameCacheStoreUnit(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), unit));
}
uint32_t StoreString(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheStoreString(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), jass_checkstring(j, 4)));
}
uint32_t SyncStoredInteger(jass_t *j) {
    (void)jass_checkhandle(j, 1, "gamecache");
    (void)jass_checkstring(j, 2);
    (void)jass_checkstring(j, 3);
    return 0;
}
uint32_t SyncStoredReal(jass_t *j) {
    (void)jass_checkhandle(j, 1, "gamecache");
    (void)jass_checkstring(j, 2);
    (void)jass_checkstring(j, 3);
    return 0;
}
uint32_t SyncStoredBoolean(jass_t *j) {
    (void)jass_checkhandle(j, 1, "gamecache");
    (void)jass_checkstring(j, 2);
    (void)jass_checkstring(j, 3);
    return 0;
}
uint32_t SyncStoredUnit(jass_t *j) {
    (void)jass_checkhandle(j, 1, "gamecache");
    (void)jass_checkstring(j, 2);
    (void)jass_checkstring(j, 3);
    return 0;
}
uint32_t SyncStoredString(jass_t *j) {
    (void)jass_checkhandle(j, 1, "gamecache");
    (void)jass_checkstring(j, 2);
    (void)jass_checkstring(j, 3);
    return 0;
}
uint32_t HaveStoredInteger(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheHave(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_INTEGER));
}
uint32_t HaveStoredReal(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheHave(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_REAL));
}
uint32_t HaveStoredBoolean(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheHave(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_BOOLEAN));
}
uint32_t HaveStoredUnit(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheHave(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_UNIT));
}
uint32_t HaveStoredString(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheHave(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_STRING));
}
uint32_t FlushGameCache(jass_t *j) {
    G_GameCacheFlush(jass_checkhandle(j, 1, "gamecache"));
    return 0;
}
uint32_t FlushStoredMission(jass_t *j) {
    G_GameCacheFlushMission(jass_checkhandle(j, 1, "gamecache"), jass_checkstring(j, 2));
    return 0;
}
uint32_t FlushStoredInteger(jass_t *j) {
    G_GameCacheFlushEntry(jass_checkhandle(j, 1, "gamecache"), jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_INTEGER);
    return 0;
}
uint32_t FlushStoredReal(jass_t *j) {
    G_GameCacheFlushEntry(jass_checkhandle(j, 1, "gamecache"), jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_REAL);
    return 0;
}
uint32_t FlushStoredBoolean(jass_t *j) {
    G_GameCacheFlushEntry(jass_checkhandle(j, 1, "gamecache"), jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_BOOLEAN);
    return 0;
}
uint32_t FlushStoredUnit(jass_t *j) {
    G_GameCacheFlushEntry(jass_checkhandle(j, 1, "gamecache"), jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_UNIT);
    return 0;
}
uint32_t FlushStoredString(jass_t *j) {
    G_GameCacheFlushEntry(jass_checkhandle(j, 1, "gamecache"), jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_STRING);
    return 0;
}
uint32_t GetStoredInteger(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushinteger(j, G_GameCacheGetInteger(cache, jass_checkstring(j, 2), jass_checkstring(j, 3)));
}
uint32_t GetStoredReal(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushnumber(j, G_GameCacheGetReal(cache, jass_checkstring(j, 2), jass_checkstring(j, 3)));
}
uint32_t GetStoredBoolean(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheGetBoolean(cache, jass_checkstring(j, 2), jass_checkstring(j, 3)));
}
uint32_t GetStoredString(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushstring(j, G_GameCacheGetString(cache, jass_checkstring(j, 2), jass_checkstring(j, 3)));
}
uint32_t RestoreUnit(jass_t *j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    cstring_t mission = jass_checkstring(j, 2);
    cstring_t key = jass_checkstring(j, 3);
    player_t *player = jass_checkhandle(j, 4, "player");
    vector2_t location = { jass_checknumber(j, 5), jass_checknumber(j, 6) };
    float facing = jass_checknumber(j, 7);
    edict_t *unit;

    if (!cache || !player) return jass_pushnullhandle(j, "unit");
    unit = G_GameCacheRestoreUnit(cache, mission, key, PLAYER_NUM(player), &location, facing);
    return unit ? jass_pushlighthandle(j, unit, "unit") : jass_pushnullhandle(j, "unit");
}
uint32_t GetRandomInt(jass_t *j) {
    int32_t lowBound = jass_checkinteger(j, 1);
    int32_t highBound = jass_checkinteger(j, 2);
    if (lowBound >= highBound) return jass_pushinteger(j, lowBound);
    return jass_pushinteger(j, lowBound + rand() % (highBound - lowBound + 1));
}
uint32_t GetRandomReal(jass_t *j) {
    float lowBound = jass_checknumber(j, 1);
    float highBound = jass_checknumber(j, 2);
    if (lowBound >= highBound) return jass_pushnumber(j, lowBound);
    float t = (float)rand() / (float)RAND_MAX;
    return jass_pushnumber(j, lowBound + t * (highBound - lowBound));
}
uint32_t CreateUnitPool(jass_t *j) {
    return jass_pushnullhandle(j, "unitpool");
}
uint32_t DestroyUnitPool(jass_t *j) {
    //handle_t whichPool = jass_checkhandle(j, 1, "unitpool");
    return 0;
}
uint32_t UnitPoolAddUnitType(jass_t *j) {
    //handle_t whichPool = jass_checkhandle(j, 1, "unitpool");
    //int32_t unitId = jass_checkinteger(j, 2);
    //float weight = jass_checknumber(j, 3);
    return 0;
}
uint32_t UnitPoolRemoveUnitType(jass_t *j) {
    //handle_t whichPool = jass_checkhandle(j, 1, "unitpool");
    //int32_t unitId = jass_checkinteger(j, 2);
    return 0;
}
uint32_t PlaceRandomUnit(jass_t *j) {
    //handle_t whichPool = jass_checkhandle(j, 1, "unitpool");
    //mapPlayer_t *forWhichPlayer = jass_checkhandle(j, 2, "player");
    //float x = jass_checknumber(j, 3);
    //float y = jass_checknumber(j, 4);
    //float facing = jass_checknumber(j, 5);
    return jass_pushnullhandle(j, "unit");
}
uint32_t CreateItemPool(jass_t *j) {
    return jass_pushnullhandle(j, "itempool");
}
uint32_t DestroyItemPool(jass_t *j) {
    //handle_t whichItemPool = jass_checkhandle(j, 1, "itempool");
    return 0;
}
uint32_t ItemPoolAddItemType(jass_t *j) {
    //handle_t whichItemPool = jass_checkhandle(j, 1, "itempool");
    //int32_t itemId = jass_checkinteger(j, 2);
    //float weight = jass_checknumber(j, 3);
    return 0;
}
uint32_t ItemPoolRemoveItemType(jass_t *j) {
    //handle_t whichItemPool = jass_checkhandle(j, 1, "itempool");
    //int32_t itemId = jass_checkinteger(j, 2);
    return 0;
}
uint32_t PlaceRandomItem(jass_t *j) {
    //handle_t whichItemPool = jass_checkhandle(j, 1, "itempool");
    //float x = jass_checknumber(j, 2);
    //float y = jass_checknumber(j, 3);
    return jass_pushnullhandle(j, "item");
}
uint32_t ChooseRandomCreep(jass_t *j) {
    //int32_t level = jass_checkinteger(j, 1);
    return jass_pushinteger(j, 0);
}
uint32_t ChooseRandomNPBuilding(jass_t *j) {
    return jass_pushinteger(j, 0);
}

uint32_t SetAllItemTypeSlots(jass_t *j) {
    G_SetAllStockSlots(true, jass_checkinteger(j, 1));
    return 0;
}

uint32_t SetAllUnitTypeSlots(jass_t *j) {
    G_SetAllStockSlots(false, jass_checkinteger(j, 1));
    return 0;
}

uint32_t SetItemTypeSlots(jass_t *j) {
    G_SetStockSlots(jass_checkhandle(j, 1, "unit"), true, jass_checkinteger(j, 2));
    return 0;
}

uint32_t SetUnitTypeSlots(jass_t *j) {
    G_SetStockSlots(jass_checkhandle(j, 1, "unit"), false, jass_checkinteger(j, 2));
    return 0;
}

uint32_t AddItemToStock(jass_t *j) {
    G_AddItemStock(jass_checkhandle(j, 1, "unit"), (uint32_t)jass_checkinteger(j, 2),
                   jass_checkinteger(j, 3), jass_checkinteger(j, 4));
    return 0;
}

uint32_t AddItemToAllStock(jass_t *j) {
    G_AddItemStockAll((uint32_t)jass_checkinteger(j, 1), jass_checkinteger(j, 2), jass_checkinteger(j, 3));
    return 0;
}

uint32_t RemoveItemFromStock(jass_t *j) {
    G_RemoveItemStock(jass_checkhandle(j, 1, "unit"), (uint32_t)jass_checkinteger(j, 2));
    return 0;
}

uint32_t RemoveItemFromAllStock(jass_t *j) {
    G_RemoveItemStockAll((uint32_t)jass_checkinteger(j, 1));
    return 0;
}

uint32_t AddUnitToStock(jass_t *j) {
    G_AddUnitStock(jass_checkhandle(j, 1, "unit"), (uint32_t)jass_checkinteger(j, 2),
                   jass_checkinteger(j, 3), jass_checkinteger(j, 4));
    return 0;
}

uint32_t AddUnitToAllStock(jass_t *j) {
    G_AddUnitStockAll((uint32_t)jass_checkinteger(j, 1), jass_checkinteger(j, 2), jass_checkinteger(j, 3));
    return 0;
}

uint32_t RemoveUnitFromStock(jass_t *j) {
    G_RemoveUnitStock(jass_checkhandle(j, 1, "unit"), (uint32_t)jass_checkinteger(j, 2));
    return 0;
}

uint32_t RemoveUnitFromAllStock(jass_t *j) {
    G_RemoveUnitStockAll((uint32_t)jass_checkinteger(j, 1));
    return 0;
}

static bool JassRandomItemEligible(ItemData_t const *row, int32_t level, uint32_t type) {
    if (!row->pickRandom || row->level != level) return false;
    return type == 8 || G_ItemTypeFromClass(row->itemClass) == type;
}

static uint32_t JassChooseRandomItem(int32_t requested_level, uint32_t requested_type) {
    uint32_t item_count, count = 0;
    ItemData_t const *items = G_ItemDataRows(&item_count);
    uint32_t selected_index;

    if (!items) {
        fprintf(stderr, "JassChooseRandomItem: Units\\ItemData.slk is not loaded\n");
        return 0;
    }

    /*
     * First pass counts candidates. This deliberately consumes no random
     * values so SetRandomSeed() remains predictable.
     */
    FOR_LOOP(i, item_count)
        if (JassRandomItemEligible(items + i, requested_level, requested_type)) count++;

    if (!count) return 0;

    selected_index = (uint32_t)(rand() % count);

    /*
     * Second pass returns the selected candidate.
     */
    FOR_LOOP(i, item_count) {
        if (!JassRandomItemEligible(items + i, requested_level, requested_type)) continue;
        if (selected_index--) continue;
        return items[i].id;
    }

    fprintf(stderr, "JassChooseRandomItem: candidate count changed during selection\n");
    return 0;
}

uint32_t ChooseRandomItem(jass_t *j) {
    int32_t level = jass_checkinteger(j, 1);
    uint32_t item_id = JassChooseRandomItem(level, 8);

    return jass_pushinteger(j, (int32_t)item_id);
}

uint32_t ChooseRandomItemEx(jass_t *j) {
    uint32_t *whichType = jass_checkhandle(j, 1, "itemtype");
    int32_t level = jass_checkinteger(j, 2);
    uint32_t type = whichType ? *whichType : 8;
    uint32_t item_id = JassChooseRandomItem(level, type);

    return jass_pushinteger(j, (int32_t)item_id);
}

uint32_t SetRandomSeed(jass_t *j) {
    int32_t seed = jass_checkinteger(j, 1);
    srand((unsigned int)seed);
    return 0;
}
uint32_t SetTerrainFog(jass_t *j) {
    //float a = jass_checknumber(j, 1);
    //float b = jass_checknumber(j, 2);
    //float c = jass_checknumber(j, 3);
    //float d = jass_checknumber(j, 4);
    //float e = jass_checknumber(j, 5);
    return 0;
}
uint32_t ResetTerrainFog(jass_t *j) {
    G_EnvironmentFogReset();
    return 0;
}
uint32_t SetUnitFog(jass_t *j) {
    //float a = jass_checknumber(j, 1);
    //float b = jass_checknumber(j, 2);
    //float c = jass_checknumber(j, 3);
    //float d = jass_checknumber(j, 4);
    //float e = jass_checknumber(j, 5);
    return 0;
}
uint32_t SetTerrainFogEx(jass_t *j) {
    int32_t style = jass_checkinteger(j, 1);
    float zstart = jass_checknumber(j, 2);
    float zend = jass_checknumber(j, 3);
    float density = jass_checknumber(j, 4);
    float red = jass_checknumber(j, 5);
    float green = jass_checknumber(j, 6);
    float blue = jass_checknumber(j, 7);

    G_EnvironmentFogSet(&(wc3EnvironmentFogParams_t){
        .style = style, .start = zstart, .end = zend, .density = density,
        .color = { red, green, blue } });
    return 0;
}
uint32_t SetWaterBaseColor(jass_t *j) {
    //int32_t red = jass_checkinteger(j, 1);
    //int32_t green = jass_checkinteger(j, 2);
    //int32_t blue = jass_checkinteger(j, 3);
    //int32_t alpha = jass_checkinteger(j, 4);
    return 0;
}
uint32_t SetWaterDeforms(jass_t *j) {
    //bool val = jass_checkboolean(j, 1);
    return 0;
}
uint32_t SetDayNightModels(jass_t *j) {
    cstring_t terrainDNCFile = jass_checkstring(j, 1);
    cstring_t unitDNCFile = jass_checkstring(j, 2);
    int terrain_model = 0, unit_model = 0;
    char value[16];

    /* The map script owns the DNC asset choice. Register both models through
     * the ordinary model configstring pool, then publish only their compact
     * indices. The generic client turns those indices back into model handles
     * and the WC3 renderer samples their first animated lights. */
    if (gi.ModelIndex) {
        if (terrainDNCFile && *terrainDNCFile) terrain_model = gi.ModelIndex(terrainDNCFile);
        if (unitDNCFile && *unitDNCFile) unit_model = gi.ModelIndex(unitDNCFile);
    }
    if (gi.configstring) {
        snprintf(value, sizeof(value), "%d", terrain_model);
        gi.configstring(CS_TERRAIN_LIGHT_MODEL, value);
        snprintf(value, sizeof(value), "%d", unit_model);
        gi.configstring(CS_ENTITY_LIGHT_MODEL, value);
    }
    return 0;
}
uint32_t SetSkyModel(jass_t *j) {
    cstring_t skyModelFile = jass_checkstring(j, 1);
    int sky_model = skyModelFile && *skyModelFile ? gi.ModelIndex(skyModelFile) : 0;
    char value[16];
    snprintf(value, sizeof(value), "%d", sky_model);
    gi.configstring(CS_SKY, value);
    return 0;
}
uint32_t EnableUserControl(jass_t *j) {
    bool b = jass_checkboolean(j, 1);
    /* Fast-forwarding must preserve the script's input lock; early edge scrolling overwrote its final camera snap. */
    if (currentplayer) {
        PLAYER_CLIENT(currentplayer)->no_control = !b;
    }
    return 0;
}
uint32_t EnableUserUI(jass_t *j) {
    bool enabled = jass_checkboolean(j, 1);
    /* Warcraft keeps this separate from EnableUserControl: it suppresses UI
     * affordances such as hover/tooltips, but does not make world selection or
     * gameplay orders inert.  Keep the state for the client presentation path;
     * command authorization must not key off it. */
    if (currentplayer) PLAYER_CLIENT(currentplayer)->no_ui = !enabled;
    return 0;
}
uint32_t SuspendTimeOfDay(jass_t *j) {
    G_SuspendTimeOfDay(jass_checkboolean(j, 1));
    return 0;
}
uint32_t SetFalseTimeOfDay(jass_t *j) {
    int32_t hour = jass_checkinteger(j, 1);
    int32_t minute = jass_checkinteger(j, 2);
    float duration = jass_checknumber(j, 3);
    G_SetFalseTimeOfDay(hour, minute, duration);
    return 0;
}
uint32_t SetTimeOfDayScale(jass_t *j) {
    //float r = jass_checknumber(j, 1);
    return 0;
}
uint32_t GetTimeOfDayScale(jass_t *j) {
    return jass_pushnumber(j, 0);
}
uint32_t ShowInterface(jass_t *j) {
    bool flag = jass_checkboolean(j, 1);
    float fadeDuration = jass_checknumber(j, 2);
    player_t *player = currentplayer;
    /* Fast-forwarding compresses time, but the script still owns the cinematic-to-game UI transition. */
    if (player)
        UI_ShowInterface(PLAYER_ENT(player), flag, fadeDuration);
    return 0;
}
uint32_t PauseGame(jass_t *j) {
    bool flag = jass_checkboolean(j, 1);
    G_SetScriptPaused(flag);
    return 0;
}
uint32_t AddIndicator(jass_t *j) {
    edict_t *whichWidget = jass_checkhandle(j, 1, "widget");
    int32_t red = jass_checkinteger(j, 2);
    int32_t green = jass_checkinteger(j, 3);
    int32_t blue = jass_checkinteger(j, 4);
    int32_t alpha = jass_checkinteger(j, 5);
    color32_t color = MAKE(color32_t,
        (uint8_t)MAX(0, MIN(255, red)),
        (uint8_t)MAX(0, MIN(255, green)),
        (uint8_t)MAX(0, MIN(255, blue)),
        (uint8_t)MAX(0, MIN(255, alpha)));

    G_SendWidgetIndicator(whichWidget, color, currentplayer);
    return 0;
}
uint32_t PingMinimap(jass_t *j) {
    float x = jass_checknumber(j, 1);
    float y = jass_checknumber(j, 2);
    float duration = jass_checknumber(j, 3);
    vector2_t position = { x, y };

    if (duration <= 0.0f) return 0;
    if (currentplayer) {
        G_SendMinimapPing(PLAYER_CLIENT(currentplayer), &position, duration, COLOR32_WHITE, 0);
    } else {
        FOR_LOOP(i, game.max_clients)
            G_SendMinimapPing(&game.clients[i], &position, duration, COLOR32_WHITE, 0);
    }
    return 0;
}
uint32_t PingMinimapEx(jass_t *j) {
    float x = jass_checknumber(j, 1);
    float y = jass_checknumber(j, 2);
    float duration = jass_checknumber(j, 3);
    int32_t red = jass_checkinteger(j, 4);
    int32_t green = jass_checkinteger(j, 5);
    int32_t blue = jass_checkinteger(j, 6);
    bool extraEffects = jass_checkboolean(j, 7);
    vector2_t position = { x, y };
    color32_t color = MAKE(color32_t,
        (uint8_t)MAX(0, MIN(255, red)),
        (uint8_t)MAX(0, MIN(255, green)),
        (uint8_t)MAX(0, MIN(255, blue)), 255);

    if (duration <= 0.0f) return 0;
    if (currentplayer) {
        G_SendMinimapPing(PLAYER_CLIENT(currentplayer), &position, duration, color,
                          extraEffects ? MINIMAP_PING_EXTRA_EFFECTS : 0);
    } else {
        FOR_LOOP(i, game.max_clients)
            G_SendMinimapPing(&game.clients[i], &position, duration, color,
                              extraEffects ? MINIMAP_PING_EXTRA_EFFECTS : 0);
    }
    return 0;
}
uint32_t SetAltMinimapIcon(jass_t *j) {
    //cstring_t iconPath = jass_checkstring(j, 1); /* TODO: minimap icon override not yet implemented */
    return 0;
}
uint32_t EnableOcclusion(jass_t *j) {
    //bool flag = jass_checkboolean(j, 1);
    return 0;
}
uint32_t SetIntroShotText(jass_t *j) {
    //cstring_t introText = jass_checkstring(j, 1);
    return 0;
}
uint32_t SetIntroShotModel(jass_t *j) {
    //cstring_t introModelPath = jass_checkstring(j, 1);
    return 0;
}
uint32_t EnableWorldFogBoundary(jass_t *j) {
    //bool b = jass_checkboolean(j, 1);
    return 0;
}
uint32_t PlayCinematic(jass_t *j) {
    cstring_t movieName = jass_checkstring(j, 1);
    PATHSTR path;

    if (!movieName || !*movieName) return 0;
    /* Retail campaign scripts pass logical names such as HumanOp/HumanEd.
     * Classic stores the pre-rendered AVI payloads under Movies\*.mpq. */
    snprintf(path, sizeof(path), "Movies\\%.*s.mpq",
             (int)(sizeof(path) - sizeof("Movies\\.mpq")),
             movieName);
    gi.QueueMovie(path);
    return 0;
}
uint32_t ForceUIKey(jass_t *j) {
    //cstring_t key = jass_checkstring(j, 1);
    return 0;
}
uint32_t ForceUICancel(jass_t *j) {
    return 0;
}
uint32_t DisplayLoadDialog(jass_t *j) {
    G_RequestLoadGameMenu();
    return 0;
}
uint32_t CreateTrackable(jass_t *j) {
    //cstring_t trackableModelPath = jass_checkstring(j, 1);
    //float x = jass_checknumber(j, 2);
    //float y = jass_checknumber(j, 3);
    //float facing = jass_checknumber(j, 4);
    return jass_pushnullhandle(j, "trackable");
}
uint32_t CreateTimerDialog(jass_t *j) {
    gtimer_t *timer = jass_checkhandle(j, 1, "timer");
    timerdialog_t *dialog = G_AllocTimerDialog(timer);
    if (!dialog) {
        jass_rterror(j, "CreateTimerDialog: timer-dialog registry is full");
        return jass_pushnullhandle(j, "timerdialog");
    }
    return jass_pushlighthandle(j, dialog, "timerdialog");
}
uint32_t DestroyTimerDialog(jass_t *j) {
    G_FreeTimerDialog(jass_checkhandle(j, 1, "timerdialog"));
    return 0;
}
uint32_t TimerDialogSetTitle(jass_t *j) {
    timerdialog_t *dialog = jass_checkhandle(j, 1, "timerdialog");
    cstring_t title = jass_checkstring(j, 2);
    if (!dialog || !dialog->inuse) return 0;
    strlcpy(dialog->title, G_LevelString(title ? title : ""), sizeof(dialog->title));
    dialog->title_set = true;
    G_MarkTimerDialogDirty(dialog);
    return 0;
}
uint32_t TimerDialogSetTitleColor(jass_t *j) {
    timerdialog_t *dialog = jass_checkhandle(j, 1, "timerdialog");
    int32_t red = jass_checkinteger(j, 2);
    int32_t green = jass_checkinteger(j, 3);
    int32_t blue = jass_checkinteger(j, 4);
    int32_t alpha = jass_checkinteger(j, 5);
    if (!dialog || !dialog->inuse) return 0;
    dialog->title_color = MAKE(color32_t,
        (uint8_t)MAX(0, MIN(255, red)), (uint8_t)MAX(0, MIN(255, green)),
        (uint8_t)MAX(0, MIN(255, blue)), (uint8_t)MAX(0, MIN(255, alpha)));
    dialog->title_color_set = true;
    G_MarkTimerDialogDirty(dialog);
    return 0;
}
uint32_t TimerDialogSetTimeColor(jass_t *j) {
    timerdialog_t *dialog = jass_checkhandle(j, 1, "timerdialog");
    int32_t red = jass_checkinteger(j, 2);
    int32_t green = jass_checkinteger(j, 3);
    int32_t blue = jass_checkinteger(j, 4);
    int32_t alpha = jass_checkinteger(j, 5);
    if (!dialog || !dialog->inuse) return 0;
    dialog->time_color = MAKE(color32_t,
        (uint8_t)MAX(0, MIN(255, red)), (uint8_t)MAX(0, MIN(255, green)),
        (uint8_t)MAX(0, MIN(255, blue)), (uint8_t)MAX(0, MIN(255, alpha)));
    dialog->time_color_set = true;
    G_MarkTimerDialogDirty(dialog);
    return 0;
}
uint32_t TimerDialogSetSpeed(jass_t *j) {
    /* Deliberately left unsupported until retail display-rate semantics are
     * pinned down.  Do not alter the authoritative gameplay timer here. */
    (void)jass_checkhandle(j, 1, "timerdialog");
    (void)jass_checknumber(j, 2);
    return 0;
}
uint32_t TimerDialogDisplay(jass_t *j) {
    timerdialog_t *dialog = jass_checkhandle(j, 1, "timerdialog");
    bool display = jass_checkboolean(j, 2);
    G_SetTimerDialogVisible(dialog, currentplayer, display);
    return 0;
}
uint32_t IsTimerDialogDisplayed(jass_t *j) {
    timerdialog_t *dialog = jass_checkhandle(j, 1, "timerdialog");
    return jass_pushboolean(j, G_IsTimerDialogVisible(dialog, currentplayer));
}
uint32_t SetCinematicScene(jass_t *j) {
    int32_t portraitUnitId = jass_checkinteger(j, 1);
    uint32_t *color = jass_checkhandle(j, 2, "playercolor");
    cstring_t speakerTitle = jass_checkstring(j, 3);
    cstring_t text = jass_checkstring(j, 4);
    float sceneDuration = jass_checknumber(j, 5);
    float voiceoverDuration = jass_checknumber(j, 6);
    if (TutorialTextDebugEnabledMisc()) {
        int32_t trigger_ordinal;
        cstring_t caller;
        cstring_t resolved_speaker = G_LevelString(speakerTitle);
        cstring_t resolved_text = G_LevelString(text);
        TutorialTextDebugContextMisc(j, &trigger_ordinal, &caller);
        fprintf(stderr,
                "WC3_TUTORIAL_TEXT native=SetCinematicScene trigger=%ld caller=\"%s\" player=%d portrait=%.4s scene=%.3f voice=%.3f speaker_raw=\"%s\" speaker=\"%s\" text_raw=\"%s\" text=\"%s\"\n",
                (long)trigger_ordinal, caller ? caller : "(native/root)",
                currentplayer ? (int)PLAYER_NUM(currentplayer) : -1,
                portraitUnitId ? (cstring_t)&portraitUnitId : "----", sceneDuration, voiceoverDuration,
                speakerTitle ? speakerTitle : "", resolved_speaker ? resolved_speaker : "",
                text ? text : "", resolved_text ? resolved_text : "");
    }
    if (G_SkipCutscene()) return 0;
    if (currentplayer) {
        gameClient_t *gc = PLAYER_CLIENT(currentplayer);
        uint32_t now = G_Time();
        G_SetPlayerText(gc, PLAYERTEXT_SPEAKER, G_LevelString(speakerTitle));
        G_SetPlayerText(gc, PLAYERTEXT_DIALOGUE, G_LevelString(text));
        /* Only gameplay transmissions are tutorial prompts; cutscene dialogue
         * belongs to the cinematic presentation and must not enter F12 history. */
        if (gc && gc->ps.client_ui_state == CLIENT_UI_GAME)
            UI_RecordTransmissionMessage(PLAYER_ENT(currentplayer));
        currentplayer->cinematic_portrait = 0;
        /* The renderer currently owns 16 replaceable team-color textures.
         * Keep unsupported extended player colors deterministic instead of
         * allowing the renderer's bit mask to wrap them onto another color. */
        currentplayer->stats[UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR] =
            color && *color < MAX_PLAYERS ? *color : 0;
        if (portraitUnitId) {
            cstring_t model = G_UnitUI((uint32_t)portraitUnitId)->modelFile;
            if (model && *model) {
                PATHSTR mf;
                G_NormalizeModelFilename(model, mf, sizeof(mf));
                currentplayer->cinematic_portrait = G_RegisterModel(mf);
            }
        }
        if (gc) {
            gc->cinematic_end_time = sceneDuration > 0 ? now + (uint32_t)(sceneDuration * 1000.0f) : 0;
            gc->cinematic_voice_end_time = voiceoverDuration > 0 ? now + (uint32_t)(voiceoverDuration * 1000.0f) : 0;
        }
        UI_InvalidateDialoguePresentation(PLAYER_ENT(currentplayer));
    }
    return 0;
}
uint32_t EndCinematicScene(jass_t *j) {
    if (TutorialTextDebugEnabledMisc()) {
        int32_t trigger_ordinal;
        cstring_t caller;
        TutorialTextDebugContextMisc(j, &trigger_ordinal, &caller);
        fprintf(stderr,
                "WC3_TUTORIAL_TEXT native=EndCinematicScene trigger=%ld caller=\"%s\" player=%d\n",
                (long)trigger_ordinal, caller ? caller : "(native/root)",
                currentplayer ? (int)PLAYER_NUM(currentplayer) : -1);
    }
    if (currentplayer) {
        gameClient_t *gc = PLAYER_CLIENT(currentplayer);
        G_SetPlayerText(gc, PLAYERTEXT_SPEAKER, "");
        G_SetPlayerText(gc, PLAYERTEXT_DIALOGUE, "");
        currentplayer->cinematic_portrait = 0;
        currentplayer->stats[UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR] = 0;
        if (gc) {
            gc->cinematic_end_time = 0;
            gc->cinematic_voice_end_time = 0;
        }
        UI_InvalidateDialoguePresentation(PLAYER_ENT(currentplayer));
    }
    return 0;
}
uint32_t ForceCinematicSubtitles(jass_t *j) {
    /* Current Warsmash always renders transmission subtitles even when this
     * override is false. Consume the native so campaign scripts remain valid
     * without pretending OpenRealm has a separate subtitle preference yet. */
    (void)jass_checkboolean(j, 1);
    return 0;
}
uint32_t NewSoundEnvironment(jass_t *j) {
    //cstring_t environmentName = jass_checkstring(j, 1);
    return 0;
}
uint32_t SetDoodadAnimation(jass_t *j) {
    float x = jass_checknumber(j, 1);
    float y = jass_checknumber(j, 2);
    float radius = jass_checknumber(j, 3);
    int32_t doodadID = jass_checkinteger(j, 4);
    bool nearestOnly = jass_checkboolean(j, 5);
    cstring_t animName = jass_checkstring(j, 6);
    bool animRandom = jass_checkboolean(j, 7);
    doodadAnimationRadiusParams_t const params = {
        .x = x, .y = y, .radius = radius, .doodad_id = (uint32_t)doodadID,
        .nearest_only = nearestOnly, .anim_name = animName, .random_animation = animRandom
    };

    G_SetDoodadAnimationRadius(&params);
    return 0;
}
uint32_t SetDoodadAnimationRect(jass_t *j) {
    box2_t const *r = jass_checkhandle(j, 1, "rect");
    int32_t doodadID = jass_checkinteger(j, 2);
    cstring_t animName = jass_checkstring(j, 3);
    bool animRandom = jass_checkboolean(j, 4);

    G_SetDoodadAnimationRect(r, (uint32_t)doodadID, animName, animRandom);
    return 0;
}
uint32_t Cheat(jass_t *j) {
    //cstring_t cheatStr = jass_checkstring(j, 1);
    return 0;
}
uint32_t IsNoVictoryCheat(jass_t *j) {
    return jass_pushboolean(j, 0);
}
uint32_t IsNoDefeatCheat(jass_t *j) {
    return jass_pushboolean(j, 0);
}
uint32_t Preload(jass_t *j) {
    //cstring_t filename = jass_checkstring(j, 1);
    return 0;
}
uint32_t PreloadEnd(jass_t *j) {
    //float timeout = jass_checknumber(j, 1);
    return 0;
}
uint32_t PreloadGenClear(jass_t *j) {
    return 0;
}
uint32_t PreloadGenStart(jass_t *j) {
    return 0;
}
uint32_t PreloadGenEnd(jass_t *j) {
    //cstring_t filename = jass_checkstring(j, 1);
    return 0;
}
uint32_t Preloader(jass_t *j) {
    //cstring_t filename = jass_checkstring(j, 1);
    return 0;
}

// **************
// 1.29 additions
// **************

uint32_t GetPlayerNeutralPassive(jass_t *j) {
    return jass_pushinteger(j, PLAYER_NEUTRAL_PASSIVE);
}
uint32_t GetPlayerNeutralAggressive(jass_t *j) {
    return jass_pushinteger(j, PLAYER_NEUTRAL_AGGRESSIVE);
}
uint32_t GetBJMaxPlayers(jass_t *j) {
    return jass_pushinteger(j, game.max_clients);
}
uint32_t GetBJPlayerNeutralVictim(jass_t *j) {
    return jass_pushinteger(j, PLAYER_NEUTRAL_VICTIM);
}
uint32_t GetBJPlayerNeutralExtra(jass_t *j) {
    return jass_pushinteger(j, PLAYER_NEUTRAL_EXTRA);
}
uint32_t GetBJMaxPlayerSlots(jass_t *j) {
    return jass_pushinteger(j, 12);
}
uint32_t ConvertVersion(jass_t *j) {
    API_ALLOC(uint32_t, version);
    *version = jass_checkinteger(j, 1);
    return 1;
}
uint32_t ConvertItemType(jass_t *j) {
    API_ALLOC(uint32_t, itemtype);
    *itemtype = jass_checkinteger(j, 1);
    return 1;
}
uint32_t ConvertAttackType(jass_t *j) {
    API_ALLOC(uint32_t, attacktype);
    *attacktype = jass_checkinteger(j, 1);
    return 1;
}
uint32_t ConvertDamageType(jass_t *j) {
    API_ALLOC(uint32_t, damagetype);
    *damagetype = jass_checkinteger(j, 1);
    return 1;
}
uint32_t ConvertWeaponType(jass_t *j) {
    API_ALLOC(uint32_t, weapontype);
    *weapontype = jass_checkinteger(j, 1);
    return 1;
}
uint32_t ConvertSoundType(jass_t *j) {
    API_ALLOC(uint32_t, soundtype);
    *soundtype = jass_checkinteger(j, 1);
    return 1;
}
uint32_t ConvertPathingType(jass_t *j) {
    API_ALLOC(uint32_t, pathingtype);
    *pathingtype = jass_checkinteger(j, 1);
    return 1;
}
uint32_t ConvertMouseButtonType(jass_t *j) {
    API_ALLOC(uint32_t, mousebuttontype);
    *mousebuttontype = jass_checkinteger(j, 1);
    return 1;
}
uint32_t ConvertAIDifficulty(jass_t *j) {
    API_ALLOC(uint32_t, aidifficulty);
    *aidifficulty = jass_checkinteger(j, 1);
    return 1;
}
uint32_t ConvertPlayerScore(jass_t *j) {
    API_ALLOC(uint32_t, playerscore);
    *playerscore = jass_checkinteger(j, 1);
    return 1;
}
/* Lightning handles are stable pointers into the same game-owned registry
 * used by ability presentation.  The client therefore renders JASS and spell
 * lightning through one endpoint/data-row path. */
typedef struct {
    vector3_t pos, size, origin;
    float color[4];
    bool shown, render, render_always;
    char file[128];
} jassImage_t;
typedef struct {
    vector2_t pos;
    float color[4];
    bool shown, render, render_always, finished;
    char name[64];
} jassUbersplat_t;

static uint32_t JassLightningCode(cstring_t code) {
    return code && strlen(code) >= 4 ? MAKEFOURCC(code[0], code[1], code[2], code[3]) : 0;
}

static uint8_t JassLightningByte(float value) {
    value = value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
    return (uint8_t)(value * 255.0f + 0.5f);
}

static uint32_t JassLightningCreate(jass_t *j, cstring_t code, bool check_visibility,
                                  vector3_t const *source, vector3_t const *target) {
    gLightning_t *bolt;
    (void)check_visibility; /* Visibility filtering is a client fog concern, not a global registry property. */
    bolt = G_LightningAdd(&(lightningAddParams_t){
        .effect_id = JassLightningCode(code), .source = source, .target = target,
        .color = COLOR32_WHITE,
    });
    return bolt ? jass_pushlighthandle(j, bolt, "lightning") : jass_pushnullhandle(j, "lightning");
}

uint32_t AddLightningEx(jass_t *j) {
    vector3_t source = MAKE(vector3_t, jass_checknumber(j, 3), jass_checknumber(j, 4), jass_checknumber(j, 5));
    vector3_t target = MAKE(vector3_t, jass_checknumber(j, 6), jass_checknumber(j, 7), jass_checknumber(j, 8));
    return JassLightningCreate(j, jass_checkstring(j, 1), jass_checkboolean(j, 2), &source, &target);
}
uint32_t AddLightning(jass_t *j) {
    vector3_t source = MAKE(vector3_t, jass_checknumber(j, 3), jass_checknumber(j, 4), 0);
    vector3_t target = MAKE(vector3_t, jass_checknumber(j, 5), jass_checknumber(j, 6), 0);
    return JassLightningCreate(j, jass_checkstring(j, 1), jass_checkboolean(j, 2), &source, &target);
}
uint32_t DestroyLightning(jass_t *j) {
    gLightning_t *bolt = jass_checkhandle(j, 1, "lightning");
    bool valid = G_LightningValid(bolt);
    if (valid) G_LightningRemove(bolt);
    return jass_pushboolean(j, valid);
}
uint32_t MoveLightningEx(jass_t *j) {
    gLightning_t *bolt = jass_checkhandle(j, 1, "lightning");
    vector3_t source, target;
    (void)jass_checkboolean(j, 2);
    source = MAKE(vector3_t, jass_checknumber(j, 3), jass_checknumber(j, 4), jass_checknumber(j, 5));
    target = MAKE(vector3_t, jass_checknumber(j, 6), jass_checknumber(j, 7), jass_checknumber(j, 8));
    if (!G_LightningValid(bolt)) return jass_pushboolean(j, 0);
    G_LightningMove(bolt, &source, &target);
    return jass_pushboolean(j, 1);
}
uint32_t MoveLightning(jass_t *j) {
    gLightning_t *bolt = jass_checkhandle(j, 1, "lightning");
    vector3_t source, target;
    (void)jass_checkboolean(j, 2);
    if (!G_LightningValid(bolt)) return jass_pushboolean(j, 0);
    source = bolt->state.source; target = bolt->state.target;
    source.x = jass_checknumber(j, 3); source.y = jass_checknumber(j, 4);
    target.x = jass_checknumber(j, 5); target.y = jass_checknumber(j, 6);
    G_LightningMove(bolt, &source, &target);
    return jass_pushboolean(j, 1);
}
uint32_t SetLightningColor(jass_t *j) {
    gLightning_t *bolt = jass_checkhandle(j, 1, "lightning");
    float precise[4] = { jass_checknumber(j, 2), jass_checknumber(j, 3),
        jass_checknumber(j, 4), jass_checknumber(j, 5) };
    color32_t color = MAKE(color32_t, JassLightningByte(precise[0]), JassLightningByte(precise[1]),
        JassLightningByte(precise[2]), JassLightningByte(precise[3]));
    if (!G_LightningValid(bolt)) return jass_pushboolean(j, 0);
    G_LightningScriptColor(bolt, color, precise);
    return jass_pushboolean(j, 1);
}
/* Read the precise script colour while invalid handles retain JASS's zero result. */
static uint32_t JassGetLightningColor(jass_t *j, uint32_t channel) {
    gLightning_t *bolt = jass_checkhandle(j, 1, "lightning");
    return jass_pushnumber(j, G_LightningValid(bolt) ? bolt->script_color[channel] : 0);
}
uint32_t GetLightningColorR(jass_t *j) { return JassGetLightningColor(j, 0); }
uint32_t GetLightningColorG(jass_t *j) { return JassGetLightningColor(j, 1); }
uint32_t GetLightningColorB(jass_t *j) { return JassGetLightningColor(j, 2); }
uint32_t GetLightningColorA(jass_t *j) { return JassGetLightningColor(j, 3); }

uint32_t CreateImage(jass_t *j) {
    cstring_t file = jass_checkstring(j, 1);
    jassImage_t *img = jass_newhandle(j, sizeof(*img), "image");
    if (!img) return jass_pushnullhandle(j, "image");
    memset(img, 0, sizeof(*img));
    img->size = MAKE(vector3_t, jass_checknumber(j, 2), jass_checknumber(j, 3), jass_checknumber(j, 4));
    img->pos = MAKE(vector3_t, jass_checknumber(j, 5), jass_checknumber(j, 6), jass_checknumber(j, 7));
    img->origin = MAKE(vector3_t, jass_checknumber(j, 8), jass_checknumber(j, 9), jass_checknumber(j, 10));
    (void)jass_checkinteger(j, 11);
    img->color[0] = img->color[1] = img->color[2] = img->color[3] = 1.0f;
    img->shown = true;
    if (file) strlcpy(img->file, file, sizeof(img->file));
    return 1;
}
uint32_t DestroyImage(jass_t *j) { jassImage_t *img = jass_checkhandle(j, 1, "image"); if (img) img->shown = false; return 0; }
uint32_t ShowImage(jass_t *j) { jassImage_t *img = jass_checkhandle(j, 1, "image"); if (img) img->shown = jass_checkboolean(j, 2); return 0; }
uint32_t SetImagePosition(jass_t *j) {
    jassImage_t *img = jass_checkhandle(j, 1, "image");
    if (img) img->pos = MAKE(vector3_t, jass_checknumber(j, 2), jass_checknumber(j, 3), jass_checknumber(j, 4));
    return 0;
}
uint32_t SetImageColor(jass_t *j) {
    jassImage_t *img = jass_checkhandle(j, 1, "image");
    if (img) {
        img->color[0] = jass_checkinteger(j, 2) / 255.0f; img->color[1] = jass_checkinteger(j, 3) / 255.0f;
        img->color[2] = jass_checkinteger(j, 4) / 255.0f; img->color[3] = jass_checkinteger(j, 5) / 255.0f;
    }
    return 0;
}
uint32_t SetImageRender(jass_t *j) { jassImage_t *img = jass_checkhandle(j, 1, "image"); if (img) img->render = jass_checkboolean(j, 2); return 0; }
uint32_t SetImageRenderAlways(jass_t *j) { jassImage_t *img = jass_checkhandle(j, 1, "image"); if (img) img->render_always = jass_checkboolean(j, 2); return 0; }
uint32_t SetImageConstantHeight(jass_t *j) { (void)j; return 0; }
uint32_t SetImageAboveWater(jass_t *j) { (void)j; return 0; }
uint32_t SetImageType(jass_t *j) { (void)j; return 0; }

uint32_t CreateUbersplat(jass_t *j) {
    jassUbersplat_t *u = jass_newhandle(j, sizeof(*u), "ubersplat");
    cstring_t name = jass_checkstring(j, 3);
    if (!u) return jass_pushnullhandle(j, "ubersplat");
    memset(u, 0, sizeof(*u));
    u->pos = MAKE(vector2_t, jass_checknumber(j, 1), jass_checknumber(j, 2));
    u->color[0] = jass_checkinteger(j, 4) / 255.0f; u->color[1] = jass_checkinteger(j, 5) / 255.0f;
    u->color[2] = jass_checkinteger(j, 6) / 255.0f; u->color[3] = jass_checkinteger(j, 7) / 255.0f;
    (void)jass_checkboolean(j, 8); (void)jass_checkboolean(j, 9);
    u->shown = true;
    if (name) strlcpy(u->name, name, sizeof(u->name));
    return 1;
}
uint32_t DestroyUbersplat(jass_t *j) { jassUbersplat_t *u = jass_checkhandle(j, 1, "ubersplat"); if (u) u->shown = false; return 0; }
uint32_t ResetUbersplat(jass_t *j) { jassUbersplat_t *u = jass_checkhandle(j, 1, "ubersplat"); if (u) u->finished = false; return 0; }
uint32_t FinishUbersplat(jass_t *j) { jassUbersplat_t *u = jass_checkhandle(j, 1, "ubersplat"); if (u) u->finished = true; return 0; }
uint32_t ShowUbersplat(jass_t *j) { jassUbersplat_t *u = jass_checkhandle(j, 1, "ubersplat"); if (u) u->shown = jass_checkboolean(j, 2); return 0; }
uint32_t SetUbersplatRender(jass_t *j) { jassUbersplat_t *u = jass_checkhandle(j, 1, "ubersplat"); if (u) u->render = jass_checkboolean(j, 2); return 0; }
uint32_t SetUbersplatRenderAlways(jass_t *j) { jassUbersplat_t *u = jass_checkhandle(j, 1, "ubersplat"); if (u) u->render_always = jass_checkboolean(j, 2); return 0; }

uint32_t VersionGet(jass_t *j) {
    API_ALLOC(uint32_t, version);
    *version = 0;
    return 1;
}
/* Version enums are typed handles from ConvertVersion, not integer arguments. */
uint32_t VersionCompatible(jass_t *j) {
    uint32_t *version = jass_checkhandle(j, 1, "version");
    return jass_pushboolean(j, version && *version == 0);
}
uint32_t VersionSupported(jass_t *j) {
    uint32_t *version = jass_checkhandle(j, 1, "version");
    return jass_pushboolean(j, version && *version == 0);
}

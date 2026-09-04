#include "g_local.h"
#include "jass/jass.h"
#include "skills/s_skills.h"

#define BOT_GUARD_RETURN_RANGE 64.0f // world units; avoid resetting movement for guards already standing near their post
#define BOT_BUILD_GRID 32.0f // world units; WC3 structures snap to this placement-cell interval
#define BOT_BUILD_SEARCH_RINGS 32 // 32-unit grid rings; searches 1024 world units around a town for legal placement

static bot_t *G_BotState(DWORD player) {
    return player < MAX_PLAYERS ? &level.bots[player] : NULL;
}

static void G_BotClearCaptains(bot_t *bot) {
    FOR_LOOP(i, BOT_CAPTAIN_COUNT) {
        if (bot->captains[i].units) gi.MemFree(bot->captains[i].units);
        memset(bot->captains + i, 0, sizeof(bot->captains[i]));
    }
}

/* KillUnit changes life immediately while ordinary death also carries SVF_DEADMONSTER. */
BOOL G_BotUnitAlive(LPEDICT unit) {
    return unit && unit->inuse && unit->health.value > 0 && !(unit->svflags & SVF_DEADMONSTER);
}

/* Stop only active gather orders; carried resources remain available for an explicit return order. */
void G_BotStopGathering(LPPLAYER player) {
    if (!player) return;
    FILTER_EDICTS(unit, unit->inuse && unit->s.player == PLAYER_NUM(player) && unit->currentmove &&
        (unit->currentmove->ability == &a_harvest || unit->currentmove->ability == &a_goldmine ||
         unit->currentmove->ability == &a_wisp_harvest)) {
        S_GoldMineReleaseWorker(unit);
        order_stop(unit);
    }
}

static BOOL G_BotHarvesterReserved(bot_t *bot, LPEDICT unit) {
    FOR_EACH_ARRAY(LPEDICT, assigned, bot->harvesters) if (*assigned == unit) return true;
    return false;
}

static void G_BotReserveHarvester(bot_t *bot, LPEDICT unit) {
    DWORD count = ARRAY_COUNT(bot->harvesters);
    LPEDICT *units = gi.MemAlloc((count + 1) * sizeof(*units));
    if (count) memcpy(units, bot->harvesters, count * sizeof(*units));
    if (bot->harvesters) gi.MemFree(bot->harvesters);
    bot->harvesters = units; ARRAY_COUNT(bot->harvesters) = count + 1; bot->harvesters[count] = unit;
}

void G_BotClearHarvest(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    if (bot->harvesters) gi.MemFree(bot->harvesters);
    bot->harvesters = NULL; ARRAY_COUNT(bot->harvesters) = 0;
}

/* Town IDs enumerate owned gold drop-offs in spawn order, matching the expansion index used by common.ai. */
LPEDICT G_BotTown(LPPLAYER player, LONG town) {
    edict_t probe = {0};
    if (!player || town < 0) return NULL;
    probe.s.player = PLAYER_NUM(player);
    FILTER_EDICTS(ent, S_CanReturnResourceAt(&probe, ent, RETURN_RESOURCE_GOLD))
        if (!town--) return ent;
    return NULL;
}

static LPEDICT G_BotMineOwner(LPPLAYER player, LPEDICT mine) {
    LPEDICT best = NULL;
    FLOAT best_dist = 0;
    edict_t probe = {0};
    if (!player || !mine) return NULL;
    probe.s.player = PLAYER_NUM(player);
    FILTER_EDICTS(town, S_CanReturnResourceAt(&probe, town, RETURN_RESOURCE_GOLD)) {
        FLOAT dist = Vector2_distance(&town->s.origin2, &mine->s.origin2);
        if (!best || dist < best_dist) { best = town; best_dist = dist; }
    }
    return best;
}

static LPEDICT G_BotHarvestTarget(LPPLAYER player, LPEDICT town, returnResource_t resource) {
    LPEDICT best = NULL;
    FLOAT best_dist = 0;
    FILTER_EDICTS(ent, resource == RETURN_RESOURCE_GOLD ? S_GoldMineCanHarvest(ent) :
        ent->inuse && ent->targtype == TARG_TREE && !M_IsDead(ent)) {
        FLOAT dist = Vector2_distance(&town->s.origin2, &ent->s.origin2);
        if (resource == RETURN_RESOURCE_GOLD && G_BotMineOwner(player, ent) != town) continue;
        if (!best || dist < best_dist) { best = ent; best_dist = dist; }
    }
    return best;
}

LPEDICT G_BotTownMine(LPPLAYER player, LONG town) {
    LPEDICT hall = G_BotTown(player, town);
    return hall ? G_BotHarvestTarget(player, hall, RETURN_RESOURCE_GOLD) : NULL;
}

LONG G_BotTownWithMine(LPPLAYER player) {
    for (LONG town = 0; G_BotTown(player, town); town++)
        if (G_BotTownMine(player, town)) return town;
    return -1;
}

DWORD G_BotMinesOwned(LPPLAYER player) {
    DWORD count = 0;
    for (LONG town = 0; G_BotTown(player, town); town++)
        if (G_BotTownMine(player, town)) count++;
    return count;
}

DWORD G_BotGoldOwned(LPPLAYER player) {
    DWORD gold = 0;
    for (LONG town = 0; G_BotTown(player, town); town++) {
        LPEDICT mine = G_BotTownMine(player, town);
        if (mine) gold += mine->resources;
    }
    return gold;
}

static BOOL G_BotUnitAtTown(LPPLAYER player, LPEDICT unit, LONG town_id) {
    LPEDICT town, nearest = NULL, candidate;
    FLOAT best_dist = 0;
    if (town_id < 0) return true;
    town = G_BotTown(player, town_id);
    if (!town) return false;
    for (LONG index = 0; (candidate = G_BotTown(player, index)); index++) {
        FLOAT dist = Vector2_distance(&candidate->s.origin2, &unit->s.origin2);
        if (!nearest || dist < best_dist) { nearest = candidate; best_dist = dist; }
    }
    return nearest == town;
}

static BOOL G_BotBuildSiteReachable(LPEDICT worker, LPCVECTOR2 point) {
    return worker && point && CM_LineIsWalkableForRadius(&worker->s.origin2, point, MAX(0.0f, worker->collision));
}

static BOOL G_BotBuildNearTown(LPPLAYER player, DWORD class_id, LONG town_id) {
    LPEDICT town = G_BotTown(player, town_id < 0 ? 0 : town_id);
    if (!town) return false;
    /* Pending footprints are not baked yet, so serialize them to keep later orders from invalidating earlier placement. */
    FILTER_EDICTS(unit, G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) && unit->build_project)
        return false;
    FILTER_EDICTS(worker, G_BotUnitAlive(worker) && worker->s.player == PLAYER_NUM(player) &&
        !worker->construction.active && !worker->training && !worker->build_project &&
        (!worker->currentmove || worker->currentmove->ability != &a_repair) && G_WorkerCanBuild(worker, class_id)) {
        for (LONG ring = 1; ring <= BOT_BUILD_SEARCH_RINGS; ring++) {
            for (LONG x = -ring; x <= ring; x++) for (LONG y = -ring; y <= ring; y++) {
                VECTOR2 point;
                if (abs(x) != ring && abs(y) != ring) continue;
                point = MAKE(VECTOR2, town->s.origin2.x + x * BOT_BUILD_GRID,
                             town->s.origin2.y + y * BOT_BUILD_GRID);
                if (!G_BotBuildSiteReachable(worker, &point)) continue;
                if (G_IssueBuildOrder(worker, class_id, &point)) return true;
            }
        }
    }
    return false;
}

/* common.ai has already bounded qty by resources; each accepted action still performs authoritative checks/payment. */
BOOL G_BotProduce(LPPLAYER player, LONG qty, DWORD class_id, LONG town_id) {
    DWORD made = 0;
    if (!player || qty <= 0 || !class_id) return false;
#ifdef WC3_DEBUG_AI
    fprintf(stderr, "WC3_DEBUG_AI produce request player=%u qty=%d id=%.4s town=%d\n",
        PLAYER_NUM(player), qty, (LPCSTR)&class_id, town_id);
#endif
    while (qty-- > 0) {
        if (G_UnitIsBuilding(class_id)) {
            if (!G_BotBuildNearTown(player, class_id, town_id)) break;
            made++;
            break; /* common.ai retries deficits; one pending footprint at a time prevents overlapping reservations. */
        } else {
            LPEDICT producer = NULL;
            FILTER_EDICTS(ent, G_BotUnitAlive(ent) && ent->s.player == PLAYER_NUM(player) &&
                !ent->construction.active && !ent->training && G_BotUnitAtTown(player, ent, town_id) &&
                G_GetTrainCommandState(G_GetPlayerClientByNumber(ent->s.player), ent, class_id, NULL, 0) ==
                    BUILD_COMMAND_AVAILABLE) { producer = ent; break; }
            if (!producer || !SP_TrainUnit(producer, class_id)) break;
        }
        made++;
    }
#ifdef WC3_DEBUG_AI
    fprintf(stderr, "WC3_DEBUG_AI produce result id=%.4s made=%u\n", (LPCSTR)&class_id, made);
#endif
    return made > 0;
}

static BOOL G_BotHarvesting(LPEDICT unit, returnResource_t resource) {
    ability_t *ability = resource == RETURN_RESOURCE_GOLD ? &a_goldmine : &a_harvest;
    return unit->currentmove && unit->currentmove->ability == ability;
}

/* A ClearHarvestAI pass preserves active jobs, then assigns each remaining worker once. */
void G_BotHarvest(LPPLAYER player, LONG town_id, LONG peons, BOOL gold) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    returnResource_t resource = gold ? RETURN_RESOURCE_GOLD : RETURN_RESOURCE_LUMBER;
    LPEDICT town, target;
    if (!bot || peons <= 0 || !(town = G_BotTown(player, town_id)) ||
        !(target = G_BotHarvestTarget(player, town, resource))) return;
    FILTER_EDICTS(unit, peons > 0 && G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) &&
        G_BotHarvesting(unit, resource) && !G_BotHarvesterReserved(bot, unit)) {
        G_BotReserveHarvester(bot, unit); peons--;
    }
    while (peons-- > 0) {
        LPEDICT best = NULL;
        FLOAT best_dist = 0;
        /* Preserve accepted construction orders; harvest reassignment used to strand their pending footprints. */
        FILTER_EDICTS(unit, G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) && !unit->training &&
            !unit->construction.active && !unit->build_project &&
            (!unit->currentmove || (unit->currentmove->ability != &a_goldmine &&
             unit->currentmove->ability != &a_harvest && unit->currentmove->ability != &a_repair)) && unit->data.UnitAbilities &&
            G_ActorHasSkill(unit, "Ahar") && !G_BotHarvesterReserved(bot, unit)) {
            FLOAT dist = Vector2_distance(&town->s.origin2, &unit->s.origin2);
            if (!best || dist < best_dist) { best = unit; best_dist = dist; }
        }
        if (!best) return;
        G_BotReserveHarvester(bot, best);
        if (best->harvested_gold) harvest_gold_return_to(best, town);
        else if (best->harvested_lumber) harvest_lumber_return_to(best, town);
        else if (resource == RETURN_RESOURCE_GOLD) harvest_gold_start(best, target);
        else harvest_start(best, target);
    }
}

/* Blizzard AI owns one assault and one defense captain; recreation drops all prior membership and orders. */
void G_BotCreateCaptains(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    G_BotClearCaptains(bot);
}

/* Captain members remain in TownCount, so common.ai adds this count when requesting their replacements. */
DWORD G_BotIgnoredUnits(LPPLAYER player, DWORD class_id) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    DWORD count = 0;
    if (!bot) return 0;
    FOR_LOOP(i, BOT_CAPTAIN_COUNT) FOR_EACH_ARRAY(LPEDICT, unit, bot->captains[i].units)
        if (G_BotUnitAlive(*unit) && (*unit)->s.player == PLAYER_NUM(player) && (*unit)->class_id == class_id) count++;
    FOR_EACH_ARRAY(botGuardPost_t, post, bot->guards)
        if (G_BotUnitAlive(post->unit) && post->unit->s.player == PLAYER_NUM(player) && post->unit->class_id == class_id) count++;
    return count;
}

/* Combat belongs to members, not formation state; validating each target also clears stale combat links. */
BOOL G_BotCaptainInCombat(LPPLAYER player, BOOL attack) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    if (!bot) return false;
    captain = bot->captains + (attack ? BOT_CAPTAIN_ATTACK : BOT_CAPTAIN_DEFENSE);
    FOR_EACH_ARRAY(LPEDICT, unit, captain->units)
        if (G_BotUnitAlive(*unit) && unit_affectingcombat(*unit)) return true;
    return false;
}

static BOOL G_BotCaptainHasUnit(bot_t *bot, LPEDICT unit) {
    FOR_LOOP(i, BOT_CAPTAIN_COUNT) FOR_EACH_ARRAY(LPEDICT, member, bot->captains[i].units)
        if (*member == unit) return true;
    return false;
}

/* Script formation retries rebuild only the assault roster; the defense captain remains independent. */
void G_BotInitAssault(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    if (!bot) return;
    captain = bot->captains + BOT_CAPTAIN_ATTACK;
    if (captain->units) gi.MemFree(captain->units);
    memset(captain, 0, sizeof(*captain)); captain->state = BOT_CAPTAIN_FORMING;
#ifdef WC3_DEBUG_AI
    fprintf(stderr, "WC3_DEBUG_AI assault init player=%u\n", PLAYER_NUM(player));
#endif
}

static void G_BotCaptainAdd(botCaptain_t *captain, LPEDICT unit) {
    DWORD count = ARRAY_COUNT(captain->units);
    LPEDICT *units = gi.MemAlloc((count + 1) * sizeof(*units));
    if (count) memcpy(units, captain->units, count * sizeof(*units));
    if (captain->units) gi.MemFree(captain->units);
    captain->units = units; ARRAY_COUNT(captain->units) = count + 1; captain->units[count] = unit;
}

/* Production is requested by common.ai; roster fills never steal units assigned to the other captain. */
static BOOL G_BotCaptainFill(LPPLAYER player, botCaptainType_t type, LONG qty, DWORD class_id) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    LONG have = 0;
    if (!bot || qty <= 0 || !class_id) return qty <= 0;
    captain = bot->captains + type;
    FOR_EACH_ARRAY(LPEDICT, unit, captain->units)
        if (G_BotUnitAlive(*unit) && (*unit)->class_id == class_id) have++;
    FILTER_EDICTS(unit, have < qty && G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) &&
        unit->class_id == class_id && !unit->construction.active && !unit->training && !G_BotCaptainHasUnit(bot, unit)) {
        G_BotCaptainAdd(captain, unit); have++;
    }
    return have >= qty;
}

BOOL G_BotAddAssault(LPPLAYER player, LONG qty, DWORD class_id) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    BOOL ready;
    if (bot && qty > 0 && class_id) bot->captains[BOT_CAPTAIN_ATTACK].desired += qty;
    ready = G_BotCaptainFill(player, BOT_CAPTAIN_ATTACK, qty, class_id);
#ifdef WC3_DEBUG_AI
    fprintf(stderr, "WC3_DEBUG_AI assault add player=%u qty=%d id=%.4s ready=%d size=%u desired=%d\n",
        player ? PLAYER_NUM(player) : MAX_PLAYERS, qty, (LPCSTR)&class_id, ready,
        G_BotCaptainGroupSize(player), bot ? bot->captains[BOT_CAPTAIN_ATTACK].desired : 0);
#endif
    return ready;
}

DWORD G_BotCaptainGroupSize(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    DWORD count = 0;
    if (!bot) return 0;
    FOR_EACH_ARRAY(LPEDICT, unit, bot->captains[BOT_CAPTAIN_ATTACK].units)
        if (G_BotUnitAlive(*unit)) count++;
    return count;
}

BOOL G_BotCaptainIsFull(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    return bot && G_BotCaptainGroupSize(player) >= bot->captains[BOT_CAPTAIN_ATTACK].desired;
}

/* Blizzard scores heroes and ordinary units separately so one healthy category cannot hide the other's losses. */
LONG G_BotCaptainReadiness(LPPLAYER player, BOOL mana) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    FLOAT cur[2] = {0}, max[2] = {0};
    if (!bot) return 100;
    FOR_EACH_ARRAY(LPEDICT, unit, bot->captains[BOT_CAPTAIN_ATTACK].units) {
        DWORD hero;
        if (!G_BotUnitAlive(*unit)) continue;
        hero = G_UnitIsHero(*unit) ? 1 : 0;
        cur[hero] += mana ? (*unit)->mana.value : (*unit)->health.value;
        max[hero] += mana ? (*unit)->mana.max_value : (*unit)->health.max_value;
    }
    /* The original fixed-real divider defines equal operands, including 0/0, as 1.0. */
    FOR_LOOP(i, 2) cur[i] = cur[i] == max[i] ? 100.0f : cur[i] * 100.0f / max[i];
    return (LONG)MIN(cur[0], cur[1]);
}

BOOL G_BotAddDefenders(LPPLAYER player, LONG qty, DWORD class_id) {
    return G_BotCaptainFill(player, BOT_CAPTAIN_DEFENSE, qty, class_id);
}

void G_BotAddGuardPost(LPPLAYER player, DWORD class_id, FLOAT x, FLOAT y) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botGuardPost_t *guards;
    DWORD count;
    if (!bot || !class_id) return;
    count = ARRAY_COUNT(bot->guards); guards = gi.MemAlloc((count + 1) * sizeof(*guards));
    if (count) memcpy(guards, bot->guards, count * sizeof(*guards));
    if (bot->guards) gi.MemFree(bot->guards);
    bot->guards = guards; ARRAY_COUNT(bot->guards) = count + 1;
    bot->guards[count] = MAKE(botGuardPost_t, class_id, MAKE(VECTOR2, x, y), NULL);
}

static BOOL G_BotGuardHasUnit(bot_t *bot, LPEDICT unit) {
    FOR_EACH_ARRAY(botGuardPost_t, post, bot->guards) if (post->unit == unit) return true;
    return false;
}

/* Guard posts reserve ordinary completed units independently from the two captain rosters. */
void G_BotFillGuardPosts(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    FOR_EACH_ARRAY(botGuardPost_t, post, bot->guards) {
        if (G_BotUnitAlive(post->unit) && post->unit->s.player == PLAYER_NUM(player) && post->unit->class_id == post->class_id) continue;
        post->unit = NULL;
        FILTER_EDICTS(unit, !post->unit && G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) &&
            unit->class_id == post->class_id && !unit->construction.active && !unit->training &&
            !G_BotCaptainHasUnit(bot, unit) && !G_BotGuardHasUnit(bot, unit)) post->unit = unit;
    }
}

/* A fighting guard keeps its combat target; an idle guard outside its post radius walks home. */
void G_BotReturnGuardPosts(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    FOR_EACH_ARRAY(botGuardPost_t, post, bot->guards) {
        if (!G_BotUnitAlive(post->unit)) { post->unit = NULL; continue; }
        if (!unit_affectingcombat(post->unit) && Vector2_distance(&post->unit->s.origin2, &post->origin) > BOT_GUARD_RETURN_RANGE)
            order_move(post->unit, Waypoint_add(&post->origin));
    }
}

/* CommandAI is a per-player stack: GetLast* observes the newest command until PopLastCommand removes it. */
BOOL G_BotPushCommand(LPPLAYER player, LONG command, LONG data) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCommand_t *commands;
    DWORD count;
    if (!bot) return false;
    count = ARRAY_COUNT(bot->commands);
    commands = gi.MemAlloc((count + 1) * sizeof(*commands));
    if (count) memcpy(commands, bot->commands, count * sizeof(*commands));
    if (bot->commands) gi.MemFree(bot->commands);
    bot->commands = commands; ARRAY_COUNT(bot->commands) = count + 1;
    bot->commands[count] = MAKE(botCommand_t, command, data);
    return true;
}

DWORD G_BotCommandsWaiting(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    return bot ? ARRAY_COUNT(bot->commands) : 0;
}

LONG G_BotLastCommand(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    return bot && ARRAY_COUNT(bot->commands) ? bot->commands[ARRAY_COUNT(bot->commands) - 1].command : 0;
}

LONG G_BotLastData(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    return bot && ARRAY_COUNT(bot->commands) ? bot->commands[ARRAY_COUNT(bot->commands) - 1].data : 0;
}

void G_BotPopCommand(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (bot && ARRAY_COUNT(bot->commands)) ARRAY_COUNT(bot->commands)--;
}

/* AI script paths are normally basenames; preserve an explicit archive path when a map supplies one. */
static BOOL G_BotScriptPath(LPCSTR script, LPSTR path, size_t size) {
    int len;
    if (!script || !*script) return false;
    len = strchr(script, '\\') || strchr(script, '/') ? snprintf(path, size, "%s", script) :
        snprintf(path, size, "Scripts\\%s", script);
    return len >= 0 && (size_t)len < size;
}

void G_BotStop(DWORD player) {
    bot_t *bot = G_BotState(player);
    if (!bot) return;
    if (bot->vm) jass_close(bot->vm);
    G_BotClearCaptains(bot);
    if (bot->commands) gi.MemFree(bot->commands);
    if (bot->harvesters) gi.MemFree(bot->harvesters);
    if (bot->guards) gi.MemFree(bot->guards);
    memset(bot, 0, sizeof(*bot));
}

/* Removal can originate inside the player's AI coroutine, so teardown waits until that resume returns. */
void G_BotRequestStop(DWORD player) {
    bot_t *bot = G_BotState(player);
    if (bot && bot->vm) { bot->stop_requested = true; jass_haltevents(bot->vm); }
}

void G_BotShutdown(void) {
    FOR_LOOP(player, MAX_PLAYERS) G_BotStop(player);
}

/* Each bot gets a private JASS root because common.ai stores all policy state in globals. */
BOOL G_BotStart(LPPLAYER player, LPCSTR script, botMode_t mode) {
    bot_t *bot;
    char path[MAX_PATHLEN];
    DWORD playernum;

    if (!player || !G_BotScriptPath(script, path, sizeof(path))) {
        fprintf(stderr, "WC3 AI: invalid player or script\n");
        return false;
    }
    playernum = PLAYER_NUM(player);
    bot = G_BotState(playernum);
    if (!bot) {
        fprintf(stderr, "WC3 AI: player %u is out of range\n", playernum);
        return false;
    }
    if (bot->vm && jass_isrunning(bot->vm)) {
        bot->restart_requested = true;
        bot->pending_mode = mode;
        strlcpy(bot->pending_script, path, sizeof(bot->pending_script));
        jass_haltevents(bot->vm);
        return true;
    }

    G_BotStop(playernum);
    /* AI VMs can start before map spawning, which previously left the shared JASS allocator unset. */
    G_InitJassHost();
    bot->vm = jass_newstate();
    bot->player = player;
    bot->mode = mode;
    strlcpy(bot->script, path, sizeof(bot->script));
    if (!jass_dofile(bot->vm, "Scripts\\common.j")) {
        fprintf(stderr, "WC3 AI: player %u could not load Scripts\\common.j\n", playernum);
        G_BotStop(playernum);
        return false;
    }
    if (!jass_dofile(bot->vm, "Scripts\\common.ai")) {
        fprintf(stderr, "WC3 AI: player %u could not load Scripts\\common.ai\n", playernum);
        G_BotStop(playernum);
        return false;
    }
    if (!jass_dofile(bot->vm, path)) {
        fprintf(stderr, "WC3 AI: player %u could not load %s\n", playernum, path);
        G_BotStop(playernum);
        return false;
    }
    if (!jass_startcoroutinebynameforplayer(bot->vm, "main", player)) {
        fprintf(stderr, "WC3 AI: player %u script %s has no main\n", playernum, path);
        G_BotStop(playernum);
        return false;
    }
    fprintf(stderr, "WC3 AI: player %u started %s\n", playernum, path);
    return true;
}

void G_BotPause(DWORD player, BOOL paused) {
    bot_t *bot = G_BotState(player);
    if (bot && bot->vm) bot->paused = paused;
}

void G_BotRunFrame(void) {
    FOR_LOOP(player, MAX_PLAYERS) {
        bot_t *bot = level.bots + player;
        if (!bot->vm) continue;
        if (bot->stop_requested) { G_BotStop(player); continue; }
        if (bot->paused) continue;
        jass_runevents(bot->vm);
        if (bot->stop_requested) { G_BotStop(player); continue; }
        if (bot->restart_requested) {
            LPPLAYER owner = bot->player;
            botMode_t mode = bot->pending_mode;
            char script[MAX_PATHLEN];
            strlcpy(script, bot->pending_script, sizeof(script));
            G_BotStop(player);
            G_BotStart(owner, script, mode);
            continue;
        }
        if (!jass_rterror_pending(bot->vm)) continue;
        fprintf(stderr, "WC3 AI: player %u script %s stopped: %s\n", player, bot->script,
            jass_rterror_message(bot->vm));
        G_BotStop(player);
    }
}
